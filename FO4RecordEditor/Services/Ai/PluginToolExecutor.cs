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

    public PluginToolExecutor(Func<object?> envProvider) => _envProvider = envProvider;

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
            "Set a field on a record. Supports scalar/text fields (Name, Value, Weight, Description) AND FormLink references " +
            "(pass a FormKey like '001234:Plugin.esp', or an EditorID of a loaded record). Existing plugins are opened automatically.",
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
        new("save_plugin",
            "Write a plugin to disk (defaults to the editor's Output folder). Call this to persist created records.",
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

            // ---- write tools ----
            case "open_plugin":
                return WriteService.OpenPlugin(Str("plugin"), env);
            case "create_plugin":
                return WriteService.CreatePlugin(Str("name"));
            case "create_record":
                return WriteService.CreateRecord(Str("plugin"), Str("type"), Str("editorId"), env);
            case "set_field":
                return WriteService.SetField(Str("plugin"), Str("record"), Str("field"), Str("value"), env);
            case "save_plugin":
                return WriteService.SavePlugin(Str("plugin"), root.TryGetProperty("path", out var pv) ? pv.GetString() : null);

            default:
                return $"Unknown tool: {toolName}";
        }
    }
}
