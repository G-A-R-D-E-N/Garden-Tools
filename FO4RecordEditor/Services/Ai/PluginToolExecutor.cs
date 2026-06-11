using System.Text.Json;

namespace FO4RecordEditor.Services;

/// <summary>
/// Executes the AI's tool calls against the loaded plugin data (via MutagenLoader's
/// cached per-mod index). The env is resolved lazily through a provider so it always
/// reflects the current Load Order / opened ESPs.
/// </summary>
public sealed class PluginToolExecutor
{
    private readonly Func<object?> _envProvider;
    private readonly Func<string?>? _mo2PathProvider;

    public PluginToolExecutor(Func<object?> envProvider, Func<string?>? mo2PathProvider = null)
    {
        _envProvider = envProvider;
        _mo2PathProvider = mo2PathProvider;
    }

    public sealed record ToolSpec(string Name, string Description, object Schema);

    // Single source of truth for the tools; projected into Anthropic (input_schema) and
    // MCP (inputSchema) formats so the API path and the Claude Code / MCP path stay in sync.
    private static readonly ToolSpec[] _specs =
    [
        new("list_plugins",
            "List every loaded plugin (ESP/ESM/ESL) the user has open. Call this first to discover what data is available.",
            new { type = "object", properties = new { }, required = Array.Empty<string>() }),
        new("list_record_types",
            "List the record-type signatures (e.g. WEAP, ARMO, COBJ, NPC_, LVLI) present in a given plugin.",
            new
            {
                type = "object",
                properties = new { plugin = new { type = "string", description = "Plugin file name, e.g. 'MyMod.esp'." } },
                required = new[] { "plugin" }
            }),
        new("list_records",
            "List records of a specific type in a plugin (EditorID + FormKey). Use to browse a group.",
            new
            {
                type = "object",
                properties = new
                {
                    plugin = new { type = "string" },
                    type = new { type = "string", description = "Record signature, e.g. 'WEAP'." },
                    limit = new { type = "integer", description = "Max records to return (default 100)." }
                },
                required = new[] { "plugin", "type" }
            }),
        new("search_records",
            "Search a plugin for records whose EditorID or FormKey contains the query text. Returns EditorID, type and FormKey.",
            new
            {
                type = "object",
                properties = new
                {
                    plugin = new { type = "string" },
                    query = new { type = "string" },
                    limit = new { type = "integer", description = "Max matches (default 50)." }
                },
                required = new[] { "plugin", "query" }
            }),
        new("get_record",
            "Get the full field dump of a single record by FormKey ('001234:Plugin.esp') or EditorID, within a plugin.",
            new
            {
                type = "object",
                properties = new
                {
                    plugin = new { type = "string" },
                    id = new { type = "string", description = "FormKey or EditorID of the record." }
                },
                required = new[] { "plugin", "id" }
            }),
        new("get_conflicts",
            "Show EVERY plugin that overrides a record, in load order, and which one WINS. The winner " +
            "is the plugin loaded LAST -- it is what the game actually uses, NOT the first override you " +
            "find. Always call this before blaming a plugin for a conflict. Accepts a FormKey " +
            "('054C84:Fallout4.esm') or an EditorID.",
            new
            {
                type = "object",
                properties = new { id = new { type = "string", description = "FormKey or EditorID of the record." } },
                required = new[] { "id" }
            }),
        new("get_winning_record",
            "Get the EFFECTIVE (winning) version of a record -- the field values the game actually uses " +
            "after all overrides are applied -- plus which plugin won. Use this instead of get_record when " +
            "you care about the real in-game result. Accepts a FormKey or an EditorID.",
            new
            {
                type = "object",
                properties = new { id = new { type = "string", description = "FormKey or EditorID of the record." } },
                required = new[] { "id" }
            }),
        new("search_robco_configs",
            "Search RobCo Patcher config files (F4SE\\Plugins\\RobCo_Patcher\\**\\*.ini) for text. These " +
            "INI files patch records at RUNTIME (scrap recipes, leveled lists, weapons, components, etc.) " +
            "and are NOT in any plugin -- so if a behaviour can't be explained by the plugin records, a " +
            "RobCo patch is the usual cause. Search by FormID ('054C83'), EditorID, or a component/keyword " +
            "name. Returns the mod + matching config lines.",
            new
            {
                type = "object",
                properties = new
                {
                    query = new { type = "string", description = "Text to find, e.g. a FormID, EditorID, or component name." },
                    limit = new { type = "integer", description = "Max matching lines (default 60)." }
                },
                required = new[] { "query" }
            }),

        // ---- write tools (author plugins) ----
        new("open_plugin",
            "Open an existing loaded plugin for editing so records can be added or modified. " +
            "Required before using create_record or set_field on a plugin that was loaded from the game environment rather than created with create_plugin.",
            new
            {
                type = "object",
                properties = new { plugin = new { type = "string", description = "Plugin file name, e.g. 'MyMod.esp'." } },
                required = new[] { "plugin" }
            }),
        new("create_plugin",
            "Create a brand-new, empty plugin (ESP) the AI can author. Changes stay in memory until save_plugin.",
            new
            {
                type = "object",
                properties = new { name = new { type = "string", description = "Plugin file name, e.g. 'MyMod.esp'." } },
                required = new[] { "name" }
            }),
        new("create_record",
            "Add a new record of a given type to a plugin. Existing loaded plugins are opened for editing automatically. Returns the new FormKey. " +
            "Supported types: BOOK/HOLOTAPE, TERM, WEAP, ARMO, MISC, COBJ, KYWD, AMMO, ALCH, ACTI, CONT, FLST, MGEF, PERK, NPC_, QUST, MESG.",
            new
            {
                type = "object",
                properties = new
                {
                    plugin = new { type = "string" },
                    type = new { type = "string", description = "Record signature, e.g. 'BOOK' for a holotape." },
                    editorId = new { type = "string" }
                },
                required = new[] { "plugin", "type", "editorId" }
            }),
        new("set_field",
            "Set a field on a record. Supports scalar/text fields (Name, Value, Weight, Description), FormLink references " +
            "(pass a FormKey like '001234:Plugin.esp' or an EditorID of a loaded record), the mesh path " +
            "(field 'Model' or 'nif' -> sets Model.File), and nested fields via dotted paths (e.g. 'Model.File'). " +
            "Existing plugins are opened automatically.",
            new
            {
                type = "object",
                properties = new
                {
                    plugin = new { type = "string" },
                    record = new { type = "string", description = "FormKey or EditorID of the record." },
                    field = new { type = "string", description = "Property name, e.g. 'Name'." },
                    value = new { type = "string" }
                },
                required = new[] { "plugin", "record", "field", "value" }
            }),
        new("add_list_item",
            "Append an item to a list field (e.g. add a keyword to a record's 'Keywords', or a FormLink to a " +
            "FormList's 'Items'). Pass a FormKey ('001234:Plugin.esp') or an EditorID of a loaded record.",
            new
            {
                type = "object",
                properties = new
                {
                    plugin = new { type = "string" },
                    record = new { type = "string", description = "FormKey or EditorID of the record." },
                    field = new { type = "string", description = "List field, e.g. 'Keywords' or 'Items'." },
                    value = new { type = "string", description = "FormKey or EditorID to add." }
                },
                required = new[] { "plugin", "record", "field", "value" }
            }),
        new("attach_script",
            "Attach a Papyrus script to a record (creates the script adapter if needed). Then configure it with " +
            "set_script_property. This is how a holotape links to its terminal program in FO4.",
            new
            {
                type = "object",
                properties = new
                {
                    plugin = new { type = "string" },
                    record = new { type = "string", description = "FormKey or EditorID of the record." },
                    script = new { type = "string", description = "Script name, e.g. 'DefaultHolotapeProgram'." }
                },
                required = new[] { "plugin", "record", "script" }
            }),
        new("set_script_property",
            "Set a property on a record's attached script. 'type' is 'object' (a FormLink -- pass a FormKey or EditorID, " +
            "e.g. the terminal), 'int', 'float', 'bool', or 'string'. Omit type to infer it.",
            new
            {
                type = "object",
                properties = new
                {
                    plugin = new { type = "string" },
                    record = new { type = "string" },
                    script = new { type = "string" },
                    name = new { type = "string", description = "Property name on the script." },
                    value = new { type = "string" },
                    type = new { type = "string", description = "object | int | float | bool | string (optional)." }
                },
                required = new[] { "plugin", "record", "script", "name", "value" }
            }),
        new("check_plugin",
            "Run an error check over a plugin (xEdit-style): lists deleted records and references to masters " +
            "the plugin doesn't declare. Read-only.",
            new
            {
                type = "object",
                properties = new { plugin = new { type = "string" } },
                required = new[] { "plugin" }
            }),
        new("compact_to_esl",
            "Renumber a plugin's records into the ESL range (0x800-0xFFF) and fix all references, so a too-big " +
            "plugin can become a light master. In memory until you save_plugin (save with a .esl path).",
            new
            {
                type = "object",
                properties = new { plugin = new { type = "string" } },
                required = new[] { "plugin" }
            }),
        new("clean_plugin",
            "Clean a plugin xEdit-style: undelete deleted records and mark them initially disabled (UDR), " +
            "which removes hard deletes that cause crashes. Call save_plugin afterwards.",
            new
            {
                type = "object",
                properties = new { plugin = new { type = "string" } },
                required = new[] { "plugin" }
            }),
        new("save_plugin",
            "Write a plugin to disk. By default OVERWRITES the original file the plugin was opened from (in-place edit); " +
            "newly created plugins go to the configured Output folder. Pass 'path' to choose a specific location.",
            new
            {
                type = "object",
                properties = new
                {
                    plugin = new { type = "string" },
                    path = new { type = "string", description = "Optional full output path; defaults to <app>/Output/<plugin>." }
                },
                required = new[] { "plugin" }
            }),
    ];

    /// <summary>Anthropic Messages API tool format (input_schema).</summary>
    public static object[] ToolDefinitions() =>
        _specs.Select(t => (object)new { name = t.Name, description = t.Description, input_schema = t.Schema }).ToArray();

    /// <summary>
    /// Tool definitions with a prompt-cache breakpoint on the LAST tool. Tools render before the
    /// system prompt and never change, so this caches the whole tool prefix across every request —
    /// the cached tokens then cost ~0.1x. cache_control is null (dropped by WhenWritingNull) on the
    /// others.
    /// </summary>
    public static object[] ToolDefinitionsCached() =>
        _specs.Select((t, i) => (object)new
        {
            name = t.Name,
            description = t.Description,
            input_schema = t.Schema,
            cache_control = i == _specs.Length - 1 ? new { type = "ephemeral" } : null,
        }).ToArray();

    /// <summary>MCP tool format (inputSchema) for the Claude Code MCP server.</summary>
    public static object[] McpToolDefinitions() =>
        _specs.Select(t => (object)new { name = t.Name, description = t.Description, inputSchema = t.Schema }).ToArray();

    public string Execute(string toolName, string inputJson)
    {
        var env = _envProvider();
        using var doc = JsonDocument.Parse(string.IsNullOrWhiteSpace(inputJson) ? "{}" : inputJson);
        var root = doc.RootElement;

        string Str(string k) => root.TryGetProperty(k, out var v) ? v.GetString() ?? "" : "";
        int Int(string k, int def) => root.TryGetProperty(k, out var v) && v.TryGetInt32(out var i) ? i : def;

        switch (toolName)
        {
            case "list_plugins":
            {
                var plugins = MutagenLoader.QueryLoadedPlugins(env);
                return plugins.Count == 0 ? "No plugins loaded." : string.Join("\n", plugins);
            }
            case "list_record_types":
            {
                var types = MutagenLoader.QueryRecordTypes(env, Str("plugin"));
                return types.Count == 0 ? "No record types (plugin not loaded or empty)." : string.Join("\n", types);
            }
            case "list_records":
            {
                var recs = MutagenLoader.QueryRecordsOfType(env, Str("plugin"), Str("type"), Int("limit", 100));
                return recs.Count == 0 ? "No records of that type." : string.Join("\n", recs.Select(r => $"{r.editorId} [{r.formKey}]"));
            }
            case "search_records":
            {
                var hits = MutagenLoader.QuerySearch(env, Str("plugin"), Str("query"), Int("limit", 50));
                return hits.Count == 0 ? "No matches." : string.Join("\n", hits.Select(h => $"{h.editorId} ({h.type}) [{h.formKey}]"));
            }
            case "get_record":
                return MutagenLoader.QueryRecordFields(env, Str("plugin"), Str("id"));
            case "get_conflicts":
                return GetConflicts(env, Str("id"));
            case "get_winning_record":
                return GetWinningRecord(env, Str("id"));
            case "search_robco_configs":
                return SearchRobcoConfigs(_mo2PathProvider?.Invoke(), Str("query"), Int("limit", 60));

            // ---- write tools ----
            case "open_plugin":
                return WriteService.OpenPlugin(Str("plugin"), env);
            case "create_plugin":
                return WriteService.CreatePlugin(Str("name"));
            case "create_record":
                return WriteService.CreateRecord(Str("plugin"), Str("type"), Str("editorId"), env);
            case "set_field":
                return WriteService.SetField(Str("plugin"), Str("record"), Str("field"), Str("value"), env);
            case "add_list_item":
                return WriteService.AddListItem(Str("plugin"), Str("record"), Str("field"), Str("value"), env);
            case "attach_script":
                return WriteService.AttachScript(Str("plugin"), Str("record"), Str("script"), env);
            case "set_script_property":
                return WriteService.SetScriptProperty(Str("plugin"), Str("record"), Str("script"), Str("name"), Str("value"),
                    root.TryGetProperty("type", out var stv) ? stv.GetString() : null, env);
            case "check_plugin":
                return MutagenLoader.CheckPlugin(env, Str("plugin"));
            case "compact_to_esl":
                return WriteService.CompactToEsl(Str("plugin"), env);
            case "clean_plugin":
                return WriteService.CleanPlugin(Str("plugin"), env);
            case "save_plugin":
                return WriteService.SavePlugin(Str("plugin"), root.TryGetProperty("path", out var pv) ? pv.GetString() : null);

            default:
                return $"Unknown tool: {toolName}";
        }
    }

    // Resolve a FormKey ('001234:Plugin.esp') or an EditorID to a FormKey, using the load order's
    // link cache (resolves both, globally) with a fallback to the per-mod index.
    private static Mutagen.Bethesda.Plugins.FormKey ResolveToFk(object? env, string id)
    {
        if (string.IsNullOrWhiteSpace(id)) return Mutagen.Bethesda.Plugins.FormKey.Null;
        if (Mutagen.Bethesda.Plugins.FormKey.TryFactory(id, out var fk)) return fk;
        var cache = MutagenLoader.LinkCache;
        if (cache != null && cache.TryResolve(id, out var rec)) return rec.FormKey;
        return MutagenLoader.ResolveEditorIdToFormKey(env, id);
    }

    private static string GetConflicts(object? env, string id)
    {
        var fk = ResolveToFk(env, id);
        if (fk.IsNull)
            return $"Could not resolve '{id}'. Pass a FormKey like '054C84:Fallout4.esm' or an exact EditorID.";

        var matrix = MutagenLoader.BuildConflictMatrix(env, fk.ToString());
        if (matrix == null || matrix.Plugins.Count == 0) return $"No record found for {fk}.";

        var sb = new System.Text.StringBuilder();
        sb.AppendLine($"{matrix.EditorID} [{matrix.FormKey}] {matrix.Type}");
        if (matrix.Plugins.Count == 1)
        {
            sb.AppendLine($"Defined only in {matrix.Plugins[0]} -- no override conflict.");
        }
        else
        {
            int diffs = matrix.Rows.Count(r => r.Differs);
            sb.AppendLine($"Overridden by {matrix.Plugins.Count} plugins (load order -- the LAST one wins):");
            for (int i = 0; i < matrix.Plugins.Count; i++)
                sb.AppendLine($"  {i + 1}. {matrix.Plugins[i]}"
                    + (i == matrix.Plugins.Count - 1 ? "   <-- WINNER (what the game uses)" : ""));
            sb.AppendLine($"{diffs} field(s) differ across versions. Call get_winning_record for the effective values.");
        }
        return sb.ToString();
    }

    private static string GetWinningRecord(object? env, string id)
    {
        var fk = ResolveToFk(env, id);
        if (fk.IsNull)
            return $"Could not resolve '{id}'. Pass a FormKey or an exact EditorID.";

        var contexts = MutagenLoader.GetRecordContexts(env, fk);
        if (contexts.Count == 0) return $"No record found for {fk}.";

        var winner = contexts[^1].plugin;   // load order, last = winner
        var dump = MutagenLoader.QueryRecordFields(env, winner, fk.ToString());
        return $"Winning plugin: {winner}  (of {contexts.Count} version(s) in the load order)\n{dump}";
    }

    private static string SearchRobcoConfigs(string? instancePath, string query, int limit)
    {
        if (string.IsNullOrWhiteSpace(instancePath) || !System.IO.Directory.Exists(instancePath))
            return "MO2 instance unknown -- load the modlist with 'Open MO2' first.";
        if (string.IsNullOrWhiteSpace(query)) return "Provide a query (a FormID, EditorID, or name).";

        // Collect each mod's (and overwrite's) RobCo_Patcher root, then scan its .ini files.
        var roots = new List<string>();
        var modsDir = System.IO.Path.Combine(instancePath, "mods");
        if (System.IO.Directory.Exists(modsDir))
            foreach (var mod in System.IO.Directory.EnumerateDirectories(modsDir))
            {
                var rc = System.IO.Path.Combine(mod, "F4SE", "Plugins", "RobCo_Patcher");
                if (System.IO.Directory.Exists(rc)) roots.Add(rc);
            }
        var ow = System.IO.Path.Combine(instancePath, "overwrite", "F4SE", "Plugins", "RobCo_Patcher");
        if (System.IO.Directory.Exists(ow)) roots.Add(ow);

        var hits = new List<string>();
        foreach (var root in roots)
        {
            foreach (var ini in System.IO.Directory.EnumerateFiles(root, "*.ini", System.IO.SearchOption.AllDirectories))
            {
                string[] lines;
                try { lines = System.IO.File.ReadAllLines(ini); } catch { continue; }
                var rel = ini.Length > modsDir.Length && ini.StartsWith(modsDir, StringComparison.OrdinalIgnoreCase)
                    ? ini[(modsDir.Length + 1)..] : System.IO.Path.GetFileName(ini);
                foreach (var line in lines)
                {
                    var l = line.Trim();
                    if (l.Length == 0 || l.StartsWith(';') || l.StartsWith('#')) continue;
                    if (l.Contains(query, StringComparison.OrdinalIgnoreCase))
                    {
                        hits.Add($"{rel}\n    {l}");
                        if (hits.Count >= limit) goto done;
                    }
                }
            }
        }
    done:
        if (roots.Count == 0) return "No RobCo Patcher config folders found under this MO2 instance.";
        return hits.Count == 0
            ? $"No RobCo Patcher config lines match '{query}'. (Searched {roots.Count} mod patcher folder(s).)"
            : $"RobCo Patcher matches for '{query}' ({hits.Count}):\n" + string.Join("\n", hits);
    }
}
