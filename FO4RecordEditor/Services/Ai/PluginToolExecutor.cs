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

    /// <summary>The tool schemas advertised to the model (Anthropic tool format).</summary>
    public static object[] ToolDefinitions() =>
    [
        new
        {
            name = "list_plugins",
            description = "List every loaded plugin (ESP/ESM/ESL) the user has open. Call this first to discover what data is available.",
            input_schema = new { type = "object", properties = new { }, required = Array.Empty<string>() }
        },
        new
        {
            name = "list_record_types",
            description = "List the record-type signatures (e.g. WEAP, ARMO, COBJ, NPC_, LVLI) present in a given plugin.",
            input_schema = new
            {
                type = "object",
                properties = new { plugin = new { type = "string", description = "Plugin file name, e.g. 'MyMod.esp'." } },
                required = new[] { "plugin" }
            }
        },
        new
        {
            name = "list_records",
            description = "List records of a specific type in a plugin (EditorID + FormKey). Use to browse a group.",
            input_schema = new
            {
                type = "object",
                properties = new
                {
                    plugin = new { type = "string" },
                    type = new { type = "string", description = "Record signature, e.g. 'WEAP'." },
                    limit = new { type = "integer", description = "Max records to return (default 100)." }
                },
                required = new[] { "plugin", "type" }
            }
        },
        new
        {
            name = "search_records",
            description = "Search a plugin for records whose EditorID or FormKey contains the query text. Returns EditorID, type and FormKey.",
            input_schema = new
            {
                type = "object",
                properties = new
                {
                    plugin = new { type = "string" },
                    query = new { type = "string" },
                    limit = new { type = "integer", description = "Max matches (default 50)." }
                },
                required = new[] { "plugin", "query" }
            }
        },
        new
        {
            name = "get_record",
            description = "Get the full field dump of a single record by FormKey ('001234:Plugin.esp') or EditorID, within a plugin.",
            input_schema = new
            {
                type = "object",
                properties = new
                {
                    plugin = new { type = "string" },
                    id = new { type = "string", description = "FormKey or EditorID of the record." }
                },
                required = new[] { "plugin", "id" }
            }
        },
    ];

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
            default:
                return $"Unknown tool: {toolName}";
        }
    }
}
