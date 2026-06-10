using System.IO;
using System.Reflection;
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
    private static readonly Dictionary<string, IFallout4Mod> _mutable = new(StringComparer.OrdinalIgnoreCase);
    // Original on-disk path of an opened plugin, so save_plugin can overwrite it in place.
    private static readonly Dictionary<string, string> _sourcePath = new(StringComparer.OrdinalIgnoreCase);

    /// <summary>User-configured output folder (from Settings). Blank => &lt;app&gt;/Output.</summary>
    public static string? OutputFolderOverride { get; set; }

    public static string DefaultOutputDir =>
        !string.IsNullOrWhiteSpace(OutputFolderOverride)
            ? OutputFolderOverride!
            : Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "Output");

    public static IFallout4Mod? GetMutable(string name) =>
        _mutable.TryGetValue(name, out var m) ? m : null;

    private static void Register(string name, IFallout4Mod mod)
    {
        _mutable[name] = mod;
        MutagenLoader.EditableMods[name] = mod;  // prioritized for ALL reads (hot loading)
        MutagenLoader.LooseMods[name] = mod;      // so list_plugins / the tree still see it
        MutagenLoader.InvalidateModIndex(name);
    }

    public static string OpenPlugin(string plugin, object? env)
    {
        if (string.IsNullOrWhiteSpace(plugin)) return "Provide a plugin name.";
        if (_mutable.ContainsKey(plugin)) return $"'{plugin}' is already open for editing.";

        string? filePath = FindPluginPath(plugin, env);
        if (string.IsNullOrEmpty(filePath) || !File.Exists(filePath))
            return $"Could not locate '{plugin}' on disk. Ensure the game environment is loaded or open the file via File > Open.";

        try
        {
            var mod = Fallout4Mod.CreateFromBinary(ModPath.FromPath(filePath), Fallout4Release.Fallout4);
            Register(plugin, mod);
            _sourcePath[plugin] = filePath;     // save_plugin overwrites the original by default
            NotifyChanged(plugin);
            var count = mod.EnumerateMajorRecords().Count();
            return $"Opened '{plugin}' for editing ({count} records). Use create_record, set_field, and save_plugin.";
        }
        catch (Exception ex)
        {
            return $"Failed to open '{plugin}' for editing: {ex.Message}";
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
        if (!name.EndsWith(".esp", StringComparison.OrdinalIgnoreCase) &&
            !name.EndsWith(".esm", StringComparison.OrdinalIgnoreCase) &&
            !name.EndsWith(".esl", StringComparison.OrdinalIgnoreCase))
            name += ".esp";

        var mod = new Fallout4Mod(ModKey.FromNameAndExtension(name), Fallout4Release.Fallout4);
        Register(name, mod);
        NotifyChanged(name);
        return $"Created new plugin '{name}'. Add records with create_record, then call save_plugin to write it.";
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
        var mod = GetMutable(plugin);
        if (mod != null) return mod;

        var openResult = OpenPlugin(plugin, env);   // loads it mutable + registers, or explains why not
        mod = GetMutable(plugin);
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
                case "int":    scriptProp = new ScriptIntProperty    { Name = propName, Data = int.Parse(value) };   break;
                case "float":  scriptProp = new ScriptFloatProperty  { Name = propName, Data = float.Parse(value) }; break;
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
        var mod = GetMutable(plugin);
        if (mod == null) return $"Plugin '{plugin}' is not open for editing.";

        // Default: overwrite the original file the plugin was opened from (modify in place).
        // Newly created plugins (no source) fall back to the configured Output folder.
        if (string.IsNullOrWhiteSpace(path))
            path = _sourcePath.TryGetValue(plugin, out var sp) ? sp : Path.Combine(DefaultOutputDir, plugin);

        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            mod.WriteToBinary(path, new BinaryWriteParameters
            {
                // Build the master list from the records' FormLinks (self-contained, no load order needed).
                MastersListContent = MastersListContentOption.Iterate
            });
            NotifyChanged(plugin);
            return $"Saved {plugin} to {path}.";
        }
        catch (Exception ex)
        {
            return $"Save failed: {ex.Message}";
        }
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

        // Ensure the patch plugin is open and mutable: open it if it exists on disk, else create it.
        if (GetMutable(patchPlugin) == null)
        {
            if (MutagenLoader.LooseModPaths.ContainsKey(patchPlugin) || FindPluginPath(patchPlugin, env) != null)
                OpenPlugin(patchPlugin, env);
            else
                CreatePlugin(patchPlugin);
        }
        var patch = GetMutable(patchPlugin);
        if (patch == null) return $"Could not open or create patch plugin '{patchPlugin}'.";

        if (src is not IMajorRecordGetter getter)
            return $"{fk} is not an overridable record.";

        try
        {
            if (!AddOverride(patch, getter))
                return $"This record type ({getter.Registration.Name}) cannot be overridden yet.";
            MutagenLoader.InvalidateModIndex(patchPlugin);
            NotifyChanged(patchPlugin);
            return $"Copied {winningPlugin}'s version of {fk} into {patchPlugin} as an override. " +
                   $"Ensure {patchPlugin} loads after the conflicting plugins, then save it.";
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
        var setterType = getter.Registration.SetterType;
        var getterType = getter.Registration.GetterType;

        foreach (var prop in mod.GetType().GetProperties(BindingFlags.Public | BindingFlags.Instance))
        {
            var pt = prop.PropertyType;
            if (!pt.IsGenericType) continue;
            var args = pt.GetGenericArguments();
            if (args.Length != 1 || args[0] != setterType) continue;

            var group = prop.GetValue(mod);
            if (group == null) continue;

            _groupOverrideMixin.MakeGenericMethod(setterType, getterType)
                .Invoke(null, new[] { group, getter });
            return true;
        }
        return false;
    }

    /// <summary>Plugins currently open for editing (candidates for a conflict patch target).</summary>
    public static IReadOnlyList<string> EditablePlugins() => _mutable.Keys.ToList();

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

        // Dotted path -> navigate into nested objects (e.g. "Model.File"), creating
        // missing intermediate objects (like a null Model) where possible.
        if (field.Contains('.'))
        {
            var parts = field.Split('.');
            object cur = rec;
            for (int i = 0; i < parts.Length - 1; i++)
            {
                var p = cur.GetType().GetProperty(parts[i],
                    BindingFlags.Public | BindingFlags.Instance | BindingFlags.IgnoreCase);
                if (p == null) { msg = $"No field '{parts[i]}' on {cur.GetType().Name}."; return false; }
                var next = p.CanRead ? p.GetValue(cur) : null;
                if (next == null)
                {
                    var pt = Nullable.GetUnderlyingType(p.PropertyType) ?? p.PropertyType;
                    try { next = System.Activator.CreateInstance(pt); } catch { next = null; }
                    if (next == null || !p.CanWrite) { msg = $"Can't initialise '{parts[i]}'."; return false; }
                    p.SetValue(cur, next);
                }
                cur = next;
            }
            return SetLeaf(cur, parts[^1], value, env, out msg);
        }

        return SetLeaf(rec, field, value, env, out msg);
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
            else if (t == typeof(bool)) converted = bool.Parse(value);
            else if (t == typeof(float)) converted = float.Parse(value);
            else if (t == typeof(double)) converted = double.Parse(value);
            else if (t == typeof(int)) converted = int.Parse(value);
            else if (t == typeof(uint)) converted = uint.Parse(value);
            else if (t == typeof(short)) converted = short.Parse(value);
            else if (t == typeof(ushort)) converted = ushort.Parse(value);
            else if (t == typeof(byte)) converted = byte.Parse(value);
            else if (t == typeof(long)) converted = long.Parse(value);
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
