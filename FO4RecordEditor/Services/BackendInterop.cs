using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;
using FO4RecordEditor.Models;
using FO4RecordEditor.ViewModels;
using Newtonsoft.Json;

namespace FO4RecordEditor.Services;

/// <summary>
/// Bridges the React frontend (via WebView2 host objects) to the live backend. Reads the env from
/// the shell on each call so it always reflects the currently loaded modlist (not a stale snapshot).
/// Clean AutoDual class with NO explicit COM interface -- the interface + AutoDual together produce
/// two dispatch surfaces and trip "Invalid number of parameters" (0x8002000E) from JS.
/// </summary>
[ClassInterface(ClassInterfaceType.AutoDual)]
[ComVisible(true)]
public class BackendInterop
{
    private readonly ShellViewModel? _shell;
    private readonly IEnumerable<ConflictEntry>? _staticConflicts;
    private readonly object? _staticEnv;

    /// <summary>Live mode (main window): reads the env + scans conflicts from the shell on demand.</summary>
    public BackendInterop(ShellViewModel shell) => _shell = shell;

    /// <summary>Snapshot mode (the standalone conflict window): a pre-computed conflict list + env.</summary>
    public BackendInterop(IEnumerable<ConflictEntry> conflicts, object? env)
    {
        _staticConflicts = conflicts;
        _staticEnv = env;
    }

    private object? Env => _shell?.GameEnvironment ?? _staticEnv;

    public System.Threading.Tasks.Task<string> GetConflicts() =>
        DebugLog.GuardAsync(nameof(GetConflicts), async () =>
        {
            if (_staticConflicts != null) return JsonConvert.SerializeObject(_staticConflicts);
            if (Env == null) return "[]";
            // Scan the loaded load order for conflicts on demand. Host-object methods are invoked on the
            // UI thread, so awaiting (rather than .GetResult()) keeps the window responsive while a large
            // modlist scans on a background thread. WebView2 marshals the Task result back to the JS await.
            var conflicts = await ConflictScanner.ScanAsync(Env);
            return JsonConvert.SerializeObject(conflicts);
        });

    public string GetConflictMatrix(string formKey) =>
        DebugLog.Guard(nameof(GetConflictMatrix), () =>
            JsonConvert.SerializeObject(MutagenLoader.BuildConflictMatrix(Env, formKey)), formKey);

    /// <summary>Records that reference this FormKey (xEdit "Referenced By"). Scans the load order off
    /// the UI thread so a big modlist doesn't freeze the window.</summary>
    public System.Threading.Tasks.Task<string> GetReferencedBy(string formKey) =>
        DebugLog.GuardAsync(nameof(GetReferencedBy), async () =>
        {
            if (Env == null) return "[]";
            var list = await System.Threading.Tasks.Task.Run(() => MutagenLoader.GetReferencedBy(Env, formKey));
            return JsonConvert.SerializeObject(list);
        }, formKey);

    /// <summary>Per-record problems (deleted flag, dangling references) for the winning version.</summary>
    public string GetProblems(string formKey) =>
        DebugLog.Guard(nameof(GetProblems), () =>
            Env == null ? "[]" : JsonConvert.SerializeObject(MutagenLoader.GetRecordProblems(Env, formKey)), formKey);

    /// <summary>Record picker search across the load order (EditorID / FormID), optional type filter.</summary>
    public System.Threading.Tasks.Task<string> SearchRecords(string query, string typeFilter) =>
        DebugLog.GuardAsync(nameof(SearchRecords), async () =>
        {
            if (Env == null) return "[]";
            var hits = await System.Threading.Tasks.Task.Run(() =>
                MutagenLoader.SearchAllRecords(Env, query, string.IsNullOrWhiteSpace(typeFilter) ? null : typeFilter));
            return JsonConvert.SerializeObject(hits);
        }, $"{query} [{typeFilter}]");

    /// <summary>The field tree for one record, as JSON the React editor can render directly
    /// (friendly label, value, EditKind/EnumOptions for dropdowns/checkboxes, summary + children).</summary>
    public string GetRecordTree(string plugin, string id) =>
        DebugLog.Guard(nameof(GetRecordTree), () =>
            JsonConvert.SerializeObject(MutagenLoader.BuildPopulatedFields(id, Env, plugin).Select(NodeDto).ToArray()),
            $"{plugin} {id}");

    // Map a RecordNode to a cycle-free DTO (the node's Parent back-reference would otherwise loop).
    private static object NodeDto(Models.RecordNode n) => new
    {
        key = n.Key,
        label = Rendering.FriendlyNames.Label(n.Key),
        value = n.Value,
        values = n.Values,
        editKind = n.EditKind.ToString(),
        enumOptions = n.EnumOptions,
        isSummary = n.IsSummary,
        hasChildren = n.Children.Count > 0,
        children = n.Children.Select(NodeDto).ToArray(),
    };

    // ---- edit ---- (every result is logged so we can see what the UI changed and why a save failed)
    public string OpenPlugin(string plugin) =>
        DebugLog.Guard(nameof(OpenPlugin), () => WriteService.OpenPlugin(plugin, Env), plugin);
    public string CreatePlugin(string name) =>
        DebugLog.Guard(nameof(CreatePlugin), () => WriteService.CreatePlugin(name), name);
    public string SetField(string plugin, string record, string field, string value) =>
        DebugLog.Guard(nameof(SetField), () => WriteService.SetField(plugin, record, field, value, Env),
            $"{plugin} {record} {field}={value}");
    public string SetComponents(string plugin, string record, string componentsJson) =>
        DebugLog.Guard(nameof(SetComponents), () => WriteService.SetComponents(plugin, record, componentsJson, Env),
            $"{plugin} {record}");
    public string SetConditions(string plugin, string record, string conditionsJson) =>
        DebugLog.Guard(nameof(SetConditions), () => WriteService.SetConditions(plugin, record, conditionsJson, Env),
            $"{plugin} {record}");
    public string AddListItem(string plugin, string record, string field, string value) =>
        DebugLog.Guard(nameof(AddListItem), () => WriteService.AddListItem(plugin, record, field, value, Env),
            $"{plugin} {record} {field}+={value}");
    public string RemoveListItem(string plugin, string record, string field, string value) =>
        DebugLog.Guard(nameof(RemoveListItem), () => WriteService.RemoveListItem(plugin, record, field, value, Env),
            $"{plugin} {record} {field}-={value}");
    public string DeleteRecord(string plugin, string id) =>
        DebugLog.Guard(nameof(DeleteRecord), () => WriteService.DeleteRecord(plugin, id, Env), $"{plugin} {id}");
    public string CopyAsOverride(string sourcePlugin, string id, string patchPlugin) =>
        DebugLog.Guard(nameof(CopyAsOverride), () => WriteService.CopyAsOverride(Env, sourcePlugin, id, patchPlugin),
            $"{id} {sourcePlugin}->{patchPlugin}");
    public string RevertOverrides(string badPlugin, string patchPlugin, string signature, string containsComponent, bool apply, int limit) =>
        DebugLog.Guard(nameof(RevertOverrides), () => WriteService.RevertOverridesFrom(Env, badPlugin, patchPlugin,
            string.IsNullOrWhiteSpace(signature) ? null : signature,
            string.IsNullOrWhiteSpace(containsComponent) ? null : containsComponent,
            apply, limit <= 0 ? 50 : limit), $"{badPlugin}->{patchPlugin} apply={apply}");
    public string CompactToEsl(string plugin) =>
        DebugLog.Guard(nameof(CompactToEsl), () => WriteService.CompactToEsl(plugin, Env), plugin);
    public string CleanPlugin(string plugin) =>
        DebugLog.Guard(nameof(CleanPlugin), () => WriteService.CleanPlugin(plugin, Env), plugin);
    public string RenumberFormId(string plugin, string record, string newId) =>
        DebugLog.Guard(nameof(RenumberFormId), () => WriteService.RenumberFormId(plugin, record, newId, Env),
            $"{plugin} {record}->{newId}");

    // ---- save ----
    public string ResolveConflict(string formKey, string winner, string patch) =>
        DebugLog.Guard(nameof(ResolveConflict), () => WriteService.ResolveConflict(Env, formKey, winner, patch),
            $"{formKey} winner={winner} patch={patch}");
    public string SavePatch(string patch) =>
        DebugLog.Guard(nameof(SavePatch), () => WriteService.SavePlugin(patch, null), patch);
    public string SavePlugin(string plugin, string path) =>
        DebugLog.Guard(nameof(SavePlugin), () =>
            WriteService.SavePlugin(plugin, string.IsNullOrWhiteSpace(path) ? null : path), $"{plugin} {path}");

    // Return JSON (not string[]): a raw array comes back over the WebView2 bridge as a remote proxy,
    // which the JS caller can't reliably spread/iterate. Every other method returns JSON for this reason.
    public string GetEditablePlugins() =>
        DebugLog.Guard(nameof(GetEditablePlugins), () => JsonConvert.SerializeObject(WriteService.EditablePlugins().ToArray()));
}
