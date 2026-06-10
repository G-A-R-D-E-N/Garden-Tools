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

    public static string DefaultOutputDir =>
        Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "Output");

    public static IFallout4Mod? GetMutable(string name) =>
        _mutable.TryGetValue(name, out var m) ? m : null;

    private static void Register(string name, IFallout4Mod mod)
    {
        _mutable[name] = mod;
        MutagenLoader.LooseMods[name] = mod;     // make it readable by the read tools
        MutagenLoader.InvalidateModIndex(name);
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
        return $"Created new plugin '{name}'. Add records with create_record, then call save_plugin to write it.";
    }

    public static string CreateRecord(string plugin, string sig, string editorId)
    {
        var mod = GetMutable(plugin);
        if (mod == null)
            return $"Plugin '{plugin}' is not open for editing. Call create_plugin first (existing-plugin editing comes later).";
        if (string.IsNullOrWhiteSpace(editorId)) return "Provide an editorId for the new record.";

        var rec = AddNewBySig(mod, sig, editorId);
        if (rec == null)
            return $"Record type '{sig}' is not supported for creation yet. Supported: " + SupportedTypes;

        MutagenLoader.InvalidateModIndex(plugin);
        return $"Created {sig} '{editorId}' [{rec.FormKey}] in {plugin}. Set fields with set_field, then save_plugin.";
    }

    public static string SetField(string plugin, string recordId, string field, string value)
    {
        var mod = GetMutable(plugin);
        if (mod == null) return $"Plugin '{plugin}' is not open for editing.";

        var rec = FindMutableRecord(mod, recordId);
        if (rec == null) return $"Record '{recordId}' not found in {plugin}.";

        if (TrySetScalar(rec, field, value, out var msg))
        {
            MutagenLoader.InvalidateModIndex(plugin);
            return $"Set {field} = '{value}' on {recordId} in {plugin}.";
        }
        return msg;
    }

    public static string SavePlugin(string plugin, string? path)
    {
        var mod = GetMutable(plugin);
        if (mod == null) return $"Plugin '{plugin}' is not open for editing.";

        path ??= Path.Combine(DefaultOutputDir, plugin);
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            mod.WriteToBinary(path, new BinaryWriteParameters
            {
                // Build the master list from the records' FormLinks (self-contained, no load order needed).
                MastersListContent = MastersListContentOption.Iterate
            });
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

    private static IFallout4MajorRecord? AddNewBySig(IFallout4Mod mod, string sig, string editorId) =>
        sig.ToUpperInvariant() switch
        {
            "BOOK" or "HOLOTAPE"  => mod.Books.AddNew(editorId),
            "TERM"                => mod.Terminals.AddNew(editorId),
            "WEAP"                => mod.Weapons.AddNew(editorId),
            "ARMO"                => mod.Armors.AddNew(editorId),
            "MISC"                => mod.MiscItems.AddNew(editorId),
            "COBJ"                => mod.ConstructibleObjects.AddNew(editorId),
            "KYWD"                => mod.Keywords.AddNew(editorId),
            "AMMO"                => mod.Ammunitions.AddNew(editorId),
            "ALCH"                => mod.Ingestibles.AddNew(editorId),
            "ACTI"                => mod.Activators.AddNew(editorId),
            "CONT"                => mod.Containers.AddNew(editorId),
            "FLST"                => mod.FormLists.AddNew(editorId),
            "MGEF"                => mod.MagicEffects.AddNew(editorId),
            "PERK"                => mod.Perks.AddNew(editorId),
            "NPC_"                => mod.Npcs.AddNew(editorId),
            "QUST"                => mod.Quests.AddNew(editorId),
            "MESG"                => mod.Messages.AddNew(editorId),
            _                     => null
        };

    private static IFallout4MajorRecord? FindMutableRecord(IFallout4Mod mod, string id)
    {
        var all = mod.EnumerateMajorRecords().Cast<IFallout4MajorRecordGetter>().ToList();
        IFallout4MajorRecordGetter? hit = null;
        if (FormKey.TryFactory(id, out var fk))
            hit = all.FirstOrDefault(r => r.FormKey == fk);
        hit ??= all.FirstOrDefault(r => string.Equals(r.EditorID, id, StringComparison.OrdinalIgnoreCase));
        return hit as IFallout4MajorRecord;
    }

    private static bool TrySetScalar(object rec, string field, string value, out string msg)
    {
        msg = "";
        var prop = rec.GetType().GetProperty(field,
            BindingFlags.Public | BindingFlags.Instance | BindingFlags.IgnoreCase);
        if (prop == null) { msg = $"No field '{field}' on {rec.GetType().Name}."; return false; }
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
