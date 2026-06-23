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
        new("list_records_summary",
            "Get a compact field table for ALL records of a type in a plugin — one row per record with " +
            "key fields (scalars, FormLinks, list counts) shown inline. Use this to survey an entire " +
            "record group AT ONCE instead of calling get_record in a loop. Essential for bulk conflict " +
            "analysis: e.g. all COBJ records with their CreatedObject + component count + workbench, " +
            "all WEAP with damage values, all NPC_ with race + level. Follow up with get_record ONLY " +
            "on specific records that need a complete field dump. 'type' is the Mutagen class name: " +
            "ConstructibleObject, Weapon, Armor, Npc, LeveledItem, FormList, etc.",
            new
            {
                type = "object",
                properties = new
                {
                    plugin = new { type = "string" },
                    type = new { type = "string", description = "Record type (Mutagen class name), e.g. 'ConstructibleObject'." },
                    limit = new { type = "integer", description = "Max records to return (default 200, max 500)." }
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
            "Show EVERY plugin that overrides a record in load order, which one WINS, and a " +
            "PER-FIELD diff table showing each plugin's value for every field that differs. " +
            "Severity tags: [OVERRIDE] = 2 distinct values; [CONFLICT] = 3+ distinct; [CRITICAL] = " +
            "winner value is empty/null while another plugin had a real value. " +
            "The LAST plugin is ALWAYS the winner -- it is what the game actually uses. " +
            "Call this BEFORE deciding what to patch. Accepts FormKey ('054C84:Fallout4.esm') or EditorID.",
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

        new("scan_conflicts",
            "Scan the ENTIRE load order for conflicting records (the same scan as the app's conflict view) " +
            "to survey what conflicts exist before planning fixes. Returns each conflicting record with the " +
            "plugins that touch it and the WINNER. By default only conflicts that involve a mod (non-vanilla) " +
            "are returned. Use this to get the big picture; then get_conflicts / get_winning_record on a " +
            "specific record for detail. Can be slow on a large modlist.",
            new
            {
                type = "object",
                properties = new
                {
                    mod_only = new { type = "boolean", description = "Only conflicts involving a non-vanilla plugin (default true)." },
                    type = new { type = "string", description = "Optional record-type filter, e.g. 'ConstructibleObject' or 'Weapon'." },
                    limit = new { type = "integer", description = "Max records to return (default 200)." }
                },
                required = Array.Empty<string>()
            }),
        new("get_referenced_by",
            "List records across the load order that REFERENCE the given record (xEdit 'Referenced By') -- i.e. " +
            "what would break if you changed or removed it. Accepts a FormKey or EditorID. Use for impact " +
            "analysis before editing or deleting a record.",
            new
            {
                type = "object",
                properties = new
                {
                    id = new { type = "string", description = "FormKey or EditorID of the record." },
                    limit = new { type = "integer", description = "Max referencing records (default 200)." }
                },
                required = new[] { "id" }
            }),
        new("get_problems",
            "Report problems with a single record's WINNING version: whether it is flagged DELETED, and any " +
            "FormLink references that don't resolve in the load order (dangling, crash-risk). Accepts a " +
            "FormKey or EditorID. Use to validate a record before/after editing.",
            new
            {
                type = "object",
                properties = new { id = new { type = "string", description = "FormKey or EditorID of the record." } },
                required = new[] { "id" }
            }),

        // ---- write tools (author plugins) ----
        new("open_plugin",
            "Open an existing plugin for editing so records can be added or modified. The 'plugin' arg " +
            "accepts a bare file name ('MyMod.esp') OR a full path ('D:\\\\...\\\\MyMod.esp'). Required before " +
            "create_record/set_field on a plugin loaded from the game environment rather than created with create_plugin.",
            new
            {
                type = "object",
                properties = new { plugin = new { type = "string", description = "Plugin file name or full path, e.g. 'MyMod.esp' or 'D:\\\\mods\\\\X\\\\MyMod.esp'." } },
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
            "Set a field on a record. Supports scalar/text fields (Name, Value, Weight, Description), booleans " +
            "(pass 'true'/'false' to check/UNCHECK a flag), enums (pass the value name), FormLink references " +
            "(a FormKey like '001234:Plugin.esp' or an EditorID), the mesh path ('Model'/'nif' -> Model.File), " +
            "and NESTED/INDEXED paths: dotted ('Model.File') and array indices ('Effects[0].RunImmediately', " +
            "'Components[0].Count', 'Conditions[1].Data.RunOnType'). Existing plugins are opened automatically.",
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
        new("renumber_formid",
            "Change one record's FormID within its plugin (xEdit 'Change FormID') and repoint every " +
            "reference to it in the SAME plugin. References in OTHER plugins are NOT rewritten. Call " +
            "get_referenced_by first and patch external referrers if any. In memory until save_plugin.",
            new
            {
                type = "object",
                properties = new
                {
                    plugin = new { type = "string" },
                    record = new { type = "string", description = "EditorID or current FormKey of the record." },
                    new_id = new { type = "string", description = "New 6-digit hex object id, e.g. '000F99'." }
                },
                required = new[] { "plugin", "record", "new_id" }
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
        new("backup_plugin",
            "Copy the plugin's original on-disk file to a timestamped .bak file beside it BEFORE making any edits. " +
            "ALWAYS call this before the first write to any existing plugin in a session. Safe to call multiple times — " +
            "each call produces a distinct backup (e.g. MyMod.esp.20250623_142031.bak).",
            new
            {
                type = "object",
                properties = new { plugin = new { type = "string", description = "Plugin file name, e.g. 'MyMod.esp'." } },
                required = new[] { "plugin" }
            }),
        new("save_plugin",
            "Write a plugin to disk. By default OVERWRITES the file it was opened from (or the full path it was " +
            "created with); newly created plugins go to the Output folder. Pass 'path' to choose a location. " +
            "NOTE: a plugin currently loaded via 'Open MO2' is memory-mapped and LOCKED by this editor, so it " +
            "can't be overwritten in place -- save_plugin then writes a '<name>.new' file beside it and says so. " +
            "If that happens, DO NOT tell the user to close MO2 (it's the editor's own lock); instead save under a " +
            "NEW plugin name not in the load order, or tell them to close the editor and rename the .new file.",
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
        new("copy_as_override",
            "Copy a record from one plugin into a patch plugin as an OVERRIDE (xEdit 'Copy as Override Into'). " +
            "The patch is opened/created automatically; afterwards edit the override with set_field, set_components, " +
            "set_conditions, add_list_item, or remove_list_item. Make the patch load LAST so it wins.",
            new
            {
                type = "object",
                properties = new
                {
                    source_plugin = new { type = "string", description = "Plugin to copy the record FROM (the version you want)." },
                    id = new { type = "string", description = "FormKey ('00008E:MAIM.esp') or EditorID of the record." },
                    patch_plugin = new { type = "string", description = "Patch plugin to copy the override INTO, e.g. 'FallenWorld_CraftingFixes.esp'." }
                },
                required = new[] { "source_plugin", "id", "patch_plugin" }
            }),
        new("delete_record",
            "Remove a record entirely from a plugin (xEdit 'Remove') — e.g. delete an incomplete stub record, " +
            "or drop an unwanted override from a patch.",
            new
            {
                type = "object",
                properties = new
                {
                    plugin = new { type = "string" },
                    id = new { type = "string", description = "FormKey or EditorID of the record to remove." }
                },
                required = new[] { "plugin", "id" }
            }),
        new("remove_list_item",
            "Remove a FormLink entry (by FormKey or EditorID) from a list field — e.g. drop an item from a " +
            "FormList's 'Items', or a keyword from 'Categories'/'Keywords'.",
            new
            {
                type = "object",
                properties = new
                {
                    plugin = new { type = "string" },
                    record = new { type = "string", description = "FormKey or EditorID of the record." },
                    field = new { type = "string", description = "List field, e.g. 'Items'." },
                    value = new { type = "string", description = "FormKey or EditorID to remove." }
                },
                required = new[] { "plugin", "record", "field", "value" }
            }),
        new("set_components",
            "Replace a COBJ (crafting recipe) record's component list — the materials it consumes. Pass a JSON " +
            "array of {\"component\":\"FormKeyOrEditorId\",\"count\":N}. Example: " +
            "[{\"component\":\"01FAA5:Fallout4.esm\",\"count\":1},{\"component\":\"1223C7:Fallout4.esm\",\"count\":3}].",
            new
            {
                type = "object",
                properties = new
                {
                    plugin = new { type = "string" },
                    record = new { type = "string", description = "FormKey or EditorID of the COBJ." },
                    components = new { type = "string", description = "JSON array of {component, count}." }
                },
                required = new[] { "plugin", "record", "components" }
            }),
        new("set_conditions",
            "Replace a COBJ's condition list (when a recipe is available). Pass a JSON array of condition objects: " +
            "{\"function\":\"GetGlobalValue\",\"param1\":\"000126:MAIM.esp\",\"operator\":\"==\",\"value\":0,\"runOn\":\"Subject\"}. " +
            "function = GetGlobalValue/GetBaseValue/GetItemCount/HasKeyword/GetStageDone/...; param1/param2 = a FormKey/EditorID " +
            "(record arg, e.g. the global or actor-value form) or an integer; operator = == != > >= < <=; value = the constant " +
            "compared against; compareGlobal = optional global FormKey to compare against instead of a constant; runOn = " +
            "Subject/Target/Reference; reference = the ref FormKey when runOn=Reference (e.g. '000014:Fallout4.esm' for PlayerRef); " +
            "flags = optional comma-separated condition flags, e.g. 'UseOr' to combine this condition with OR instead of AND.",
            new
            {
                type = "object",
                properties = new
                {
                    plugin = new { type = "string" },
                    record = new { type = "string", description = "FormKey or EditorID of the COBJ." },
                    conditions = new { type = "string", description = "JSON array of condition objects." }
                },
                required = new[] { "plugin", "record", "conditions" }
            }),
        new("revert_overrides",
            "BATCH-fix a mod that injected the same bad edit across many records (e.g. a corrupted recipe " +
            "template). For every record where 'bad_plugin' is currently the WINNER, this restores the " +
            "version loaded just before it, forwarded into a patch plugin — in ONE call, no per-record loop. " +
            "Defaults to a DRY RUN that lists what it would do; pass apply=true to write them. Optionally " +
            "filter by 'signature' (e.g. COBJ) and/or 'contains_component' (a FormKey/EditorID like the " +
            "hubflower) to target exactly the corrupted records.",
            new
            {
                type = "object",
                properties = new
                {
                    bad_plugin = new { type = "string", description = "The plugin whose overrides to undo, e.g. 'FallenWorldCrafting_Compat.esp'." },
                    patch_plugin = new { type = "string", description = "Patch to write the reverts into, e.g. 'FallenWorld_CraftingFixes.esp'." },
                    signature = new { type = "string", description = "Optional record type filter, e.g. 'COBJ'." },
                    contains_component = new { type = "string", description = "Optional: only revert COBJs whose winning version uses this component (FormKey/EditorID)." },
                    apply = new { type = "boolean", description = "false (default) = dry-run preview; true = write the reverts." },
                    limit = new { type = "integer", description = "Max records to list in the dry-run preview (default 50)." }
                },
                required = new[] { "bad_plugin", "patch_plugin" }
            }),
        new("batch_patch_records",
            "Apply field operations to ALL records of a type in a plugin IN ONE CALL — the bulk write " +
            "equivalent of list_records_summary for reading. Instead of N copy_as_override + N set_field " +
            "calls, this handles 3000+ records at once. Defaults to dry_run=true (safe preview). " +
            "Pass dry_run=false to write. 'operations' is a JSON array of {op, field, value}: " +
            "op = 'set' (default), 'add' (list append), 'remove' (list remove). " +
            "Optional filter_field + filter_value narrow to only records whose field contains that value " +
            "(FormLink fields match on EditorID or FormKey string). " +
            "Always dry-run first to confirm the record count and filter, then pass dry_run=false. " +
            "Example: fix all 3000 COBJ workbench keywords in one call with filter_field='WorkbenchKeyword' " +
            "filter_value='OldWorkbench' and op set WorkbenchKeyword to the new FormKey.",
            new
            {
                type = "object",
                properties = new
                {
                    source_plugin = new { type = "string", description = "Plugin whose records to patch." },
                    patch_plugin  = new { type = "string", description = "Patch plugin to write overrides into." },
                    type          = new { type = "string", description = "Record type (Mutagen class name), e.g. 'ConstructibleObject'." },
                    operations    = new { type = "string", description = "JSON array of operations: [{\"op\":\"set\",\"field\":\"WorkbenchKeyword\",\"value\":\"001234:Fallout4.esm\"}]." },
                    filter_field  = new { type = "string", description = "Optional: only patch records where this field contains filter_value." },
                    filter_value  = new { type = "string", description = "Filter value (substring match; FormLinks matched by EditorID or FormKey)." },
                    dry_run       = new { type = "boolean", description = "true (default) = preview only; false = write the patch." },
                    limit         = new { type = "integer", description = "Max records to process (default 5000)." }
                },
                required = new[] { "source_plugin", "patch_plugin", "type", "operations" }
            }),
        new("reload_plugin",
            "Evict a plugin from the editor session and re-open it fresh from disk. Fixes stale " +
            "in-memory state that causes check_plugin to report false-positive 'undeclared master' errors " +
            "after save_plugin writes new master references. Use this when check_plugin complains about a " +
            "master that the binary file already declares correctly.",
            new
            {
                type = "object",
                properties = new { plugin = new { type = "string", description = "Plugin file name, e.g. 'FWC_NonFWC_Schematics.esp'." } },
                required = new[] { "plugin" }
            }),
        new("strip_masters_clean",
            "Load a plugin binary, drop every COBJ condition whose param/reference FormKey belongs to " +
            "one of the named masters, then save via Mutagen so that the master list and all FormID " +
            "high-bytes are recomputed correctly. Use to fix master-index corruption caused by binary " +
            "header surgery (e.g. the ITO strip). source_path may be a .bak file. Defaults to a dry run; " +
            "pass dry_run=false to write. output_path defaults to source_path (overwrites in place).",
            new
            {
                type = "object",
                properties = new
                {
                    source_path = new { type = "string", description = "Full path to the binary plugin to load, e.g. 'E:\\\\...\\\\FWC_NonFWC_Schematics.esp.bak'." },
                    masters = new { type = "string", description = "JSON array of master file names to strip, e.g. '[\"ITO.esp\",\"ITOBeta.esp\"]'." },
                    output_path = new { type = "string", description = "Full path to write the result (optional; defaults to source_path)." },
                    dry_run = new { type = "boolean", description = "true (default) = preview only; false = write the file." }
                },
                required = new[] { "source_path", "masters" }
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

    /// <summary>Gemini function-declaration format. Gemini's schema wants UPPERCASE OpenAPI types
    /// (OBJECT/STRING/INTEGER/BOOLEAN/…), so we transform our lowercase JSON-schema types.</summary>
    public static object[] GeminiToolDefinitions() =>
        _specs.Select(t => (object)new
        {
            name = t.Name,
            description = t.Description,
            parameters = ToGeminiSchema(t.Schema),
        }).ToArray();

    // Built with System.Text.Json nodes so GeminiProvider (which serializes with System.Text.Json) emits it correctly.
    private static System.Text.Json.Nodes.JsonNode ToGeminiSchema(object schema)
    {
        var node = System.Text.Json.JsonSerializer.SerializeToNode(schema)!;
        UppercaseTypes(node);
        return node;
    }

    private static void UppercaseTypes(System.Text.Json.Nodes.JsonNode? node)
    {
        if (node is System.Text.Json.Nodes.JsonObject o)
        {
            if (o["type"] is System.Text.Json.Nodes.JsonValue tv && tv.TryGetValue<string>(out var ts))
                o["type"] = ts.ToUpperInvariant();
            foreach (var kv in o) UppercaseTypes(kv.Value);
        }
        else if (node is System.Text.Json.Nodes.JsonArray a)
        {
            foreach (var item in a) UppercaseTypes(item);
        }
    }

    // Hard ceiling on any single tool result. A full record dump or a big conflict matrix can be
    // tens of KB; returning that to the model is slow and trips Claude Code's "output too large".
    private const int MaxResultChars = 8000;

    /// <summary>Raised on the calling thread after every tool call, whether read or write.
    /// The GUI subscribes to drive its live PIE-mode feed and auto-refresh the open record.</summary>
    public record McpToolEvent(string Tool, string Plugin, string Record, string Field, string Summary, bool IsWrite);
    public static event Action<McpToolEvent>? ToolCompleted;

    public string Execute(string toolName, string inputJson)
    {
        DebugLog.Write("INFO", "AI-Tool", $"call {toolName}",
            string.IsNullOrWhiteSpace(inputJson) ? null : inputJson);
        string raw;
        try
        {
            raw = ExecuteInner(toolName, inputJson);
        }
        catch (Exception ex)
        {
            DebugLog.Exception($"AI-Tool {toolName}", ex);
            throw;   // the agent surfaces "Tool error: ..."; the full stack is now in the debug log
        }
        DebugLog.Write("DEBUG", "AI-Tool", $"done {toolName} -> {raw.Length} chars");
        try { ToolCompleted?.Invoke(ExtractEvent(toolName, inputJson)); } catch { }
        if (raw.Length <= MaxResultChars) return raw;
        return raw[..MaxResultChars] +
               $"\n\n…[truncated {raw.Length - MaxResultChars:N0} more chars. Narrow the request — e.g. ask for a " +
               "specific field, use a higher 'limit' sparingly, or use revert_overrides for bulk fixes instead of reading each record.]";
    }

    private static McpToolEvent ExtractEvent(string toolName, string inputJson)
    {
        using var doc = JsonDocument.Parse(string.IsNullOrWhiteSpace(inputJson) ? "{}" : inputJson);
        var root = doc.RootElement;
        string Str(string k) => root.TryGetProperty(k, out var v) ? v.GetString() ?? "" : "";

        var plugin = Str("plugin");
        var record = Str("record") is { Length: > 0 } r ? r : Str("id");
        var field  = Str("field");

        bool isWrite = toolName is "set_field" or "set_conditions" or "set_components" or
            "add_list_item" or "remove_list_item" or "open_plugin" or "save_plugin" or "backup_plugin" or
            "create_record" or "delete_record" or "copy_as_override" or "clean_plugin" or
            "compact_to_esl" or "renumber_formid" or "reload_plugin" or "strip_masters_clean" or
            "attach_script" or "set_script_property" or "revert_overrides" or "batch_patch_records";

        var val = Str("value");
        if (val.Length > 40) val = val[..37] + "...";

        string summary = toolName switch
        {
            "open_plugin"         => $"open {plugin}",
            "backup_plugin"       => $"backup {plugin}",
            "save_plugin"         => $"save {plugin}",
            "set_field"           => $"{ShortId(record)}.{field} = {val}",
            "set_conditions"      => $"{ShortId(record)}: conditions rebuilt",
            "set_components"      => $"{ShortId(record)}: components set",
            "add_list_item"       => $"{ShortId(record)}.{field} += {Str("value")}",
            "remove_list_item"    => $"{ShortId(record)}.{field} -= {Str("value")}",
            "create_record"       => $"create {Str("type")} '{Str("editorId")}' in {plugin}",
            "delete_record"       => $"delete {ShortId(record)} from {plugin}",
            "copy_as_override"    => $"{ShortId(Str("id"))} -> {Str("patch_plugin")}",
            "reload_plugin"       => $"reload {plugin}",
            "strip_masters_clean" => $"strip masters: {System.IO.Path.GetFileName(Str("source_path"))}",
            "check_plugin"        => $"check {plugin}",
            "clean_plugin"        => $"clean UDRs in {plugin}",
            "compact_to_esl"      => $"compact {plugin} to ESL",
            "renumber_formid"     => $"renumber a record in {plugin}",
            "get_record"          => $"read {ShortId(Str("id"))} in {plugin}",
            "list_records"         => $"list {Str("type")} in {plugin}",
            "list_records_summary" => $"summary {Str("type")} in {plugin}",
            "search_records"      => $"search '{Str("query")}' in {plugin}",
            "get_conflicts"       => $"conflicts for {ShortId(Str("id"))}",
            "get_winning_record"  => $"winning record {ShortId(Str("id"))}",
            "revert_overrides"    => $"revert {Str("bad_plugin")} -> {Str("patch_plugin")}",
            "batch_patch_records" => $"batch patch {Str("type")} in {Str("source_plugin")} -> {Str("patch_plugin")}",
            "scan_conflicts"      => "scan all conflicts",
            _                     => toolName.Replace('_', ' '),
        };

        return new McpToolEvent(toolName, plugin, record, field, summary, isWrite);
    }

    private static string ShortId(string id) => id.Length <= 32 ? id : id[..29] + "...";

    private string ExecuteInner(string toolName, string inputJson)
    {
        var env = _envProvider();
        using var doc = JsonDocument.Parse(string.IsNullOrWhiteSpace(inputJson) ? "{}" : inputJson);
        var root = doc.RootElement;

        string Str(string k) => root.TryGetProperty(k, out var v) ? v.GetString() ?? "" : "";
        int Int(string k, int def) => root.TryGetProperty(k, out var v) && v.TryGetInt32(out var i) ? i : def;
        bool Bool(string k) => root.TryGetProperty(k, out var v) && v.ValueKind == JsonValueKind.True;

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
            case "list_records_summary":
                return MutagenLoader.QueryRecordSummaries(env, Str("plugin"), Str("type"), Math.Min(Int("limit", 200), 500));
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
            case "scan_conflicts":
                return ScanConflicts(env, !root.TryGetProperty("mod_only", out var mo) || mo.ValueKind != JsonValueKind.False,
                    root.TryGetProperty("type", out var tf) ? tf.GetString() : null, Int("limit", 200));
            case "get_referenced_by":
                return GetReferencedByText(env, Str("id"), Int("limit", 200));
            case "get_problems":
                return GetProblemsText(env, Str("id"));

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
            case "renumber_formid":
                return WriteService.RenumberFormId(Str("plugin"), Str("record"), Str("new_id"), env);
            case "clean_plugin":
                return WriteService.CleanPlugin(Str("plugin"), env);
            case "backup_plugin":
                return WriteService.BackupPlugin(Str("plugin"));
            case "save_plugin":
                return WriteService.SavePlugin(Str("plugin"), root.TryGetProperty("path", out var pv) ? pv.GetString() : null);
            case "copy_as_override":
                return WriteService.CopyAsOverride(env, Str("source_plugin"), Str("id"), Str("patch_plugin"));
            case "delete_record":
                return WriteService.DeleteRecord(Str("plugin"), Str("id"), env);
            case "remove_list_item":
                return WriteService.RemoveListItem(Str("plugin"), Str("record"), Str("field"), Str("value"), env);
            case "set_components":
                return WriteService.SetComponents(Str("plugin"), Str("record"), Str("components"), env);
            case "set_conditions":
                return WriteService.SetConditions(Str("plugin"), Str("record"), Str("conditions"), env);
            case "revert_overrides":
                return WriteService.RevertOverridesFrom(env, Str("bad_plugin"), Str("patch_plugin"),
                    root.TryGetProperty("signature", out var sg) ? sg.GetString() : null,
                    root.TryGetProperty("contains_component", out var cc) ? cc.GetString() : null,
                    Bool("apply"), Int("limit", 50));
            case "batch_patch_records":
                return WriteService.BatchPatchRecords(
                    env, Str("patch_plugin"), Str("source_plugin"), Str("type"),
                    Str("operations"),
                    root.TryGetProperty("filter_field",  out var ff) ? ff.GetString() : null,
                    root.TryGetProperty("filter_value",  out var fv) ? fv.GetString() : null,
                    !root.TryGetProperty("dry_run", out var dr2) || dr2.ValueKind != JsonValueKind.False,
                    Int("limit", 5000));
            case "reload_plugin":
                return WriteService.ReloadPlugin(Str("plugin"), env);
            case "strip_masters_clean":
            {
                var mastersRaw = Str("masters");
                string[] masters;
                try
                {
                    using var mastersDoc = JsonDocument.Parse(string.IsNullOrWhiteSpace(mastersRaw) ? "[]" : mastersRaw);
                    masters = mastersDoc.RootElement.EnumerateArray()
                        .Select(e => e.GetString() ?? "").Where(s => s.Length > 0).ToArray();
                }
                catch { masters = Array.Empty<string>(); }
                bool isDryRun = !root.TryGetProperty("dry_run", out var dr) || dr.ValueKind != JsonValueKind.False;
                return WriteService.StripMastersClean(Str("source_path"), masters, Str("output_path"), isDryRun);
            }

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
        sb.AppendLine($"{matrix.EditorID} [{matrix.FormKey}] {matrix.Type}  |  record-level: {matrix.Level.ToUpperInvariant()}");

        if (matrix.Plugins.Count == 1)
        {
            sb.AppendLine($"Defined only in {matrix.Plugins[0]} -- no override conflict.");
            return sb.ToString();
        }

        sb.AppendLine($"Override chain ({matrix.Plugins.Count} plugins, load order -- LAST WINS):");
        for (int i = 0; i < matrix.Plugins.Count; i++)
            sb.AppendLine($"  {i + 1}. {matrix.Plugins[i]}"
                + (i == matrix.Plugins.Count - 1 ? "  <-- WINNER (what the game uses)" : ""));

        var diffRows = matrix.Rows.Where(r => r.Differs).ToList();
        sb.AppendLine($"\n{diffRows.Count} field(s) differ:");

        if (diffRows.Count > 0)
        {
            // Budget-bound the table so the executor's 8 KB hard cap never blind-truncates it
            // mid-row (which loses the footer and wastes the cut tokens). We stop emitting rows
            // when approaching the budget and report an honest omission count instead.
            const int FW = 34, VW = 20, Budget = 6500;
            var head = "  " + "Field".PadRight(FW);
            foreach (var p in matrix.Plugins) head += " | " + Trunc(p, VW).PadRight(VW);
            sb.AppendLine(head.TrimEnd());
            sb.AppendLine("  " + new string('-', Math.Min(FW + matrix.Plugins.Count * (VW + 3), 180)));

            int shown = 0;
            foreach (var row in diffRows)
            {
                if (sb.Length > Budget)
                {
                    sb.AppendLine($"  ... {diffRows.Count - shown} more differing field(s) omitted to save tokens. " +
                                  "Use get_winning_record for effective values, or get_record(plugin,id) for one plugin's full dump.");
                    break;
                }
                var sev = $"[{row.Severity.ToUpperInvariant()}]";
                var line = "  " + (sev + " " + row.Field).PadRight(FW);
                for (int ci = 0; ci < matrix.Plugins.Count; ci++)
                    line += " | " + Trunc(ci < row.Values.Count ? row.Values[ci] : "", VW).PadRight(VW);
                sb.AppendLine(line.TrimEnd());   // drop trailing column padding -- pure wasted tokens
                shown++;
            }
        }

        sb.AppendLine("\nUse get_winning_record(id) for the complete effective field dump.");
        return sb.ToString();
    }

    private static string Trunc(string? s, int max) =>
        string.IsNullOrEmpty(s) ? "" : s.Length <= max ? s : s[..(max - 3)] + "...";

    private static string ScanConflicts(object? env, bool modOnly, string? typeFilter, int limit)
    {
        if (env == null) return "No environment loaded. Use 'Load Env' or 'Open MO2' first.";

        var all = ConflictScanner.ScanCached(env);
        var filtered = all.Where(c =>
            (!modOnly || c.InvolvesMod) &&
            (string.IsNullOrWhiteSpace(typeFilter) || string.Equals(c.Type, typeFilter, StringComparison.OrdinalIgnoreCase)))
            .ToList();

        if (filtered.Count == 0)
            return "No conflicts found" + (modOnly ? " involving mods" : "") +
                   (string.IsNullOrWhiteSpace(typeFilter) ? "." : $" of type {typeFilter}.");

        var sb = new System.Text.StringBuilder();
        sb.AppendLine($"{filtered.Count} conflicting record(s){(modOnly ? " involving mods" : "")}" +
                      $"{(string.IsNullOrWhiteSpace(typeFilter) ? "" : $" of type {typeFilter}")}; showing {Math.Min(limit, filtered.Count)}:");
        foreach (var c in filtered.Take(limit))
            sb.AppendLine($"  {c.EditorID} [{c.FormKey}] ({c.Type}) — {c.Plugins.Count} plugins, WINNER={c.Winner}");
        if (filtered.Count > limit)
            sb.AppendLine($"…and {filtered.Count - limit} more. Filter by 'type' or raise 'limit' to see them.");
        return sb.ToString();
    }

    private static string GetReferencedByText(object? env, string id, int limit)
    {
        var fk = ResolveToFk(env, id);
        if (fk.IsNull) return $"Could not resolve '{id}'. Pass a FormKey or an exact EditorID.";

        var refs = MutagenLoader.GetReferencedBy(env, fk.ToString(), limit);
        if (refs.Count == 0) return $"Nothing references {fk}.";

        var sb = new System.Text.StringBuilder();
        sb.AppendLine($"{refs.Count} record(s) reference {fk}:");
        foreach (var r in refs)
            sb.AppendLine($"  {(string.IsNullOrEmpty(r.EditorID) ? r.FormKey : r.EditorID)} [{r.FormKey}] ({r.Type}) in {r.Plugin}");
        return sb.ToString();
    }

    private static string GetProblemsText(object? env, string id)
    {
        var fk = ResolveToFk(env, id);
        if (fk.IsNull) return $"Could not resolve '{id}'. Pass a FormKey or an exact EditorID.";

        var problems = MutagenLoader.GetRecordProblems(env, fk.ToString());
        if (problems.Count == 0) return $"No problems found for {fk}.";
        return $"{problems.Count} problem(s) for {fk}:\n" +
               string.Join("\n", problems.Select(p => $"  [{p.Severity}] {p.Description}"));
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
