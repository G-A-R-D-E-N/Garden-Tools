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
