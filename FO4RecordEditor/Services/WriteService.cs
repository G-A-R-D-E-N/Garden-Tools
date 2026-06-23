using System.IO;
using System.Reflection;
using System.Text.Json;
using Mutagen.Bethesda;
using Mutagen.Bethesda.Fallout4;
using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Plugins.Binary.Parameters;
using Mutagen.Bethesda.Plugins.Records;
using Mutagen.Bethesda.Strings;

namespace FO4RecordEditor.Services;

/// <summary>
/// Lets the AI author plugins: create a new mutable mod, add records, set scalar fields,
/// and write the ESP. Mutable mods are mirrored into MutagenLoader.LooseMods so the read
/// tools (list/search/get) immediately see what was created. Changes live in memory until
/// save_plugin is called.
/// </summary>
public static class WriteService
{
    private static readonly System.Globalization.CultureInfo Inv = System.Globalization.CultureInfo.InvariantCulture;

    private static readonly Dictionary<string, IFallout4Mod> _mutable = new(StringComparer.OrdinalIgnoreCase);
    // Original on-disk path of an opened plugin, so save_plugin can overwrite it in place.
    private static readonly Dictionary<string, string> _sourcePath = new(StringComparer.OrdinalIgnoreCase);

    /// <summary>User-configured output folder (from Settings). Blank => &lt;app&gt;/Output.</summary>
    public static string? OutputFolderOverride { get; set; }

    public static string DefaultOutputDir =>
        !string.IsNullOrWhiteSpace(OutputFolderOverride)
            ? OutputFolderOverride!
            : Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "Output");

    /// <summary>The loaded MO2 instance's overwrite folder (&lt;instance&gt;\overwrite). When set, NEW
    /// patches save here so MO2 loads them automatically at top priority — no manual mod install.</summary>
    public static string? Mo2OverwriteFolder { get; set; }

    /// <summary>Where a brand-new patch (no source file) saves by default: the MO2 overwrite folder
    /// when a modlist is loaded, otherwise the configured Output folder.</summary>
    public static string NewPatchDir =>
        !string.IsNullOrWhiteSpace(Mo2OverwriteFolder) ? Mo2OverwriteFolder! : DefaultOutputDir;

    public static IFallout4Mod? GetMutable(string name) =>
        _mutable.TryGetValue(name, out var m) ? m : null;

    private static void Register(string name, IFallout4Mod mod)
    {
        _mutable[name] = mod;
        MutagenLoader.EditableMods[name] = mod;  // prioritized for ALL reads (hot loading)
        MutagenLoader.LooseMods[name] = mod;      // so list_plugins / the tree still see it
        MutagenLoader.InvalidateModIndex(name);
    }

    // Accept a bare file name ("Patch.esp") OR a full path ("D:\mods\X\Patch.esp"). Returns the file
    // name used as the registry key / ModKey, plus the explicit on-disk path when one was supplied.
    private static (string name, string? explicitPath) NormalizePlugin(string plugin)
    {
        if (string.IsNullOrWhiteSpace(plugin)) return (plugin, null);
        if ((plugin.Contains('\\') || plugin.Contains('/')) && Path.IsPathRooted(plugin))
            return (Path.GetFileName(plugin), plugin);
        return (plugin, null);
    }

    public static string OpenPlugin(string plugin, object? env)
    {
        if (string.IsNullOrWhiteSpace(plugin)) return "Provide a plugin name.";
        var (name, explicitPath) = NormalizePlugin(plugin);
        if (_mutable.ContainsKey(name)) return $"'{name}' is already open for editing.";

        string? filePath = explicitPath ?? FindPluginPath(name, env);
        if (string.IsNullOrEmpty(filePath) || !File.Exists(filePath))
            return $"Could not locate '{name}' on disk. Pass a full path (e.g. 'D:\\...\\{name}'), load the game environment, or open the file via File > Open.";

        try
        {
            var mod = Fallout4Mod.CreateFromBinary(ModPath.FromPath(filePath), Fallout4Release.Fallout4);
            Register(name, mod);
            _sourcePath[name] = filePath;     // save_plugin overwrites the original by default
            NotifyChanged(name);
            var count = mod.EnumerateMajorRecords().Count();
            return $"Opened '{name}' for editing ({count} records). Use create_record, set_field, and save_plugin.";
        }
        catch (Exception ex)
        {
            return $"Failed to open '{name}' for editing: {ex.Message}";
        }
    }

    private static string? FindPluginPath(string plugin, object? env)
    {
        // 1. Loose mod opened via file picker
        if (MutagenLoader.LooseModPaths.TryGetValue(plugin, out var loosePath))
            return loosePath;

        // 2. Game environment load order
        if (env != null)
        {
            try
            {
                dynamic dynEnv = env;
                foreach (dynamic l in (System.Collections.IEnumerable)dynEnv.LoadOrder.ListedOrder)
                {
                    if (!string.Equals((string)l.ModKey.FileName.String, plugin, StringComparison.OrdinalIgnoreCase))
                        continue;
                    // Try direct Path property first
                    try { var p = (string?)l.Path?.Path; if (p != null) return p; } catch { }
                    // Fall back to DataFolderPath + filename
                    try
                    {
                        var dataDir = (string)dynEnv.DataFolderPath.Path;
                        return Path.Combine(dataDir, plugin);
                    }
                    catch { }
                    break;
                }
            }
            catch { }
        }

        return null;
    }

    public static string CreatePlugin(string name)
    {
        if (string.IsNullOrWhiteSpace(name)) return "Provide a plugin name, e.g. 'MyMod.esp'.";
        // A full path is allowed -- use its file name as the ModKey and remember the path to save to.
        var (fileName, explicitPath) = NormalizePlugin(name);
        if (!fileName.EndsWith(".esp", StringComparison.OrdinalIgnoreCase) &&
            !fileName.EndsWith(".esm", StringComparison.OrdinalIgnoreCase) &&
            !fileName.EndsWith(".esl", StringComparison.OrdinalIgnoreCase))
            fileName += ".esp";

        var mod = new Fallout4Mod(ModKey.FromNameAndExtension(fileName), Fallout4Release.Fallout4);
        Register(fileName, mod);
        if (explicitPath != null) _sourcePath[fileName] = explicitPath;   // save_plugin writes here
        NotifyChanged(fileName);
        return $"Created new plugin '{fileName}'. Add records with create_record, then call save_plugin to write it.";
    }

    /// <summary>Raised (with the plugin file name) when a plugin is created/opened/modified,
    /// so the UI can show or refresh it in the Explorer tree.</summary>
    public static event Action<string>? PluginChanged;
    private static void NotifyChanged(string name) => PluginChanged?.Invoke(name);

    public static string CreateRecord(string plugin, string sig, string editorId, object? env)
    {
        var mod = EnsureOpen(plugin, env, out var openMsg);
        if (mod == null) return openMsg;
        if (string.IsNullOrWhiteSpace(editorId)) return "Provide an editorId for the new record.";

        var fk = NextFreeFormKey(mod);
        if (fk == null)
            return $"'{plugin}' is a light plugin (ESL) and its FormID range (0x800-0xFFF) is full. " +
                   $"Save it as a .esp (FormID restriction lifted) to add more records.";

        var rec = AddNewBySig(mod, sig, editorId, fk.Value);
        if (rec == null)
            return $"Record type '{sig}' is not supported for creation yet. Supported: " + SupportedTypes;

        MutagenLoader.InvalidateModIndex(plugin);
        NotifyChanged(plugin);
        return $"Created {sig} '{editorId}' [{rec.FormKey}] in {plugin}. Set fields with set_field, then save_plugin.";
    }

    public static string SetField(string plugin, string recordId, string field, string value, object? env)
    {
        var mod = EnsureOpen(plugin, env, out var openMsg);
        if (mod == null) return openMsg;

        var rec = FindMutableRecord(mod, recordId);
        if (rec == null) return $"Record '{recordId}' not found in {plugin}.";

        if (TrySet(rec, field, value, env, out var msg))
        {
            MutagenLoader.InvalidateModIndex(plugin);
            NotifyChanged(plugin);
            return $"Set {field} = '{value}' on {recordId} in {plugin}.";
        }
        return msg;
    }

    // Return the plugin as a mutable mod, auto-opening an already-loaded (read-only) plugin
    // from disk if needed. Created plugins are already mutable. This lets the AI edit an
    // existing plugin without a separate open_plugin step.
    private static IFallout4Mod? EnsureOpen(string plugin, object? env, out string msg)
    {
        msg = "";
        var (name, _) = NormalizePlugin(plugin);
        var mod = GetMutable(name);
        if (mod != null) return mod;

        var openResult = OpenPlugin(plugin, env);   // normalizes + loads mutable, or explains why not
        mod = GetMutable(name);
        if (mod == null) msg = openResult;
        return mod;
    }

    public static string AddListItem(string plugin, string recordId, string field, string value, object? env)
    {
        var mod = EnsureOpen(plugin, env, out var openMsg);
        if (mod == null) return openMsg;

        var rec = FindMutableRecord(mod, recordId);
        if (rec == null) return $"Record '{recordId}' not found in {plugin}.";

        var prop = rec.GetType().GetProperty(field,
            BindingFlags.Public | BindingFlags.Instance | BindingFlags.IgnoreCase);
        if (prop == null) return $"No field '{field}' on {rec.GetType().Name}.";

        // The list may be null on a freshly created record -- initialise it.
        var listObj = prop.CanRead ? prop.GetValue(rec) : null;
        if (listObj == null)
        {
            if (!prop.CanWrite) return $"Field '{field}' is an uninitialised list and can't be created.";
            var listType = Nullable.GetUnderlyingType(prop.PropertyType) ?? prop.PropertyType;
            try { listObj = System.Activator.CreateInstance(listType); } catch { listObj = null; }
            if (listObj == null) return $"Could not initialise list '{field}'.";
            prop.SetValue(rec, listObj);
        }
        if (listObj is not System.Collections.IList list)
            return $"Field '{field}' is not a list.";

        // Find the FormLink target type of the list elements (e.g. IKeywordGetter).
        var elemType = prop.PropertyType.IsGenericType ? prop.PropertyType.GetGenericArguments().FirstOrDefault() : null;
        Type? linkTarget = null;
        if (elemType != null && elemType.IsGenericType)
        {
            var gtd = elemType.GetGenericTypeDefinition();
            if (gtd == typeof(IFormLink<>) || gtd == typeof(IFormLinkGetter<>) ||
                gtd == typeof(IFormLinkNullable<>) || gtd == typeof(IFormLinkNullableGetter<>))
                linkTarget = elemType.GetGenericArguments()[0];
        }
        if (linkTarget == null)
            return $"Field '{field}' is a list of {elemType?.Name ?? "?"}; add_list_item supports FormLink lists " +
                   $"(e.g. Keywords, FormList Items) for now.";

        if (!FormKey.TryFactory(value, out var fk))
        {
            fk = MutagenLoader.ResolveEditorIdToFormKey(env, value);
            if (fk.IsNull) return $"'{value}' is not a FormKey and no loaded record has that EditorID.";
        }

        try
        {
            var link = System.Activator.CreateInstance(typeof(FormLink<>).MakeGenericType(linkTarget), fk);
            list.Add(link);
            MutagenLoader.InvalidateModIndex(plugin);
            NotifyChanged(plugin);
            return $"Added {value} to {field} on {recordId} in {plugin} (now {list.Count} item(s)).";
        }
        catch (Exception ex)
        {
            return $"Could not add to {field}: {ex.Message}";
        }
    }

    // ---- FormID compaction to ESL range -----------------------------------------------

    public static string CompactToEsl(string plugin, object? env)
    {
        var mod = EnsureOpen(plugin, env, out var msg); if (mod == null) return msg;

        // Native records whose object IDs fall outside the ESL range 0x800-0xFFF.
        var native = mod.EnumerateMajorRecords().Where(r => r.FormKey.ModKey == mod.ModKey).ToList();
        var inRange = new HashSet<uint>(native.Select(r => r.FormKey.ID).Where(id => id is >= 0x800 and <= 0xFFF));

        var remap = new Dictionary<FormKey, FormKey>();
        uint next = 0x800;
        foreach (var r in native.Where(r => r.FormKey.ID > 0xFFF).OrderBy(r => r.FormKey.ID))
        {
            while (inRange.Contains(next)) next++;
            if (next > 0xFFF)
                return $"Cannot compact '{plugin}': it has more than {0xFFF - 0x800 + 1} records, which won't fit the ESL range.";
            remap[r.FormKey] = new FormKey(mod.ModKey, next);
            inRange.Add(next);
            next++;
        }

        if (remap.Count == 0)
            return $"'{plugin}' is already ESL-compatible (all native FormIDs within 0x800-0xFFF).";

        // Re-key each out-of-range record (duplicate into its group with the new FormKey,
        // then remove the old), iterating the mod's top-level groups generically.
        foreach (var groupProp in mod.GetType().GetProperties()
                     .Where(p => typeof(Mutagen.Bethesda.Plugins.Records.IGroup).IsAssignableFrom(p.PropertyType)))
        {
            if (groupProp.GetValue(mod) is not Mutagen.Bethesda.Plugins.Records.IGroup group) continue;
            var toRekey = ((System.Collections.IEnumerable)group).Cast<Mutagen.Bethesda.Plugins.Records.IMajorRecordGetter>()
                .Where(r => remap.ContainsKey(r.FormKey)).ToList();
            var removeM = group.GetType().GetMethod("Remove", new[] { typeof(FormKey) });
            foreach (var rg in toRekey)
            {
                group.DuplicateInAsNewUntypedRecord((Mutagen.Bethesda.Plugins.Records.IMajorRecord)rg, remap[rg.FormKey]);
                removeM?.Invoke(group, new object[] { rg.FormKey });
            }
        }

        // Repoint every reference in the plugin to the new FormKeys.
        mod.RemapLinks(remap);

        MutagenLoader.InvalidateModIndex(plugin); NotifyChanged(plugin);
        return $"Compacted {plugin}: remapped {remap.Count} record(s) into the ESL range (0x800-0xFFF) and fixed " +
               $"all references. Now save_plugin to a path ending in .esl (and the plugin can be a light master). " +
               $"This is in memory until you save.";
    }

    /// <summary>
    /// Change one record's FormID within its plugin (xEdit "Change FormID") and repoint every
    /// reference to it that lives in the SAME plugin. References in OTHER plugins are not rewritten
    /// (that needs a patch); the message notes they may exist so the caller / AI can fix them.
    /// In memory until save_plugin. newIdHex is the 6-digit object id, e.g. "000F99" or "0xF99".
    /// </summary>
    public static string RenumberFormId(string plugin, string recordId, string newIdHex, object? env)
    {
        var mod = EnsureOpen(plugin, env, out var msg); if (mod == null) return msg;
        var rec = FindMutableRecord(mod, recordId);
        if (rec == null) return $"Record '{recordId}' not found in {plugin}.";

        var hex = (newIdHex ?? "").Trim();
        if (hex.StartsWith("0x", StringComparison.OrdinalIgnoreCase)) hex = hex[2..];
        if (!uint.TryParse(hex, System.Globalization.NumberStyles.HexNumber, Inv, out var newId))
            return $"'{newIdHex}' is not a valid hex FormID (use the 6-digit object id, e.g. '000F99').";
        if (newId == 0) return "FormID 000000 is the null id and can't be assigned.";

        var oldFk = rec.FormKey;
        var newFk = new FormKey(mod.ModKey, newId);
        if (newFk == oldFk) return "The new FormID is the same as the current one.";
        if (mod.EnumerateMajorRecords().Any(r => r.FormKey == newFk))
            return $"FormID {newId:X6} is already used by another record in {plugin}. Pick a free id.";

        // Re-key: duplicate the record into its group under the new FormKey, then drop the old one.
        bool rekeyed = false;
        foreach (var groupProp in mod.GetType().GetProperties()
                     .Where(p => typeof(Mutagen.Bethesda.Plugins.Records.IGroup).IsAssignableFrom(p.PropertyType)))
        {
            if (groupProp.GetValue(mod) is not Mutagen.Bethesda.Plugins.Records.IGroup group) continue;
            var match = ((System.Collections.IEnumerable)group)
                .Cast<Mutagen.Bethesda.Plugins.Records.IMajorRecordGetter>()
                .FirstOrDefault(r => r.FormKey == oldFk);
            if (match == null) continue;
            group.DuplicateInAsNewUntypedRecord((Mutagen.Bethesda.Plugins.Records.IMajorRecord)match, newFk);
            group.GetType().GetMethod("Remove", new[] { typeof(FormKey) })?.Invoke(group, new object[] { oldFk });
            rekeyed = true;
            break;
        }
        if (!rekeyed) return $"Could not locate the group holding {oldFk} in {plugin}.";

        // Repoint every in-plugin reference to the new id.
        mod.RemapLinks(new Dictionary<FormKey, FormKey> { [oldFk] = newFk });
        MutagenLoader.InvalidateModIndex(plugin);
        NotifyChanged(plugin);

        return $"Renumbered {rec.EditorID} from {oldFk} to {newFk} in {plugin} and fixed in-plugin references. " +
               $"NOTE: references to {oldFk} in OTHER plugins are NOT updated — check 'Referenced By' (or ask the " +
               $"AI to find and patch them). save_plugin to persist.";
    }

    // ---- cleaning (xEdit-style UDR) ----------------------------------------------------

    // FO4 "Initially Disabled" major-record flag.
    private const int InitiallyDisabledFlag = 0x0000_0800;

    public static string CleanPlugin(string plugin, object? env)
    {
        var mod = EnsureOpen(plugin, env, out var msg); if (mod == null) return msg;

        int undeleted = 0, skipped = 0;
        foreach (var recG in mod.EnumerateMajorRecords().ToList())
        {
            if (!recG.IsDeleted) continue;
            if (recG is IFallout4MajorRecord rec)
            {
                // Undelete and Disable Reference: clear the deleted flag, mark initially
                // disabled so the engine treats it as a hidden ref instead of a hard delete.
                rec.IsDeleted = false;
                try { rec.MajorRecordFlagsRaw |= InitiallyDisabledFlag; } catch { }
                undeleted++;
            }
            else skipped++;
        }

        if (undeleted > 0) { MutagenLoader.InvalidateModIndex(plugin); NotifyChanged(plugin); }
        return $"Cleaned {plugin}: undeleted + disabled {undeleted} record(s)" +
               (skipped > 0 ? $"; {skipped} deleted record(s) could not be processed" : "") +
               ". Call save_plugin to persist.";
    }

    // ---- VMAD scripts (e.g. wire a holotape to its terminal program) -------------------

    public static string AttachScript(string plugin, string recordId, string scriptName, object? env)
    {
        var mod = EnsureOpen(plugin, env, out var msg); if (mod == null) return msg;
        var rec = FindMutableRecord(mod, recordId); if (rec == null) return $"Record '{recordId}' not found in {plugin}.";

        var vmad = GetOrCreateVmad(rec, out var vmadMsg); if (vmad == null) return vmadMsg;
        var scripts = (System.Collections.IList)vmad.GetType().GetProperty("Scripts")!.GetValue(vmad)!;
        foreach (var s in scripts)
            if (string.Equals((string?)s!.GetType().GetProperty("Name")!.GetValue(s), scriptName, StringComparison.OrdinalIgnoreCase))
                return $"Script '{scriptName}' is already attached to {recordId}.";

        scripts.Add(new ScriptEntry { Name = scriptName });
        MutagenLoader.InvalidateModIndex(plugin); NotifyChanged(plugin);
        return $"Attached script '{scriptName}' to {recordId} in {plugin}. Configure it with set_script_property.";
    }

    public static string SetScriptProperty(string plugin, string recordId, string scriptName,
        string propName, string value, string? type, object? env)
    {
        var mod = EnsureOpen(plugin, env, out var msg); if (mod == null) return msg;
        var rec = FindMutableRecord(mod, recordId); if (rec == null) return $"Record '{recordId}' not found in {plugin}.";

        var vmad = rec.GetType().GetProperty("VirtualMachineAdapter")?.GetValue(rec);
        if (vmad == null) return $"{recordId} has no scripts. Call attach_script first.";
        var scripts = (System.Collections.IList)vmad.GetType().GetProperty("Scripts")!.GetValue(vmad)!;

        object? script = null;
        foreach (var s in scripts)
            if (string.Equals((string?)s!.GetType().GetProperty("Name")!.GetValue(s), scriptName, StringComparison.OrdinalIgnoreCase))
            { script = s; break; }
        if (script == null) return $"Script '{scriptName}' is not attached to {recordId}. Call attach_script first.";

        var props = (System.Collections.IList)script.GetType().GetProperty("Properties")!.GetValue(script)!;
        for (int i = props.Count - 1; i >= 0; i--)
            if (string.Equals((string?)props[i]!.GetType().GetProperty("Name")!.GetValue(props[i]), propName, StringComparison.OrdinalIgnoreCase))
                props.RemoveAt(i);

        var kind = (type ?? InferScriptKind(value, env)).ToLowerInvariant();
        object scriptProp;
        try
        {
            switch (kind)
            {
                case "object": case "form": case "formlink":
                {
                    if (!FormKey.TryFactory(value, out var fk))
                    {
                        fk = MutagenLoader.ResolveEditorIdToFormKey(env, value);
                        if (fk.IsNull) return $"'{value}' is not a FormKey and no loaded record has that EditorID.";
                    }
                    var op = new ScriptObjectProperty { Name = propName };
                    var link = op.GetType().GetProperty("Object")!.GetValue(op)!;
                    link.GetType().GetMethod("SetTo", new[] { typeof(FormKey) })!.Invoke(link, new object[] { fk });
                    scriptProp = op;
                    break;
                }
                case "int":    scriptProp = new ScriptIntProperty    { Name = propName, Data = int.Parse(value, Inv) };   break;
                case "float":  scriptProp = new ScriptFloatProperty  { Name = propName, Data = float.Parse(value, Inv) }; break;
                case "bool":   scriptProp = new ScriptBoolProperty   { Name = propName, Data = bool.Parse(value) };  break;
                default:       scriptProp = new ScriptStringProperty { Name = propName, Data = value };              break;
            }
        }
        catch (Exception ex) { return $"Could not set script property: {ex.Message}"; }

        props.Add(scriptProp);
        MutagenLoader.InvalidateModIndex(plugin); NotifyChanged(plugin);
        return $"Set script property '{propName}' = '{value}' ({kind}) on script '{scriptName}' of {recordId}.";
    }

    private static object? GetOrCreateVmad(object rec, out string msg)
    {
        msg = "";
        var prop = rec.GetType().GetProperty("VirtualMachineAdapter");
        if (prop == null) { msg = $"{rec.GetType().Name} does not support scripts."; return null; }
        var vmad = prop.GetValue(rec);
        if (vmad == null)
        {
            var vt = Nullable.GetUnderlyingType(prop.PropertyType) ?? prop.PropertyType;
            try { vmad = System.Activator.CreateInstance(vt); } catch { vmad = null; }
            if (vmad == null) { msg = "Could not initialise the script adapter."; return null; }
            vt.GetProperty("Version")?.SetValue(vmad, (short)6);    // FO4 VMAD version/format
            vt.GetProperty("ObjectFormat")?.SetValue(vmad, (ushort)2);
            prop.SetValue(rec, vmad);
        }
        return vmad;
    }

    private static string InferScriptKind(string value, object? env)
    {
        if (FormKey.TryFactory(value, out _)) return "object";
        if (!MutagenLoader.ResolveEditorIdToFormKey(env, value).IsNull) return "object";
        if (bool.TryParse(value, out _)) return "bool";
        if (int.TryParse(value, out _)) return "int";
        if (float.TryParse(value, out _)) return "float";
        return "string";
    }

    public static string SavePlugin(string plugin, string? path)
    {
        var (name, explicitPath) = NormalizePlugin(plugin);
        var mod = GetMutable(name);
        if (mod == null) return $"Plugin '{name}' is not open for editing.";

        // Default: overwrite the original file the plugin was opened from (modify in place).
        // Newly created plugins (no source) fall back to the configured Output folder.
        // New patches (no source file) save into the MO2 overwrite folder when a modlist is loaded, so
        // MO2 picks them up automatically; plugins opened from disk overwrite their original file.
        if (string.IsNullOrWhiteSpace(path))
            path = explicitPath ?? (_sourcePath.TryGetValue(name, out var sp) ? sp : Path.Combine(NewPatchDir, name));

        var prms = new BinaryWriteParameters
        {
            // Build the master list from the records' FormLinks (self-contained, no load order needed).
            MastersListContent = MastersListContentOption.Iterate,
            // We write to a temp file whose name (Plugin.esp.xxxx.tmp) is not a valid ModKey; the mod
            // already carries the correct ModKey, so skip Mutagen's filename<->ModKey check.
            ModKey = ModKeyOption.NoCheck,
        };
        try { Directory.CreateDirectory(Path.GetDirectoryName(path)!); } catch { }

        // Write to a temp file first, then swap it in. A direct in-place write fails when the file is
        // memory-mapped -- every plugin loaded via 'Open MO2' is an overlay the editor itself locks,
        // so we never write straight over the live file.
        var tmp = path + "." + Guid.NewGuid().ToString("N")[..8] + ".tmp";
        try { mod.WriteToBinary(tmp, prms); }
        catch (Exception ex) { try { File.Delete(tmp); } catch { } return $"Save failed while writing: {ex.Message}"; }

        try
        {
            if (File.Exists(path)) File.Replace(tmp, path, null);
            else File.Move(tmp, path);
            NotifyChanged(name);
            var inOverwrite = !string.IsNullOrWhiteSpace(Mo2OverwriteFolder) &&
                path.StartsWith(Mo2OverwriteFolder!, StringComparison.OrdinalIgnoreCase);
            return inOverwrite
                ? $"Saved {name} to the MO2 overwrite folder ({path}). MO2 loads it automatically at top " +
                  "priority — just enable it in the plugin list if it isn't already checked, and place it " +
                  "after the plugins it patches."
                : $"Saved {name} to {path}.";
        }
        catch (Exception)
        {
            // The target is locked. This is almost always THIS editor's own overlay from 'Open MO2'
            // (not MO2 and not xEdit). Keep the freshly-written copy beside it so no work is lost.
            var sideways = path + ".new";
            try { if (File.Exists(sideways)) File.Delete(sideways); File.Move(tmp, sideways); }
            catch { sideways = tmp; }
            return $"Wrote {name} to '{sideways}', but could NOT overwrite '{path}'. That file is locked by " +
                   $"THIS editor: every plugin loaded via 'Open MO2' is memory-mapped, so the editor holds it " +
                   $"open (closing MO2 will NOT help). To apply it: close this editor and rename " +
                   $"'{Path.GetFileName(sideways)}' to '{Path.GetFileName(path)}', or save the patch under a " +
                   $"NEW plugin name that isn't in the loaded list.";
        }
    }

    /// <summary>
    /// Evict a plugin from the editor session and re-open it from its original on-disk path.
    /// Fixes stale in-memory state after a save (e.g. check_plugin reporting false-positive
    /// undeclared-master errors because it sees pre-save session state).
    /// </summary>
    public static string ReloadPlugin(string plugin, object? env)
    {
        var (name, _) = NormalizePlugin(plugin);
        if (!_sourcePath.TryGetValue(name, out var path))
            return $"'{name}' is not open for editing. Use open_plugin first.";
        _mutable.Remove(name);
        _sourcePath.Remove(name);
        MutagenLoader.EditableMods.TryRemove(name, out _);
        MutagenLoader.LooseMods.TryRemove(name, out _);
        MutagenLoader.InvalidateModIndex(name);
        return OpenPlugin(path, env);
    }

    /// <summary>
    /// Load a plugin binary, drop every COBJ condition whose param1/param2/reference FormKey
    /// belongs to one of the stripped masters, then write it back via Mutagen so that the master
    /// list is recomputed from scratch and all remaining FormID high-bytes are correct.
    /// This is the Mutagen-native fix for the ITO master-index corruption found in session 2026-06-22.
    /// </summary>
    public static string StripMastersClean(string sourcePath, string[] mastersToStrip, string outputPath, bool dryRun)
    {
        if (!File.Exists(sourcePath))
            return $"Source file not found: '{sourcePath}'.";
        if (mastersToStrip.Length == 0)
            return "Provide at least one master name to strip.";

        var stripSet = new HashSet<string>(mastersToStrip, StringComparer.OrdinalIgnoreCase);
        IFallout4Mod mod;
        try { mod = Fallout4Mod.CreateFromBinary(ModPath.FromPath(sourcePath), Fallout4Release.Fallout4); }
        catch (Exception ex) { return $"Failed to load '{sourcePath}': {ex.Message}"; }

        int dropped = 0, touched = 0;
        foreach (var cobj in mod.ConstructibleObjects)
        {
            var toRemove = cobj.Conditions.Where(c => ConditionRefersToStrippedMaster(c, stripSet)).ToList();
            if (toRemove.Count == 0) continue;
            foreach (var c in toRemove) cobj.Conditions.Remove(c);
            dropped += toRemove.Count;
            touched++;
        }

        var summary = $"Loaded '{Path.GetFileName(sourcePath)}': " +
                      $"{dropped} condition(s) across {touched} record(s) reference the stripped master(s).";
        if (dryRun)
            return $"[DRY RUN] {summary} Pass dry_run=false to write.";

        if (string.IsNullOrWhiteSpace(outputPath))
            outputPath = sourcePath;
        try { Directory.CreateDirectory(Path.GetDirectoryName(outputPath)!); } catch { }

        var prms = new BinaryWriteParameters
        {
            MastersListContent = MastersListContentOption.Iterate,
            ModKey = ModKeyOption.NoCheck,
        };
        var tmp = outputPath + "." + Guid.NewGuid().ToString("N")[..8] + ".tmp";
        try { mod.WriteToBinary(tmp, prms); }
        catch (Exception ex) { try { File.Delete(tmp); } catch { } return $"Write failed: {ex.Message}"; }

        try
        {
            if (File.Exists(outputPath)) File.Replace(tmp, outputPath, null);
            else File.Move(tmp, outputPath);
        }
        catch (Exception ex) { return $"Swap failed: {ex.Message} (tmp preserved at '{tmp}')"; }

        return $"Saved to '{outputPath}'. Stripped {mastersToStrip.Length} master(s); dropped {dropped} condition(s) across {touched} record(s). Mutagen recomputed all FormID master indices.";
    }

    private static bool ConditionRefersToStrippedMaster(Mutagen.Bethesda.Fallout4.IConditionGetter cond, HashSet<string> stripSet)
    {
        if (cond.Data is not Mutagen.Bethesda.Fallout4.IFunctionConditionDataGetter data) return false;
        if (!data.ParameterOneRecord.FormKey.IsNull && stripSet.Contains(data.ParameterOneRecord.FormKey.ModKey.FileName.String)) return true;
        if (!data.ParameterTwoRecord.FormKey.IsNull && stripSet.Contains(data.ParameterTwoRecord.FormKey.ModKey.FileName.String)) return true;
        if (!data.Reference.FormKey.IsNull && stripSet.Contains(data.Reference.FormKey.ModKey.FileName.String)) return true;
        return false;
    }

    /// <summary>
    /// Resolve a conflict by copying the chosen plugin's version of a record into a patch plugin
    /// as an override. The patch plugin (loading last) then wins the conflict. The patch is opened
    /// or created automatically and held in memory until SavePlugin is called.
    /// </summary>
    public static string ResolveConflict(object? env, string formKeyStr, string winningPlugin, string patchPlugin)
    {
        if (!FormKey.TryFactory(formKeyStr, out var fk)) return "Invalid FormKey.";
        if (string.IsNullOrWhiteSpace(patchPlugin)) return "Choose a patch plugin to write the resolution into.";

        var src = MutagenLoader.GetRecordVersion(env, winningPlugin, fk);
        if (src == null) return $"Could not find {fk} in '{winningPlugin}'.";

        // The patch may be given as a bare name or a full path. Ensure it's open and mutable:
        // open it if it already exists on disk (so we extend it), else create it new.
        var (patchName, patchPath) = NormalizePlugin(patchPlugin);
        if (GetMutable(patchName) == null)
        {
            bool existsOnDisk = (patchPath != null && File.Exists(patchPath))
                || MutagenLoader.LooseModPaths.ContainsKey(patchName)
                || FindPluginPath(patchName, env) != null;
            if (existsOnDisk) OpenPlugin(patchPlugin, env);
            else CreatePlugin(patchPlugin);
        }
        var patch = GetMutable(patchName);
        if (patch == null) return $"Could not open or create patch plugin '{patchName}'.";

        if (src is not IMajorRecordGetter getter)
            return $"{fk} is not an overridable record.";

        try
        {
            if (!AddOverride(patch, getter))
                return $"This record type ({getter.Registration.Name}) cannot be overridden yet.";
            MutagenLoader.InvalidateModIndex(patchName);
            NotifyChanged(patchName);
            return $"Copied {winningPlugin}'s version of {fk} into {patchName} as an override. " +
                   $"Ensure {patchName} loads after the conflicting plugins, then save it.";
        }
        catch (Exception ex)
        {
            return $"Resolve failed: {ex.Message}";
        }
    }

    // Mutagen 0.53.1 exposes GetOrAddAsOverride only on IGroup<T> (the group mixin), not on the
    // mod. Find the mod's typed group whose element matches the record and invoke the mixin via
    // reflection so this works for any record type without a per-signature switch.
    private static readonly MethodInfo? _groupOverrideMixin = typeof(GetOrAddAsOverrideMixIns)
        .GetMethods(BindingFlags.Public | BindingFlags.Static)
        .FirstOrDefault(m => m.Name == "GetOrAddAsOverride"
            && m.IsGenericMethodDefinition
            && m.GetGenericArguments().Length == 2
            && m.GetParameters().Length == 2);

    private static bool AddOverride(IFallout4Mod mod, IMajorRecordGetter getter)
    {
        if (_groupOverrideMixin == null) return false;
        // Match the mod's typed group by the record's GETTER type: the group's generic argument
        // is the CONCRETE record class (e.g. ConstructibleObject), which implements the getter
        // interface (IConstructibleObjectGetter). Registration.SetterType is the *interface*, so
        // comparing against it never matched the concrete group arg -- that was the bug.
        var getterType = getter.Registration.GetterType;

        foreach (var prop in mod.GetType().GetProperties(BindingFlags.Public | BindingFlags.Instance))
        {
            var pt = prop.PropertyType;
            if (!pt.IsGenericType) continue;
            var args = pt.GetGenericArguments();
            if (args.Length != 1) continue;
            var groupMajor = args[0];                              // concrete record class
            if (!getterType.IsAssignableFrom(groupMajor)) continue;

            var group = prop.GetValue(mod);
            if (group is not Mutagen.Bethesda.Plugins.Records.IGroup) continue;

            try
            {
                _groupOverrideMixin.MakeGenericMethod(groupMajor, getterType)
                    .Invoke(null, new[] { group, getter });
                return true;
            }
            catch { /* try another candidate group */ }
        }
        return false;
    }

    /// <summary>Plugins currently open for editing (candidates for a conflict patch target).</summary>
    public static IReadOnlyList<string> EditablePlugins() => _mutable.Keys.ToList();

    /// <summary>
    /// Persist the in-grid edits of one record into a patch plugin: forward the record's current
    /// (winning) version into the patch as an override, apply every edited field from the display
    /// tree onto that override, and write the patch to disk. Returns a human-readable result.
    /// </summary>
    public static string SaveRecordEditsToPatch(
        object? env, Models.RecordNode record, string sourcePlugin, string patchPlugin)
    {
        var fkStr = record.GetValue("FormKey");
        if (fkStr == null || !FormKey.TryFactory(fkStr, out var fk))
            return "This record has no FormKey to patch.";
        if (string.IsNullOrWhiteSpace(patchPlugin)) return "Enter a patch plugin name.";
        if (!patchPlugin.EndsWith(".esp", StringComparison.OrdinalIgnoreCase) &&
            !patchPlugin.EndsWith(".esm", StringComparison.OrdinalIgnoreCase) &&
            !patchPlugin.EndsWith(".esl", StringComparison.OrdinalIgnoreCase))
            patchPlugin += ".esp";

        var dirtyLeaves = record.Descendants().Where(n => n.IsDirty && n.IsLeaf).ToList();
        if (dirtyLeaves.Count == 0) return "No field edits to save — change a value first.";

        // 1. Forward the winning version into the patch as a mutable override.
        var forward = ResolveConflict(env, fkStr, sourcePlugin, patchPlugin);
        var patch = GetMutable(patchPlugin);
        if (patch == null) return forward;   // ResolveConflict already explained the failure

        var rec = FindMutableRecord(patch, fkStr);
        if (rec == null) return $"Could not locate {fk} in the patch after forwarding it. ({forward})";

        // 2. Apply each edited field onto the override by its tree path.
        int applied = 0; var failures = new List<string>();
        foreach (var leaf in dirtyLeaves)
        {
            var segments = PathSegments(record, leaf);
            if (segments.Count == 0) continue;
            if (SetFieldByPath(rec, segments, leaf.Value, env, out var err)) applied++;
            else failures.Add($"{string.Join(".", segments)}: {err}");
        }

        // 3. Write the patch (Output folder by default).
        MutagenLoader.InvalidateModIndex(patchPlugin);
        var save = SavePlugin(patchPlugin, null);
        record.ClearDirty();

        var msg = $"Applied {applied} edit(s) to {patchPlugin} and saved. {save} " +
                  $"Enable {patchPlugin} AFTER the conflicting plugins so it wins.";
        if (failures.Count > 0)
            msg += $"\n{failures.Count} field(s) could not be applied: {string.Join("; ", failures.Take(5))}";
        return msg;
    }

    // The display-tree path from the record root down to a leaf, e.g. ["Components","[0]","Count"].
    private static List<string> PathSegments(Models.RecordNode root, Models.RecordNode leaf)
    {
        var segs = new List<string>();
        for (var n = leaf; n != null && !ReferenceEquals(n, root); n = n.Parent)
            segs.Add(n.Key);
        segs.Reverse();
        return segs;
    }

    // Navigate property names and list indices ("[i]") to the leaf's owner, then set it.
    // Mirrors SetLeaf's scalar/FormLink handling, but reaches into lists (components, etc.).
    private static bool SetFieldByPath(object rec, List<string> segments, string value, object? env, out string msg)
    {
        msg = "";
        object cur = rec;
        for (int i = 0; i < segments.Count - 1; i++)
        {
            var seg = segments[i];
            if (seg.Length > 2 && seg[0] == '[' && seg[^1] == ']' && int.TryParse(seg[1..^1], out var idx))
            {
                if (cur is not System.Collections.IList list) { msg = $"'{segments[i - 1]}' is not an indexable list."; return false; }
                if (idx < 0 || idx >= list.Count) { msg = $"index {idx} out of range ({list.Count})."; return false; }
                cur = list[idx]!;
            }
            else
            {
                var p = cur.GetType().GetProperty(seg, BindingFlags.Public | BindingFlags.Instance | BindingFlags.IgnoreCase);
                if (p == null) { msg = $"no field '{seg}' on {cur.GetType().Name}."; return false; }
                var next = p.CanRead ? p.GetValue(cur) : null;
                if (next == null) { msg = $"'{seg}' is null and can't be edited."; return false; }
                cur = next;
            }
        }
        return SetLeaf(cur, segments[^1], value, env, out msg);
    }

    // ---- copy as override / delete / list editing / COBJ structs -----------------------

    /// <summary>
    /// Copy a record (identified by FormKey or EditorID) from the chosen source plugin into a patch
    /// plugin as an override — the xEdit "Copy as Override Into" operation. The patch is opened or
    /// created automatically. After this, edit the override with set_field/set_components/etc.
    /// </summary>
    public static string CopyAsOverride(object? env, string sourcePlugin, string id, string patchPlugin)
    {
        if (!ResolveFk(env, id, out var fk))
            return $"'{id}' is not a FormKey and no loaded record has that EditorID.";
        return ResolveConflict(env, fk.ToString(), sourcePlugin, patchPlugin);
    }

    /// <summary>Remove a record entirely from a plugin (xEdit "Remove"). Use for deleting a stub
    /// record the editor created incompletely, or dropping an unwanted override from a patch.</summary>
    public static string DeleteRecord(string plugin, string recordId, object? env)
    {
        var mod = EnsureOpen(plugin, env, out var openMsg); if (mod == null) return openMsg;
        var rec = FindMutableRecord(mod, recordId); if (rec == null) return $"Record '{recordId}' not found in {plugin}.";
        var fk = rec.FormKey;

        foreach (var groupProp in mod.GetType().GetProperties()
                     .Where(p => typeof(Mutagen.Bethesda.Plugins.Records.IGroup).IsAssignableFrom(p.PropertyType)))
        {
            if (groupProp.GetValue(mod) is not Mutagen.Bethesda.Plugins.Records.IGroup group) continue;
            var contains = ((System.Collections.IEnumerable)group)
                .Cast<Mutagen.Bethesda.Plugins.Records.IMajorRecordGetter>().Any(r => r.FormKey == fk);
            if (!contains) continue;
            group.GetType().GetMethod("Remove", new[] { typeof(FormKey) })?.Invoke(group, new object[] { fk });
            MutagenLoader.InvalidateModIndex(plugin); NotifyChanged(plugin);
            return $"Removed {rec.EditorID} [{fk}] from {plugin}. Call save_plugin to persist.";
        }
        return $"Could not locate the group holding {fk} in {plugin}.";
    }

    /// <summary>Remove a FormLink entry (by FormKey or EditorID) from a list field — e.g. drop an
    /// item from a FormList's 'Items' or a keyword from 'Categories'/'Keywords'.</summary>
    public static string RemoveListItem(string plugin, string recordId, string field, string value, object? env)
    {
        var mod = EnsureOpen(plugin, env, out var openMsg); if (mod == null) return openMsg;
        var rec = FindMutableRecord(mod, recordId); if (rec == null) return $"Record '{recordId}' not found in {plugin}.";

        var prop = rec.GetType().GetProperty(field, BindingFlags.Public | BindingFlags.Instance | BindingFlags.IgnoreCase);
        if (prop?.GetValue(rec) is not System.Collections.IList list) return $"Field '{field}' is not a list on {rec.GetType().Name}.";
        if (!ResolveFk(env, value, out var fk)) return $"'{value}' is not a FormKey and no loaded record has that EditorID.";

        int removed = 0;
        for (int i = list.Count - 1; i >= 0; i--)
        {
            var item = list[i];
            var fkProp = item?.GetType().GetProperty("FormKey");
            if (fkProp?.GetValue(item) is FormKey ifk && ifk == fk) { list.RemoveAt(i); removed++; }
        }
        if (removed == 0) return $"No entry matching {value} found in {field} ({list.Count} item(s)).";
        MutagenLoader.InvalidateModIndex(plugin); NotifyChanged(plugin);
        return $"Removed {removed} entr(y/ies) matching {value} from {field} on {recordId} (now {list.Count}). save_plugin to persist.";
    }

    /// <summary>Replace a COBJ's component list. componentsJson is an array of
    /// {"component":"FormKeyOrEditorId","count":N}. Components are the materials the recipe consumes.</summary>
    public static string SetComponents(string plugin, string recordId, string componentsJson, object? env)
    {
        var mod = EnsureOpen(plugin, env, out var openMsg); if (mod == null) return openMsg;
        var rec = FindMutableRecord(mod, recordId); if (rec == null) return $"Record '{recordId}' not found in {plugin}.";
        if (rec is not ConstructibleObject cobj)
            return $"set_components only applies to COBJ records; {recordId} is {rec.GetType().Name}.";

        var list = new Noggog.ExtendedList<ConstructibleObjectComponent>();
        var failures = new List<string>();
        try
        {
            using var doc = JsonDocument.Parse(string.IsNullOrWhiteSpace(componentsJson) ? "[]" : componentsJson);
            foreach (var el in doc.RootElement.EnumerateArray())
            {
                var comp = el.TryGetProperty("component", out var c) ? c.GetString() ?? "" : "";
                uint count = el.TryGetProperty("count", out var ct) && ct.TryGetUInt32(out var n) ? n : 1u;
                if (!ResolveFk(env, comp, out var fk)) { failures.Add($"{comp}: unresolved"); continue; }
                var item = new ConstructibleObjectComponent { Count = count };
                item.Component.SetTo(fk);
                list.Add(item);
            }
        }
        catch (Exception ex) { return $"Could not parse components JSON: {ex.Message}. Expected [{{\"component\":\"01FAA5:Fallout4.esm\",\"count\":1}}]."; }

        cobj.Components = list;
        MutagenLoader.InvalidateModIndex(plugin); NotifyChanged(plugin);
        var msg = $"Set {list.Count} component(s) on {recordId} in {plugin}.";
        if (failures.Count > 0) msg += $" Skipped {failures.Count}: {string.Join("; ", failures)}.";
        return msg + " save_plugin to persist.";
    }

    /// <summary>Replace a COBJ's condition list. conditionsJson is an array of
    /// {"function","param1","param2","operator","value","compareGlobal","runOn","reference"}.
    /// function = e.g. GetGlobalValue/GetBaseValue/GetItemCount/HasKeyword/GetStageDone;
    /// param1/param2 = a FormKey/EditorID (record param) or an integer; operator = == != &gt; &gt;= &lt; &lt;=;
    /// value = the float compared against (omit when compareGlobal is set);
    /// compareGlobal = optional global FormKey to compare against instead of a constant;
    /// runOn = Subject/Target/Reference/...; reference = the ref FormKey when runOn=Reference (e.g. PlayerRef).</summary>
    public static string SetConditions(string plugin, string recordId, string conditionsJson, object? env)
    {
        var mod = EnsureOpen(plugin, env, out var openMsg); if (mod == null) return openMsg;
        var rec = FindMutableRecord(mod, recordId); if (rec == null) return $"Record '{recordId}' not found in {plugin}.";

        var condProp = rec.GetType().GetProperty("Conditions");
        if (condProp?.GetValue(rec) is not System.Collections.IList condList)
            return $"{rec.GetType().Name} has no editable Conditions list.";

        var built = new List<Condition>();
        var failures = new List<string>();
        try
        {
            using var doc = JsonDocument.Parse(string.IsNullOrWhiteSpace(conditionsJson) ? "[]" : conditionsJson);
            int idx = 0;
            foreach (var el in doc.RootElement.EnumerateArray())
            {
                idx++;
                var fnName = el.TryGetProperty("function", out var f) ? f.GetString() ?? "" : "";
                if (!Enum.TryParse<Condition.Function>(fnName, ignoreCase: true, out var fn))
                { failures.Add($"#{idx}: unknown function '{fnName}'"); continue; }

                var data = new FunctionConditionData { Function = fn };
                if (el.TryGetProperty("runOn", out var ro) && ro.GetString() is { } roStr &&
                    Enum.TryParse<Condition.RunOnType>(roStr, ignoreCase: true, out var roType))
                    data.RunOnType = roType;
                if (el.TryGetProperty("reference", out var rf) && rf.GetString() is { } rfStr &&
                    ResolveFk(env, rfStr, out var rfFk))
                    data.Reference.SetTo(rfFk);
                ApplyParam(el, "param1", env, fk => data.ParameterOneRecord.SetTo(fk), n => data.ParameterOneNumber = n);
                ApplyParam(el, "param2", env, fk => data.ParameterTwoRecord.SetTo(fk), n => data.ParameterTwoNumber = n);

                var op = ParseOperator(el.TryGetProperty("operator", out var o) ? o.GetString() : null);

                Condition.Flag condFlags = default;
                if (el.TryGetProperty("flags", out var flagEl) && flagEl.GetString() is { Length: > 0 } flagStr)
                {
                    foreach (var part in flagStr.Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
                        if (Enum.TryParse<Condition.Flag>(part, ignoreCase: true, out var fl)) condFlags |= fl;
                }

                if (el.TryGetProperty("compareGlobal", out var cg) && cg.GetString() is { } cgStr && ResolveFk(env, cgStr, out var cgFk))
                {
                    var cond = new ConditionGlobal { CompareOperator = op, Data = data };
                    cond.ComparisonValue.SetTo(cgFk);
                    if (condFlags != default) cond.Flags = condFlags;
                    built.Add(cond);
                }
                else
                {
                    float val = el.TryGetProperty("value", out var v) && v.TryGetSingle(out var fv) ? fv : 1f;
                    var cond = new ConditionFloat { CompareOperator = op, ComparisonValue = val, Data = data };
                    if (condFlags != default) cond.Flags = condFlags;
                    built.Add(cond);
                }
            }
        }
        catch (Exception ex) { return $"Could not parse conditions JSON: {ex.Message}."; }

        condList.Clear();
        foreach (var c in built) condList.Add(c);
        MutagenLoader.InvalidateModIndex(plugin); NotifyChanged(plugin);
        var msg = $"Set {built.Count} condition(s) on {recordId} in {plugin}.";
        if (failures.Count > 0) msg += $" Skipped {failures.Count}: {string.Join("; ", failures)}.";
        return msg + " save_plugin to persist.";
    }

    // A condition parameter is either a record (FormKey/EditorID) or an integer.
    private static void ApplyParam(JsonElement el, string key, object? env, Action<FormKey> setRecord, Action<int> setNumber)
    {
        if (!el.TryGetProperty(key, out var p)) return;
        if (p.ValueKind == JsonValueKind.Number && p.TryGetInt32(out var n)) { setNumber(n); return; }
        if (p.ValueKind == JsonValueKind.String && p.GetString() is { Length: > 0 } s)
        {
            if (int.TryParse(s, out var ni)) setNumber(ni);
            else if (ResolveFk(env, s, out var fk)) setRecord(fk);
        }
    }

    private static CompareOperator ParseOperator(string? op) => (op ?? "==").Trim() switch
    {
        "==" or "=" or "EqualTo" => CompareOperator.EqualTo,
        "!=" or "<>" or "NotEqualTo" => CompareOperator.NotEqualTo,
        ">" or "GreaterThan" => CompareOperator.GreaterThan,
        ">=" or "GreaterThanOrEqualTo" => CompareOperator.GreaterThanOrEqualTo,
        "<" or "LessThan" => CompareOperator.LessThan,
        "<=" or "LessThanOrEqualTo" => CompareOperator.LessThanOrEqualTo,
        _ => CompareOperator.EqualTo,
    };

    // Resolve a FormKey ('001234:Plugin.esp') or an EditorID to a FormKey via the load-order link
    // cache (global) with a fallback to the per-mod index.
    private static bool ResolveFk(object? env, string value, out FormKey fk)
    {
        if (FormKey.TryFactory(value, out fk)) return true;
        var cache = MutagenLoader.LinkCache;
        if (cache != null && cache.TryResolve(value, out var rec)) { fk = rec.FormKey; return true; }
        fk = MutagenLoader.ResolveEditorIdToFormKey(env, value);
        return !fk.IsNull;
    }

    // ---- batch revert (undo a plugin's bad overrides across the whole load order) -------

    /// <summary>
    /// Revert records that <paramref name="badPlugin"/> currently WINS (it's the last override) back
    /// to the version loaded just before it, by forwarding that prior version into a patch plugin.
    /// This undoes a mod that mass-injected the same bad edit (e.g. a corrupted recipe template) across
    /// many records. Defaults to a DRY RUN that lists what it would do; pass apply=true to write them.
    /// Optionally filter to one signature (e.g. "COBJ") and/or to records whose winning version uses a
    /// specific component FormKey (e.g. the hubflower), to target exactly the corrupted set.
    /// </summary>
    public static string RevertOverridesFrom(object? env, string badPlugin, string patchPlugin,
        string? sig, string? containsComponent, bool apply, int limit)
    {
        if (string.IsNullOrWhiteSpace(badPlugin)) return "Provide the plugin whose overrides to revert.";
        FormKey? compFk = null;
        if (!string.IsNullOrWhiteSpace(containsComponent))
        {
            if (!ResolveFk(env, containsComponent, out var cfk))
                return $"'{containsComponent}' is not a FormKey/EditorID of a loaded record.";
            compFk = cfk;
        }

        var overrides = MutagenLoader.EnumerateOverrides(env, badPlugin, sig);
        if (overrides.Count == 0)
            return $"'{badPlugin}' carries no override records{(sig != null ? $" of type {sig}" : "")} (or it isn't loaded).";

        IFallout4Mod? patch = null;
        if (apply)
        {
            if (string.IsNullOrWhiteSpace(patchPlugin)) return "Provide a patch plugin to write the reverts into.";
            if (GetMutable(patchPlugin) == null)
            {
                if (MutagenLoader.LooseModPaths.ContainsKey(patchPlugin) || FindPluginPath(patchPlugin, env) != null)
                    OpenPlugin(patchPlugin, env);
                else CreatePlugin(patchPlugin);
            }
            patch = GetMutable(patchPlugin);
            if (patch == null) return $"Could not open or create patch plugin '{patchPlugin}'.";
        }

        var preview = new List<string>();
        int reverted = 0, skipped = 0, noPrior = 0;
        foreach (var (fk, eid, rsig) in overrides)
        {
            var ctxs = MutagenLoader.GetRecordContexts(env, fk);
            if (ctxs.Count < 2) { noPrior++; continue; }
            // Only act where badPlugin is what the game currently uses (the winner).
            if (!string.Equals(ctxs[^1].plugin, badPlugin, StringComparison.OrdinalIgnoreCase)) { skipped++; continue; }
            if (compFk != null && !MutagenLoader.CobjUsesComponent(ctxs[^1].rec, compFk.Value)) { skipped++; continue; }

            var prior = ctxs[^2];   // the version badPlugin overrode
            if (apply)
            {
                if (prior.rec is IMajorRecordGetter g && AddOverride(patch!, g)) reverted++;
                else skipped++;
            }
            else
            {
                reverted++;
                if (preview.Count < limit)
                    preview.Add($"{eid} [{fk}] ({rsig}) -> restore {prior.plugin}'s version");
            }
        }

        if (apply)
        {
            MutagenLoader.InvalidateModIndex(patchPlugin); NotifyChanged(patchPlugin);
            var save = SavePlugin(patchPlugin, null);
            return $"Reverted {reverted} record(s) that {badPlugin} was overriding, into {patchPlugin}. " +
                   $"{(skipped > 0 ? skipped + " skipped (not the winner / filtered out). " : "")}{save} " +
                   $"Load {patchPlugin} AFTER {badPlugin} so the reverts win.";
        }

        var sb = new System.Text.StringBuilder();
        sb.AppendLine($"DRY RUN — {reverted} record(s) where {badPlugin} is the winner would be reverted to the prior version" +
                      $"{(compFk != null ? $" (filtered to ones using {containsComponent})" : "")}.");
        if (skipped > 0) sb.AppendLine($"({skipped} of {badPlugin}'s overrides skipped — a later plugin already wins, or filtered out.)");
        foreach (var line in preview) sb.AppendLine("  " + line);
        if (reverted > preview.Count) sb.AppendLine($"  …and {reverted - preview.Count} more.");
        sb.AppendLine($"Run revert_overrides again with apply=true to write these into '{patchPlugin}'.");
        return sb.ToString();
    }

    // ---- helpers --------------------------------------------------------

    private const string SupportedTypes =
        "BOOK/HOLOTAPE, TERM, WEAP, ARMO, MISC, COBJ, KYWD, AMMO, ALCH, ACTI, CONT, " +
        "FLST, MGEF, PERK, NPC_, QUST, MESG.";

    // True if the plugin must keep FormIDs in the ESL light range (0x800-0xFFF).
    private static bool IsEsl(IFallout4Mod mod)
    {
        if (mod.ModKey.FileName.String.EndsWith(".esl", StringComparison.OrdinalIgnoreCase)) return true;
        try
        {
            var flags = mod.ModHeader.Flags.ToString();
            return flags.Contains("Light", StringComparison.OrdinalIgnoreCase) ||
                   flags.Contains("Small", StringComparison.OrdinalIgnoreCase);
        }
        catch { return false; }
    }

    // Lowest free object FormID >= 0x800, capped at 0xFFF for ESL plugins.
    private static FormKey? NextFreeFormKey(IFallout4Mod mod)
    {
        uint end = IsEsl(mod) ? 0xFFFu : 0xFFFFFFu;
        var used = new HashSet<uint>(mod.EnumerateMajorRecords().Select(r => r.FormKey.ID));
        for (uint id = 0x800; id <= end; id++)
            if (!used.Contains(id)) return new FormKey(mod.ModKey, id);
        return null;   // ESL range exhausted
    }

    // Construct a record of the given type with an explicit (ESL-safe) FormKey and add it.
    // We allocate the FormKey ourselves rather than via AddNew(), which otherwise inherits
    // the plugin's header NextObjectID and can hand out IDs outside the ESL range.
    private static IFallout4MajorRecord? AddNewBySig(IFallout4Mod mod, string sig, string editorId, FormKey fk)
    {
        var r = Fallout4Release.Fallout4;
        switch (sig.ToUpperInvariant())
        {
            case "BOOK": case "HOLOTAPE": { var x = new Book(fk, r) { EditorID = editorId };               mod.Books.Add(x);                return x; }
            case "TERM":                  { var x = new Terminal(fk, r) { EditorID = editorId };           mod.Terminals.Add(x);            return x; }
            case "WEAP":                  { var x = new Weapon(fk, r) { EditorID = editorId };             mod.Weapons.Add(x);              return x; }
            case "ARMO":                  { var x = new Armor(fk, r) { EditorID = editorId };              mod.Armors.Add(x);               return x; }
            case "MISC":                  { var x = new MiscItem(fk, r) { EditorID = editorId };           mod.MiscItems.Add(x);            return x; }
            case "COBJ":                  { var x = new ConstructibleObject(fk, r) { EditorID = editorId };mod.ConstructibleObjects.Add(x); return x; }
            case "KYWD":                  { var x = new Keyword(fk, r) { EditorID = editorId };            mod.Keywords.Add(x);             return x; }
            case "AMMO":                  { var x = new Ammunition(fk, r) { EditorID = editorId };         mod.Ammunitions.Add(x);          return x; }
            case "ALCH":                  { var x = new Ingestible(fk, r) { EditorID = editorId };         mod.Ingestibles.Add(x);          return x; }
            case "ACTI":                  { var x = new Mutagen.Bethesda.Fallout4.Activator(fk, r) { EditorID = editorId }; mod.Activators.Add(x);    return x; }
            case "CONT":                  { var x = new Container(fk, r) { EditorID = editorId };          mod.Containers.Add(x);           return x; }
            case "FLST":                  { var x = new FormList(fk, r) { EditorID = editorId };           mod.FormLists.Add(x);            return x; }
            case "MGEF":                  { var x = new MagicEffect(fk, r) { EditorID = editorId };        mod.MagicEffects.Add(x);         return x; }
            case "PERK":                  { var x = new Perk(fk, r) { EditorID = editorId };               mod.Perks.Add(x);                return x; }
            case "NPC_":                  { var x = new Npc(fk, r) { EditorID = editorId };                mod.Npcs.Add(x);                 return x; }
            case "QUST":                  { var x = new Quest(fk, r) { EditorID = editorId };              mod.Quests.Add(x);               return x; }
            case "MESG":                  { var x = new Message(fk, r) { EditorID = editorId };            mod.Messages.Add(x);             return x; }
            default:                      return null;
        }
    }

    private static IFallout4MajorRecord? FindMutableRecord(IFallout4Mod mod, string id)
    {
        var all = mod.EnumerateMajorRecords().Cast<IFallout4MajorRecordGetter>().ToList();
        IFallout4MajorRecordGetter? hit = null;
        if (FormKey.TryFactory(id, out var fk))
            hit = all.FirstOrDefault(r => r.FormKey == fk);
        hit ??= all.FirstOrDefault(r => string.Equals(r.EditorID, id, StringComparison.OrdinalIgnoreCase));
        return hit as IFallout4MajorRecord;
    }

    private static bool TrySet(object rec, string field, string value, object? env, out string msg)
    {
        // Convenience: "Model" / "nif" on a record sets the mesh path (Model.File).
        if (string.Equals(field, "Model", StringComparison.OrdinalIgnoreCase) ||
            string.Equals(field, "nif", StringComparison.OrdinalIgnoreCase))
            field = "Model.File";

        // Navigate a dotted/indexed path -- properties ("Model.File") AND list indices
        // ("Effects[0].Data.RunImmediately", "Components[0].Count") -- creating missing
        // intermediate objects where possible, then set the final leaf.
        var segments = SplitFieldPath(field);
        if (segments.Count == 0) { msg = "Empty field path."; return false; }

        object cur = rec;
        for (int i = 0; i < segments.Count - 1; i++)
        {
            var seg = segments[i];
            if (TryIndex(seg, out var idx))
            {
                if (cur is not System.Collections.IList list)
                { msg = $"'{(i > 0 ? segments[i - 1] : "value")}' is not an indexable list."; return false; }
                if (idx < 0 || idx >= list.Count)
                { msg = $"index {idx} is out of range (the list has {list.Count} item(s))."; return false; }
                cur = list[idx]!;
            }
            else
            {
                var p = cur.GetType().GetProperty(seg, BindingFlags.Public | BindingFlags.Instance | BindingFlags.IgnoreCase);
                if (p == null) { msg = $"No field '{seg}' on {cur.GetType().Name}."; return false; }
                var next = p.CanRead ? p.GetValue(cur) : null;
                if (next == null)
                {
                    var pt = Nullable.GetUnderlyingType(p.PropertyType) ?? p.PropertyType;
                    try { next = System.Activator.CreateInstance(pt); } catch { next = null; }
                    if (next == null || !p.CanWrite) { msg = $"Can't initialise '{seg}'."; return false; }
                    p.SetValue(cur, next);
                }
                cur = next;
            }
        }

        var last = segments[^1];
        if (TryIndex(last, out _))
        {
            msg = $"Address a field INSIDE the list element, e.g. '{field}.<Field>' -- setting a whole element by index isn't supported.";
            return false;
        }
        return SetLeaf(cur, last, value, env, out msg);
    }

    // "[3]" -> 3. Returns false for property names.
    private static bool TryIndex(string seg, out int idx)
    {
        idx = -1;
        return seg.Length >= 3 && seg[0] == '[' && seg[^1] == ']' && int.TryParse(seg[1..^1], out idx);
    }

    // "Effects[0].Data.RunImmediately" -> ["Effects","[0]","Data","RunImmediately"].
    private static List<string> SplitFieldPath(string field)
    {
        var segs = new List<string>();
        foreach (var part in field.Split('.', StringSplitOptions.RemoveEmptyEntries))
        {
            int b = part.IndexOf('[');
            if (b < 0) { segs.Add(part); continue; }
            if (b > 0) segs.Add(part[..b]);            // the property name before the first '['
            int j = b;
            while (j < part.Length && part[j] == '[')   // one or more [n] indexers
            {
                int close = part.IndexOf(']', j);
                if (close < 0) break;
                segs.Add(part[j..(close + 1)]);
                j = close + 1;
            }
        }
        return segs;
    }

    private static bool SetLeaf(object rec, string field, string value, object? env, out string msg)
    {
        msg = "";
        var prop = rec.GetType().GetProperty(field,
            BindingFlags.Public | BindingFlags.Instance | BindingFlags.IgnoreCase);
        if (prop == null) { msg = $"No field '{field}' on {rec.GetType().Name}."; return false; }

        // FormLink? Settable via the link's SetTo(FormKey) even when the property is get-only.
        var current = prop.CanRead ? prop.GetValue(rec) : null;
        if (current != null)
        {
            var setTo = current.GetType().GetMethods()
                .FirstOrDefault(m => m.Name == "SetTo" && m.GetParameters().Length == 1 &&
                    (m.GetParameters()[0].ParameterType == typeof(FormKey) ||
                     m.GetParameters()[0].ParameterType == typeof(FormKey?)));
            if (setTo != null)
            {
                FormKey fk;
                if (!FormKey.TryFactory(value, out fk))
                {
                    fk = MutagenLoader.ResolveEditorIdToFormKey(env, value);
                    if (fk.IsNull)
                    {
                        msg = $"'{value}' is not a FormKey and no loaded record has that EditorID. " +
                              $"Use a FormKey like '001234:Plugin.esp'.";
                        return false;
                    }
                }
                try { setTo.Invoke(current, new object[] { fk }); return true; }
                catch (Exception ex) { msg = $"Could not set link {field}: {ex.Message}"; return false; }
            }
        }

        if (!prop.CanWrite) { msg = $"Field '{field}' is read-only."; return false; }

        var t = Nullable.GetUnderlyingType(prop.PropertyType) ?? prop.PropertyType;
        try
        {
            object converted;
            if (t == typeof(string)) converted = value;
            else if (t == typeof(TranslatedString))
                converted = typeof(TranslatedString).GetMethod("op_Implicit", new[] { typeof(string) })!
                    .Invoke(null, new object[] { value })!;
            else if (t.IsEnum) converted = Enum.Parse(t, value, ignoreCase: true);
            // Values arrive as invariant-formatted strings (from JSON / the React editor), so parse
            // them with InvariantCulture -- the current culture would break "1.5" on comma-decimal locales.
            else if (t == typeof(bool)) converted = bool.Parse(value);
            else if (t == typeof(float)) converted = float.Parse(value, Inv);
            else if (t == typeof(double)) converted = double.Parse(value, Inv);
            else if (t == typeof(int)) converted = int.Parse(value, Inv);
            else if (t == typeof(uint)) converted = uint.Parse(value, Inv);
            else if (t == typeof(short)) converted = short.Parse(value, Inv);
            else if (t == typeof(ushort)) converted = ushort.Parse(value, Inv);
            else if (t == typeof(byte)) converted = byte.Parse(value, Inv);
            else if (t == typeof(long)) converted = long.Parse(value, Inv);
            else { msg = $"Field '{field}' has type {t.Name}, which set_field can't set yet (scalar/text only)."; return false; }

            prop.SetValue(rec, converted);
            return true;
        }
        catch (Exception ex)
        {
            msg = $"Could not set {field}: {ex.Message}";
            return false;
        }
    }
}
