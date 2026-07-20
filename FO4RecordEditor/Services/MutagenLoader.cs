using System.Collections;
using System.IO;
using System.Reflection;
using FO4RecordEditor.Models;
using Mutagen.Bethesda;
using Mutagen.Bethesda.Fallout4;
using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Plugins.Records;
using Mutagen.Bethesda.Environments;

namespace FO4RecordEditor.Services;

/// <summary>
/// Loads an ESP/ESM/ESL directly via Mutagen -- no Spriggit step needed.
/// Uses a generic reflection walker so it's not tied to specific Mutagen API shapes.
/// </summary>
public static class MutagenLoader
{
    // Properties to skip at any depth (noisy / internal / circular)
    private static readonly HashSet<string> _skipProps = new(StringComparer.OrdinalIgnoreCase)
    {
        "Registration", "IsCompressed", "IsDeleted", "FormVersion",
        "VersionControl", "StaticRegistration", "ProtocolDefinition",
        "CustomData", "BinaryWriteTranslator", "RecordType", "ContainedFormLinks",
        // Framework metadata noise -- not useful for editing, already covered by the
        // FormKey/EditorID/Type header rows.
        "Fallout4MajorRecordFlags", "MajorRecordFlags", "MajorRecordFlagsRaw",
        "Version2", "MajorFlags", "IsNull", "FormVersion2",
    };

    private static readonly HashSet<Type> _leafTypes = new()
    {
        typeof(string), typeof(bool), typeof(byte), typeof(sbyte),
        typeof(short), typeof(ushort), typeof(int), typeof(uint),
        typeof(long), typeof(ulong), typeof(float), typeof(double),
    };

    // Concurrent: the UI thread and the AI agent thread read/write these at the same time (a tool
    // call edits while the tree reads). Plain Dictionaries threw "concurrent update corrupted state".
    public static readonly System.Collections.Concurrent.ConcurrentDictionary<string, object> LooseMods = new(StringComparer.OrdinalIgnoreCase);

    // Tracks file paths for loose-opened ESPs so OpenPlugin can reload them as mutable.
    public static readonly System.Collections.Concurrent.ConcurrentDictionary<string, string> LooseModPaths = new(StringComparer.OrdinalIgnoreCase);

    // Plugins the AI has opened for editing (mutable). These take priority over the env /
    // loose read-only copies for ALL reads, so edits are immediately visible in the tree and
    // to get_record (hot loading). WriteService registers entries here.
    public static readonly System.Collections.Concurrent.ConcurrentDictionary<string, object> EditableMods = new(StringComparer.OrdinalIgnoreCase);

    // Link cache for the currently loaded environment. Set when an env is built (env load /
    // MO2 profile). Used to resolve FormLinks to human-readable names (EditorID + FULL name)
    // in the field walker, so the UI shows `c_Gears "Gear" [...]` instead of a bare FormID.
    public static Mutagen.Bethesda.Plugins.Cache.ILinkCache? LinkCache;

    // Per-plugin ESL ("small master") flag for the loaded load order, keyed by file name. Populated
    // when the env is built. Used to compute the correct master-mapped FormID (regular high-byte vs
    // 0xFE light-master encoding) when fixing custom-ActorValue condition parameters on save.
    public static readonly System.Collections.Generic.Dictionary<string, bool> MasterIsEsl =
        new(System.StringComparer.OrdinalIgnoreCase);

    // Cache of the per-type "Name" property (FULL) used to label resolved FormLinks.
    private static readonly System.Collections.Concurrent.ConcurrentDictionary<Type, PropertyInfo?> _nameProp = new();

    /// <summary>
    /// Render a FormLink as xEdit-style `EditorID "FullName" [FormID:File]`, resolving the
    /// target through the load order's link cache. Falls back to the raw FormKey when the
    /// target can't be resolved (no env loaded, or a dangling/unmastered reference).
    /// </summary>
    internal static string FormatFormLink(Mutagen.Bethesda.Plugins.IFormLinkIdentifier fli)
    {
        var fk = fli.FormKey;
        if (fk.IsNull) return "Null";
        var fkStr = fk.ToString();

        var cache = LinkCache;
        if (cache == null) return fkStr;
        try
        {
            if (!cache.TryResolve<IMajorRecordGetter>(fk, out var rec)) return fkStr;

            var eid = rec.EditorID ?? "";
            string name = rec is Mutagen.Bethesda.Plugins.Aspects.INamedGetter named ? named.Name ?? "" : "";
            if (name.Length == 0)
            {
                var np = _nameProp.GetOrAdd(rec.GetType(), t => t.GetProperty("Name"));
                if (np != null) name = np.GetValue(rec)?.ToString() ?? "";
            }

            var label = (eid, name) switch
            {
                ("", "") => "",
                (_, "")  => eid,
                ("", _)  => $"\"{name}\"",
                _        => $"{eid} \"{name}\"",
            };
            return label.Length > 0 ? $"{label} [{fkStr}]" : fkStr;
        }
        catch { return fkStr; }
    }

    // Every concrete Fallout 4 record class (Registration.Name -> the getter interface), reflected
    // once from the Mutagen assembly. Used to resolve which record types a FormLink may point at,
    // including broad interfaces (IItemGetter -> Weapon/Armor/Ingestible/...).
    private static readonly Lazy<List<(string Name, Type Type)>> _fo4Records = new(() =>
    {
        var list = new List<(string, Type)>();
        var seen = new HashSet<string>(StringComparer.Ordinal);
        var major = typeof(Mutagen.Bethesda.Plugins.Records.IMajorRecordGetter);
        Type[] types;
        try { types = typeof(Mutagen.Bethesda.Fallout4.IFallout4ModGetter).Assembly.GetTypes(); }
        catch (ReflectionTypeLoadException ex) { types = ex.Types.Where(t => t != null).ToArray()!; }
        foreach (var t in types)
        {
            if (t is null || !t.IsClass || t.IsAbstract) continue;
            if (!major.IsAssignableFrom(t)) continue;       // a concrete record class (e.g. Weapon)
            if (seen.Add(t.Name)) list.Add((t.Name, t));
        }
        return list;
    });

    private static readonly System.Collections.Concurrent.ConcurrentDictionary<Type, string> _refTypesCache = new();

    // Resolve a FormLink property type to (displayLabel, csvOfTargetRecordClasses). The csv is every
    // concrete record class assignable to the link's target interface, so the picker filter is
    // correct for both single-type links (Keyword) and broad ones (an "item" link).
    private static (string? Display, string? Csv) FormLinkInfo(Type t)
    {
        foreach (var cand in new[] { t }.Concat(t.GetInterfaces()))
        {
            if (!cand.IsGenericType) continue;
            var args = cand.GetGenericArguments();
            if (args.Length != 1) continue;
            var arg = args[0];
            if (!typeof(Mutagen.Bethesda.Plugins.Records.IMajorRecordGetter).IsAssignableFrom(arg)) continue;

            var n = arg.Name;                                   // "IKeywordGetter"
            if (n.StartsWith("I", StringComparison.Ordinal)) n = n[1..];
            if (n.EndsWith("Getter", StringComparison.Ordinal)) n = n[..^6];

            var csv = _refTypesCache.GetOrAdd(arg, target =>
                string.Join(",", _fo4Records.Value
                    .Where(r => target.IsAssignableFrom(r.Type))
                    .Select(r => r.Name)
                    .OrderBy(x => x, StringComparer.Ordinal)));
            return (n.Length > 0 ? n : null, string.IsNullOrEmpty(csv) ? null : csv);
        }
        return (null, null);
    }

    // ---- per-mod record index (built once, reused for every group/record load) ----
    // Without this, every group expansion re-enumerated the ENTIRE mod (O(allRecords)
    // per group), which hangs on large masters like Fallout4.esm.
    private sealed class ModIndex
    {
        public readonly Dictionary<string, List<Mutagen.Bethesda.Plugins.Records.IMajorRecordGetter>> BySig =
            new(StringComparer.Ordinal);
        public readonly Dictionary<FormKey, Mutagen.Bethesda.Plugins.Records.IMajorRecordGetter> ByFormKey = new();
        // The exact mod instance this index was built from. Cached entries are keyed by file name,
        // but a plugin's backing instance is REPLACED on reload_plugin / open / env refresh (the
        // editable in-memory copy supersedes the env/loose copy). If we served the cache by name
        // alone, a stale index built from a pre-edit (or empty deployed) instance would be returned
        // forever — the silent cause of list_records/get_record showing 0 records while
        // check_plugin (which enumerates directly) correctly saw them. Rebuilding when Source no
        // longer matches the resolved mod makes the cache self-healing across every replace path.
        public object? Source;
    }

    private static readonly System.Collections.Concurrent.ConcurrentDictionary<string, ModIndex> _modIndexCache = new(StringComparer.OrdinalIgnoreCase);

    // Resolve a mod by file name. Editable (mutable) copies win so the AI's in-progress
    // edits are what everything reads (hot loading), then the env load order, then loose ESPs.
    private static object? ResolveMod(string modName, object? envObj)
    {
        if (EditableMods.TryGetValue(modName, out var editable)) return editable;

        if (envObj != null)
        {
            dynamic env = envObj;
            foreach (var l in ((IEnumerable)env.LoadOrder.ListedOrder).Cast<dynamic>())
            {
                if (l.Mod != null &&
                    string.Equals((string)l.ModKey.FileName.String, modName, StringComparison.OrdinalIgnoreCase))
                    return l.Mod;
            }
        }
        return LooseMods.TryGetValue(modName, out var loose) ? loose : null;
    }

    // Every loaded mod (editable copies win over env/loose), de-duplicated by name.
    private static IEnumerable<(string name, object mod)> AllLoadedMods(object? envObj)
    {
        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var kv in EditableMods)
            if (seen.Add(kv.Key)) yield return (kv.Key, kv.Value);

        if (envObj != null)
        {
            dynamic env = envObj;
            foreach (var l in ((IEnumerable)env.LoadOrder.ListedOrder).Cast<dynamic>())
            {
                if (l.Mod == null) continue;
                var name = (string)l.ModKey.FileName.String;
                if (seen.Add(name)) yield return (name, (object)l.Mod);
            }
        }
        foreach (var kv in LooseMods)
            if (seen.Add(kv.Key)) yield return (kv.Key, kv.Value);
    }

    // Enumerate a mod exactly once, bucketing records by signature and FormKey. Cached per mod.
    private static ModIndex GetModIndex(object mod, string modName, Action<string>? progress = null)
    {
        // Serve the cache only when it was built from the SAME mod instance we're resolving now.
        // A name-only hit would hand back a stale index after the backing instance was replaced
        // (reload_plugin, open_plugin, env refresh, or an editable copy superseding the env copy).
        if (_modIndexCache.TryGetValue(modName, out var cached) && ReferenceEquals(cached.Source, mod))
            return cached;

        var idx = new ModIndex { Source = mod };
        if (mod is Mutagen.Bethesda.Fallout4.IFallout4ModGetter f4mod)
        {
            int count = 0;
            foreach (var rec in f4mod.EnumerateMajorRecords())
            {
                var sig = rec.Registration.Name;
                if (!idx.BySig.TryGetValue(sig, out var list)) { list = new(); idx.BySig[sig] = list; }
                list.Add(rec);
                idx.ByFormKey[rec.FormKey] = rec;
                if (++count % 5000 == 0) progress?.Invoke($"Indexing {modName}: {count} records...");
            }
        }
        _modIndexCache[modName] = idx;
        return idx;
    }

    public static void InvalidateModIndex(string modName) => _modIndexCache.TryRemove(modName, out _);

    public static RecordNode LoadEsp(string espPath, IProgress<(string message, double? percent)>? progress = null)
    {
        var fileName = Path.GetFileName(espPath);
        var root = new RecordNode { Key = fileName, FilePath = espPath };
        progress?.Report(($"Opening {fileName}...", 0));

        var modPath = ModPath.FromPath(espPath);
        var mod = Fallout4Mod.CreateFromBinaryOverlay(modPath, Fallout4Release.Fallout4);

        // Store the loose mod so it can be queried by GetGroups and PopulateNode
        LooseMods[fileName] = mod;
        LooseModPaths[fileName] = espPath;
        InvalidateModIndex(fileName);   // drop any stale index from a prior load of this file

        var dummy = new RecordNode { Key = "Loading...", Parent = root };
        AddLeafInit(dummy, "_NeedsGroups", fileName);
        root.Children.Add(dummy);

        progress?.Report(($"Loaded loose plugin {fileName}.", 100));
        return root;
    }

    // Build a lazy Explorer tree node (root + "_NeedsGroups" dummy) for a plugin that is
    // already registered in LooseMods (e.g. one the AI created/opened for editing).
    public static RecordNode MakeLazyNode(string pluginName, string? filePath = null)
    {
        var root = new RecordNode { Key = pluginName, FilePath = filePath };
        var dummy = new RecordNode { Key = "Loading...", Parent = root };
        AddLeafInit(dummy, "_NeedsGroups", pluginName);
        root.Children.Add(dummy);
        return root;
    }

    // Build the game environment and list its plugins WITHOUT creating tree nodes, so the
    // caller can let the user choose which plugins to load.
    public static (object env, List<string> plugins) BuildEnvironment(
        IProgress<(string message, double? percent)>? progress = null,
        string? dataFolderOverride = null)
    {
        progress?.Report(("Initializing Game Environment...", 0));
        _modIndexCache.Clear();
        var builder = GameEnvironment.Typical.Builder<IFallout4Mod, IFallout4ModGetter>(GameRelease.Fallout4);
        if (!string.IsNullOrWhiteSpace(dataFolderOverride))
        {
            progress?.Report(($"Using data folder: {dataFolderOverride}", null));
            builder = builder.WithTargetDataFolder(new Noggog.DirectoryPath(dataFolderOverride));
        }
        var env = builder.Build();
        LinkCache = env.LinkCache;

        var plugins = new List<string>();
        MasterIsEsl.Clear();
        foreach (var l in env.LoadOrder.ListedOrder)
            if (l.Mod != null)
            {
                plugins.Add(l.ModKey.FileName.String);
                MasterIsEsl[l.ModKey.FileName.String] = l.Mod.IsSmallMaster;
            }

        progress?.Report(($"Found {plugins.Count} plugins.", null));
        return (env, plugins);
    }

    public static (RecordNode root, object env) LoadEnvironment(IProgress<(string message, double? percent)>? progress = null)
    {
        progress?.Report(("Initializing Game Environment...", 0));
        _modIndexCache.Clear();   // a fresh environment invalidates all cached per-mod indexes
        var env = GameEnvironment.Typical.Builder<IFallout4Mod, IFallout4ModGetter>(GameRelease.Fallout4).Build();
        
        var root = new RecordNode { Key = "Load Order" };
        var listed = env.LoadOrder.ListedOrder.ToList();
        
        for (int i = 0; i < listed.Count; i++)
        {
            var modListing = listed[i];
            var mod = modListing.Mod;
            if (mod == null) continue;

            double percent = (double)(i + 1) / listed.Count * 100;
            progress?.Report(($"Loading {mod.ModKey.FileName} ({i + 1}/{listed.Count})...", percent));

            var modNode = new RecordNode { Key = mod.ModKey.FileName.String, Parent = root };
            // Add a dummy node to allow expansion
            var dummy = new RecordNode { Key = "Loading...", Parent = modNode };
            AddLeafInit(dummy, "_NeedsGroups", mod.ModKey.FileName.String);
            modNode.Children.Add(dummy);

            root.Children.Add(modNode);
        }

        progress?.Report(($"Done -- Loaded {listed.Count} plugins.", null));
        return (root, env);
    }

    public static List<RecordNode> GetGroups(string modName, object? envObj, RecordNode pluginNode, Action<string>? progress = null)
    {
        var mod = ResolveMod(modName, envObj);
        if (mod == null) return [];

        progress?.Invoke("Enumerating records...");
        var idx = GetModIndex(mod, modName, progress);

        var result = new List<RecordNode>();
        foreach (var sig in idx.BySig.Keys.OrderBy(g => g))
        {
            var grpNode = new RecordNode { Key = sig, Parent = pluginNode };
            var grpDummy = new RecordNode { Key = "Loading...", Parent = grpNode };
            AddLeafInit(grpDummy, "_NeedsRecords", $"{modName}|{sig}");
            grpNode.Children.Add(grpDummy);
            result.Add(grpNode);
        }
        if (result.Count == 0)
        {
            var noneNode = new RecordNode { Key = "(No records)", Parent = pluginNode };
            result.Add(noneNode);
        }
        return result;
    }

    public static List<RecordNode> GetRecords(string modName, string sig, object? envObj, RecordNode groupNode, Action<string>? progress = null)
    {
        var mod = ResolveMod(modName, envObj);
        if (mod == null) return [];

        progress?.Invoke("Enumerating records...");
        var idx = GetModIndex(mod, modName, progress);

        var result = new List<RecordNode>();
        if (!idx.BySig.TryGetValue(sig, out var recs)) return result;

        foreach (var rec in recs)
        {
            string editorId = rec.EditorID ?? "";
            var recNode = new RecordNode
            {
                Key = !string.IsNullOrEmpty(editorId) ? editorId : rec.FormKey.ToString(),
                Parent = groupNode,
                IsRecordNode = true,   // a leaf in the tree; fields show in the center grid
            };
            AddLeafInit(recNode, "FormKey", rec.FormKey.ToString());
            AddLeafInit(recNode, "EditorID", editorId);
            AddLeafInit(recNode, "Type", sig);
            AddLeafInit(recNode, "_HasData", "False");
            recNode.ConflictStatus = ConflictState.GetStatus(modName, rec.FormKey);
            result.Add(recNode);
        }
        return result;
    }

    public static void PopulateNode(RecordNode node, object? envObj, string modName = "")
    {
        // Remove the lazy-load flag so we don't try to populate again.
        var flag = node.GetChild("_HasData");
        if (flag != null) node.Children.Remove(flag);

        var fkStr = node.GetValue("FormKey");
        if (fkStr == null || !FormKey.TryFactory(fkStr, out var formKey)) return;

        // Use the plugin the record is listed under; fall back to the FormKey's master.
        if (string.IsNullOrEmpty(modName)) modName = formKey.ModKey.FileName.String;

        var mod = ResolveMod(modName, envObj);
        if (mod == null) return;

        // O(1) lookup against the index built when the group was expanded -- no link-cache
        // build, no full re-enumeration, so this is safe to call synchronously.
        var idx = GetModIndex(mod, modName);
        if (idx.ByFormKey.TryGetValue(formKey, out var rec))
            WalkObject(rec, node, 0, 4, modName);
    }

    // Populate a record node with EVERY plugin's version of the record (xEdit-style),
    // so each field leaf accumulates a value per plugin (Values[pluginName]). Returns the
    // plugins that touch the record in load order (last = current winner). Falls back to the
    // single listed plugin when no environment / only one version exists.
    public static List<string> PopulateNodeAllVersions(RecordNode node, object? envObj)
    {
        var flag = node.GetChild("_HasData");
        if (flag != null) node.Children.Remove(flag);

        var fkStr = node.GetValue("FormKey");
        if (fkStr == null || !FormKey.TryFactory(fkStr, out var fk)) return new();

        var versions = GetRecordContexts(envObj, fk);

        if (versions.Count == 0)
        {
            // No load order (e.g. a loosely opened ESP): keep the legacy single-plugin walk.
            var modName = node.Parent?.Parent?.Key ?? fk.ModKey.FileName.String;
            var mod = ResolveMod(modName, envObj);
            if (mod != null && GetModIndex(mod, modName).ByFormKey.TryGetValue(fk, out var rec))
                WalkObject(rec, node, 0, 4, modName);
            return new List<string> { modName };
        }

        foreach (var (plugin, rec) in versions)
            WalkObject(rec, node, 0, 4, plugin);
        return versions.Select(v => v.plugin).ToList();
    }

    // Build a record's field subtree on a DETACHED node (safe to run on a background
    // thread because nothing is bound to it yet). The caller attaches the returned
    // children to the real, tree-bound record node on the UI thread. This keeps large
    // records (NPC_, etc.) from freezing the UI during the reflection walk.
    public static List<RecordNode> BuildPopulatedFields(string formKeyStr, object? envObj, string modName)
    {
        var temp = new RecordNode { Key = "temp" };
        if (!FormKey.TryFactory(formKeyStr, out var formKey)) return [];
        if (string.IsNullOrEmpty(modName)) modName = formKey.ModKey.FileName.String;

        var mod = ResolveMod(modName, envObj);
        if (mod == null) return [];

        var idx = GetModIndex(mod, modName);
        if (idx.ByFormKey.TryGetValue(formKey, out var rec))
            WalkObject(rec, temp, 0, 4, modName);

        return temp.Children.ToList();
    }

    // ============================================================================
    //  Conflict field matrix — xEdit-style per-plugin/per-field view of one record.
    // ============================================================================

    /// <summary>The override version of a record carried by a specific plugin (or null).</summary>
    public static Mutagen.Bethesda.Plugins.Records.IMajorRecordGetter? GetRecordVersion(
        object? envObj, string plugin, FormKey fk)
    {
        foreach (var (p, rec) in GetRecordContexts(envObj, fk))
            if (string.Equals(p, plugin, StringComparison.OrdinalIgnoreCase)) return rec;
        return null;
    }

    /// <summary>Plugins (in load order) that carry a version of the given FormKey.</summary>
    public static List<string> GetConflictingPlugins(object? envObj, FormKey fk) =>
        GetRecordContexts(envObj, fk).Select(c => c.plugin).ToList();

    public sealed record RefByDto(string Plugin, string FormKey, string EditorID, string Type);

    /// <summary>Records across the load order whose contained FormLinks point at the given FormKey
    /// (xEdit's "Referenced By"). Capped to keep an on-demand scan responsive.</summary>
    public static List<RefByDto> GetReferencedBy(object? envObj, string formKeyStr, int cap = 500)
    {
        var result = new List<RefByDto>();
        if (envObj == null || !FormKey.TryFactory(formKeyStr, out var target)) return result;

        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var (name, mod) in AllLoadedMods(envObj))
        {
            if (mod is not Mutagen.Bethesda.Fallout4.IFallout4ModGetter f4) continue;
            foreach (var rec in f4.EnumerateMajorRecords())
            {
                if (rec.FormKey == target) continue;   // a record referencing itself isn't interesting
                bool refs = false;
                foreach (var link in rec.EnumerateFormLinks())
                    if (link.FormKey == target) { refs = true; break; }
                if (!refs) continue;

                if (!seen.Add(name + "|" + rec.FormKey)) continue;
                result.Add(new RefByDto(name, rec.FormKey.ToString(), rec.EditorID ?? "", rec.Registration.Name));
                if (result.Count >= cap) return result;
            }
        }
        return result;
    }

    public sealed record ProblemDto(string Severity, string Description);

    /// <summary>Lightweight per-record problems for the winning version: deleted flag and any
    /// FormLink that doesn't resolve anywhere in the load order (a dangling, crash-risk reference).</summary>
    public static List<ProblemDto> GetRecordProblems(object? envObj, string formKeyStr)
    {
        var result = new List<ProblemDto>();
        if (envObj == null || !FormKey.TryFactory(formKeyStr, out var fk)) return result;

        var ctx = GetRecordContexts(envObj, fk);
        if (ctx.Count == 0) return result;
        var rec = ctx[^1].rec;   // the winning version is what the game uses

        if (rec.IsDeleted)
            result.Add(new ProblemDto("Error", "Record is flagged DELETED — prefer a disable/override; deletions can crash the game."));

        var cache = LinkCache;
        var seen = new HashSet<FormKey>();
        foreach (var link in rec.EnumerateFormLinks())
        {
            if (link.FormKey.IsNull || !seen.Add(link.FormKey)) continue;
            if (cache != null && !cache.TryResolveIdentifier<IMajorRecordGetter>(link.FormKey, out _))
                result.Add(new ProblemDto("Error", $"Unresolved reference {link.FormKey} — target not found in the load order (crash risk)."));
        }
        return result;
    }

    // 4-letter signature -> Mutagen record class name, for filtering EnumerateOverrides by type.
    private static readonly Dictionary<string, string> _sigToClass = new(StringComparer.OrdinalIgnoreCase)
    {
        ["COBJ"] = "ConstructibleObject", ["WEAP"] = "Weapon", ["ARMO"] = "Armor", ["ALCH"] = "Ingestible",
        ["MISC"] = "MiscItem", ["FLST"] = "FormList", ["LVLI"] = "LeveledItem", ["AMMO"] = "Ammunition",
    };

    /// <summary>
    /// Every record a plugin carries as an OVERRIDE (the FormKey belongs to another mod), optionally
    /// filtered to one signature (e.g. "COBJ"). Used to batch-revert a plugin's edits.
    /// </summary>
    public static List<(FormKey fk, string editorId, string sig)> EnumerateOverrides(
        object? envObj, string plugin, string? sig = null)
    {
        var result = new List<(FormKey, string, string)>();
        if (ResolveMod(plugin, envObj) is not Mutagen.Bethesda.Plugins.Records.IModGetter mod) return result;

        string? wantClass = sig == null ? null
            : (_sigToClass.TryGetValue(sig, out var c) ? c : sig);

        foreach (var rec in mod.EnumerateMajorRecords())
        {
            if (rec.FormKey.ModKey == mod.ModKey) continue;   // its own new record, not an override
            var rsig = rec.Registration.Name;
            if (wantClass != null && !string.Equals(rsig, wantClass, StringComparison.OrdinalIgnoreCase)) continue;
            result.Add((rec.FormKey, rec.EditorID ?? "", rsig));
        }
        return result;
    }

    /// <summary>True if a COBJ getter's Components reference the given component FormKey (used to
    /// target exactly the records carrying a specific bad ingredient like hubflower).</summary>
    public static bool CobjUsesComponent(Mutagen.Bethesda.Plugins.Records.IMajorRecordGetter rec, FormKey component)
    {
        if (rec is not Mutagen.Bethesda.Fallout4.IConstructibleObjectGetter cobj || cobj.Components == null) return false;
        foreach (var c in cobj.Components)
            if (c.Component.FormKey == component) return true;
        return false;
    }

    /// <summary>
    /// Every version of a record across the load order, in load order (last = winner). Uses the
    /// environment's link cache (already built, efficient, handles cells/worldspace children) so we
    /// never have to fully index the masters just to find one record's overrides.
    /// </summary>
    public static List<(string plugin, Mutagen.Bethesda.Plugins.Records.IMajorRecordGetter rec)>
        GetRecordContexts(object? envObj, FormKey fk)
    {
        var ordered = new List<(string plugin, Mutagen.Bethesda.Plugins.Records.IMajorRecordGetter rec)>();
        // The on-disk load order comes from the env's link cache; with no env (CLI / unit tests /
        // a session that only authored patches) we skip it and rely solely on the EditableMods scan
        // below, so in-editor patches are still resolvable.
        if (envObj != null)
        {
            try
            {
                var lc = (Mutagen.Bethesda.Plugins.Cache.ILinkCache)((dynamic)envObj).LinkCache;
                // ResolveAllSimpleContexts returns winning-first; reverse to load order.
                var contexts = lc.ResolveAllSimpleContexts<IMajorRecordGetter>(fk).ToList();
                contexts.Reverse();
                foreach (var ctx in contexts)
                    ordered.Add((ctx.ModKey.FileName.String, ctx.Record));
            }
            catch { }
        }

        // Hot-load AI/in-editor patches: any EditableMods plugin that carries this FormKey is a LIVE
        // override applied on top of the on-disk load order. If that plugin is already in the env, swap
        // in the edited record (position preserved); otherwise it's a new patch that wins (loads last).
        // This is why an AI patch shows up in the conflict view / get_conflicts immediately, without
        // the user reloading the modlist.
        foreach (var kv in EditableMods)
        {
            try
            {
                var idx = GetModIndex(kv.Value, kv.Key);
                if (!idx.ByFormKey.TryGetValue(fk, out var rec)) continue;
                int at = ordered.FindIndex(o => string.Equals(o.plugin, kv.Key, StringComparison.OrdinalIgnoreCase));
                if (at >= 0) ordered[at] = (kv.Key, rec);
                else ordered.Add((kv.Key, rec));
            }
            catch { }
        }
        return ordered;
    }

    /// <summary>
    /// Build the field-level conflict matrix for one record: walk every plugin's version into a
    /// shared node tree (each leaf accumulates a value per plugin) then flatten to rows.
    /// </summary>
    public static ConflictMatrix? BuildConflictMatrix(object? envObj, string formKeyStr)
    {
        if (envObj == null || !FormKey.TryFactory(formKeyStr, out var fk)) return null;

        var versions = GetRecordContexts(envObj, fk);
        if (versions.Count == 0) return null;

        var root = new RecordNode { Key = "root" };
        foreach (var (plugin, rec) in versions)
            WalkObject(rec, root, 0, 6, plugin);

        var plugins = versions.Select(v => v.plugin).ToList();
        var rows = new List<ConflictFieldRow>();
        FlattenConflictRows(root, "", 0, plugins, rows);

        // Header identity fields first, in xEdit-ish order; Description sits right after FormKey.
        var identityKeys = new[] { "EditorID", "TitleString", "FormKey", "Description", "Type", "_HasData" };
        var orderedRows = rows.OrderBy(r => {
            int idx = Array.IndexOf(identityKeys, r.Field);
            return idx == -1 ? 999 : idx;
        }).ToList();

        // Record-level ConflictAll rollup: only one version -> "onlyone"; otherwise worst severity.
        string level;
        if (plugins.Count <= 1) level = "onlyone";
        else if (orderedRows.Any(r => r.Severity == "critical")) level = "critical";
        else if (orderedRows.Any(r => r.Severity == "conflict")) level = "conflict";
        else if (orderedRows.Any(r => r.Severity == "override")) level = "override";
        else level = "noconflict";

        var last = versions[^1];
        return new ConflictMatrix
        {
            FormKey = fk.ToString(),
            EditorID = last.rec.EditorID ?? "",
            Type = last.rec.Registration.Name,
            Plugins = plugins,
            Winner = last.plugin,
            Rows = orderedRows,
            Level = level,
        };
    }

    /// <summary>Human-friendly display label for a conflict row. Array-entry keys like "[0]"
    /// are shown as "Item [0]" (singular of parent collection name + index) so users see
    /// "Item [0]", "Condition [0]", "Component [0]" instead of raw "[0]".</summary>
    private static string MakeDisplayLabel(string key, string parentKey)
    {
        if (key.Length > 1 && key[0] == '[' && parentKey != "root")
            return $"{Rendering.FriendlyNames.Singular(parentKey)} {key}";
        return Rendering.FriendlyNames.Label(key);
    }

    private static void FlattenConflictRows(RecordNode node, string path, int depth, List<string> plugins, List<ConflictFieldRow> rows)
    {
        foreach (var child in node.Children)
        {
            string sep = child.Key.StartsWith("[") ? "" : (path.Length == 0 ? "" : ".");
            string p = path + sep + child.Key;
            bool hasKids = child.Children.Count > 0;
            // Emit a row for any field with a value OR any container (so arrays like Components and
            // Conditions get a labeled, collapsible PARENT row instead of orphaned [0]/[1] entries).
            if (child.Values.Count > 0 || hasKids)
            {
                var vals = plugins.Select(pl => child.Values.TryGetValue(pl, out var v) ? v : "").ToList();
                var present = vals.Where(v => v.Length > 0).ToList();
                bool anyMissing = present.Count < plugins.Count;
                // Differ check uses the SAME semantic canonicalization as ClassifyRow/Severity, so a
                // float-formatting difference ("1.0" vs "1.000000") is not flagged as a change here while
                // Severity calls it "none". Keeping the two in sync avoids a contradictory row.
                bool differs = present.Select(CanonValue).Distinct().Count() > 1 || (present.Count > 0 && anyMissing);
                var editKindString = child.EditKind.ToString();
                var (statuses, severity) = ClassifyRow(vals, editKindString);
                rows.Add(new ConflictFieldRow
                {
                    Field = p, DisplayLabel = MakeDisplayLabel(child.Key, node.Key), Level = depth, Values = vals, Differs = differs,
                    Statuses = statuses, Severity = severity,
                    IsSummary = child.IsSummary, HasChildren = hasKids,
                    EditKind = editKindString, EnumOptions = child.EnumOptions,
                    RefType = child.RefType, RefTypes = child.RefTypes,
                });
            }
            // Recurse into ALL children, including summary entries (conditions/components/effects), so
            // their sub-fields are present in the grid. The UI collapses a summary's children by
            // default and expands them on click — xEdit's "double-click the array element" behaviour.
            FlattenConflictRows(child, p, depth + 1, plugins, rows);
        }
    }

    // Canonicalize a value for semantic equality: numbers compare by value (so "1.0" == "1.000000"),
    // everything else compares ordinally. This is our lightweight stand-in for xEdit's DisplaySortKey.
    internal static string CanonValue(string v)
    {
        if (double.TryParse(v, System.Globalization.NumberStyles.Float | System.Globalization.NumberStyles.AllowThousands,
                System.Globalization.CultureInfo.InvariantCulture, out var d))
            return d.ToString("R", System.Globalization.CultureInfo.InvariantCulture); // canonical numeric form
        return v;
    }

    // Compute per-plugin conflict status (parallel to vals) and a row-level severity using semantic
    // equality. masterIdx = first present column, winnerIdx = last present column (load-order winner).
    internal static (string[] statuses, string severity) ClassifyRow(IReadOnlyList<string> vals, string editKind)
    {
        int n = vals.Count;
        var statuses = new string[n];
        // present columns (non-empty)
        var present = new List<int>();
        for (int i = 0; i < n; i++) if (!string.IsNullOrEmpty(vals[i])) present.Add(i);

        if (present.Count == 0) { for (int i = 0; i < n; i++) statuses[i] = "notdefined"; return (statuses, "none"); }

        int masterIdx = present[0];
        int winnerIdx = present[^1];
        string Canon(int i) => CanonValue(vals[i]);
        bool Eq(int a, int b) => string.Equals(Canon(a), Canon(b), StringComparison.Ordinal);

        // distinct semantic values among present
        var distinct = new HashSet<string>(StringComparer.Ordinal);
        foreach (var i in present) distinct.Add(Canon(i));
        bool anyMissing = present.Count < n;
        bool differs = distinct.Count > 1 || (present.Count > 0 && anyMissing);

        for (int i = 0; i < n; i++)
        {
            if (string.IsNullOrEmpty(vals[i])) { statuses[i] = "notdefined"; continue; }
            if (present.Count == 1) { statuses[i] = "only"; continue; }
            if (i == masterIdx) { statuses[i] = "master"; continue; }
            if (Eq(i, masterIdx)) { statuses[i] = "identical"; continue; }
            if (i == winnerIdx) { statuses[i] = "win"; continue; }
            statuses[i] = Eq(i, winnerIdx) ? "override" : "lose";
        }

        // Severity
        string severity;
        if (!differs) severity = "none";
        else
        {
            // critical: a FormLink (Ref) whose WINNING value is a null/broken target while another plugin
            // supplied a real one (a reference that resolves to nothing -> in-game break).
            bool refBrokenWinner = string.Equals(editKind, "Ref", StringComparison.OrdinalIgnoreCase)
                && LooksNullRef(vals[winnerIdx]) && present.Any(i => !LooksNullRef(vals[i]));
            if (refBrokenWinner) severity = "critical";
            else if (distinct.Count >= 3) severity = "conflict";
            else severity = "override";
        }
        return (statuses, severity);
    }

    private static bool LooksNullRef(string v) =>
        string.IsNullOrEmpty(v) || v.Equals("Null", StringComparison.OrdinalIgnoreCase)
        || v.Equals("None", StringComparison.OrdinalIgnoreCase) || v.StartsWith("000000:", StringComparison.OrdinalIgnoreCase);

    // ============================================================================
    //  Public query API for the AI tool executor (read all loaded plugin data).
    //  Each call is scoped to one plugin and served from the cached per-mod index,
    //  so the AI can drill into a full load order without ever indexing everything.
    // ============================================================================

    public static IReadOnlyList<string> QueryLoadedPlugins(object? envObj)
    {
        var names = new List<string>();
        if (envObj != null)
        {
            dynamic env = envObj;
            foreach (var l in ((IEnumerable)env.LoadOrder.ListedOrder).Cast<dynamic>())
                if (l.Mod != null) names.Add((string)l.ModKey.FileName.String);
        }
        foreach (var k in LooseMods.Keys)
            if (!names.Contains(k, StringComparer.OrdinalIgnoreCase)) names.Add(k);
        return names;
    }

    public static IReadOnlyList<string> QueryRecordTypes(object? envObj, string plugin)
    {
        var mod = ResolveMod(plugin, envObj);
        if (mod == null) return [];
        // Include per-type counts so the caller can gauge group size before listing.
        return GetModIndex(mod, plugin).BySig
            .OrderBy(kv => kv.Key)
            .Select(kv => $"{kv.Key} ({kv.Value.Count})")
            .ToList();
    }

    public static IReadOnlyList<(string formKey, string editorId)> QueryRecordsOfType(
        object? envObj, string plugin, string sig, int limit = 100, int offset = 0)
    {
        var mod = ResolveMod(plugin, envObj);
        if (mod == null) return [];
        var idx = GetModIndex(mod, plugin);
        if (!idx.BySig.TryGetValue(sig, out var recs)) return [];
        return recs.Skip(Math.Max(0, offset)).Take(limit).Select(r => (r.FormKey.ToString(), r.EditorID ?? "")).ToList();
    }

    /// <summary>Total record count for a signature in a plugin (for pagination headers).</summary>
    public static int CountRecordsOfType(object? envObj, string plugin, string sig)
    {
        var mod = ResolveMod(plugin, envObj);
        if (mod == null) return 0;
        var idx = GetModIndex(mod, plugin);
        return idx.BySig.TryGetValue(sig, out var recs) ? recs.Count : 0;
    }

    /// <summary>
    /// Return a compact tabular summary for up to <paramref name="limit"/> records of a given
    /// type — one line per record, key fields shown inline. Far cheaper than N QueryRecordFields
    /// calls when the agent needs to survey a whole group at once.
    /// </summary>
    public static string QueryRecordSummaries(object? envObj, string plugin, string sig, int limit, int offset = 0)
    {
        var mod = ResolveMod(plugin, envObj);
        if (mod == null) return ToolError.Fail($"Plugin '{plugin}' not found.");
        var idx = GetModIndex(mod, plugin);
        if (!idx.BySig.TryGetValue(sig, out var recs))
            return $"No records of type '{sig}' in {plugin}.";

        var take = recs.Skip(Math.Max(0, offset)).Take(limit).ToList();
        if (take.Count == 0) return $"No records of type '{sig}' in {plugin} at offset {offset}.";

        // Discover output columns from the first record (all same-type records share identical
        // property lists in Mutagen). Keep only scalar/enum/FormLink/collection properties.
        var rec0 = take[0];
        var colProps = new List<PropertyInfo>();
        foreach (var prop in rec0.GetType().GetProperties(BindingFlags.Public | BindingFlags.Instance))
        {
            if (_skipProps.Contains(prop.Name)) continue;
            if (prop.Name is "EditorID" or "FormKey") continue;
            if (prop.GetIndexParameters().Length > 0) continue;
            object? v; try { v = prop.GetValue(rec0); } catch { continue; }
            if (v == null) continue;
            var vt = v.GetType();
            bool keep = _leafTypes.Contains(vt) || vt.IsEnum
                || v is Mutagen.Bethesda.Plugins.IFormLinkIdentifier
                || (v is IEnumerable && vt != typeof(string));
            if (!keep) continue;
            colProps.Add(prop);
            if (colProps.Count >= 8) break;
        }

        // Budget-bound the output so the AI executor's ~8 KB hard cap never blind-truncates the
        // table mid-row (which would leave a misleading "N of M shown" header above a cut body).
        // We emit rows until the budget, then report the ACTUAL count emitted.
        const int EW = 36, VW = 26, Budget = 6800;
        var sb = new System.Text.StringBuilder();
        sb.AppendLine($"{sig} in {plugin}: {recs.Count} total record(s).");

        // Header row
        var head = "  " + "EditorID [FormKey]".PadRight(EW);
        foreach (var p in colProps) head += " | " + SumTrunc(p.Name, VW).PadRight(VW);
        sb.AppendLine(head.TrimEnd());
        sb.AppendLine("  " + new string('-', Math.Min(EW + colProps.Count * (VW + 3), 200)));

        int emitted = 0;
        foreach (var rec in take)
        {
            if (sb.Length > Budget) break;
            var hdr = (rec.EditorID ?? "(no-eid)") + " [" + rec.FormKey + "]";
            var line = "  " + SumTrunc(hdr, EW).PadRight(EW);
            foreach (var prop in colProps)
            {
                string cell;
                try
                {
                    var v = prop.GetValue(rec);
                    cell = v switch
                    {
                        null => "",
                        Mutagen.Bethesda.Plugins.IFormLinkIdentifier fli => FormatFormLink(fli),
                        IEnumerable ie when v.GetType() != typeof(string) =>
                            "[" + SumCountEnumerable(ie) + " items]",
                        _ => v.ToString() ?? "",
                    };
                }
                catch { cell = "?"; }
                line += " | " + SumTrunc(cell, VW).PadRight(VW);
            }
            sb.AppendLine(line.TrimEnd());   // drop trailing column padding -- pure wasted tokens
            emitted++;
        }

        if (emitted < recs.Count)
            sb.AppendLine($"  ... showed {emitted} of {recs.Count}. Narrow with search_records (by EditorID/FormKey) " +
                          "or get_record for one record's full dump; raise 'limit' only if you truly need more rows.");

        return sb.ToString();
    }

    private static string SumTrunc(string? s, int max) =>
        string.IsNullOrEmpty(s) ? "" : s.Length <= max ? s : s[..(max - 3)] + "...";

    private static int SumCountEnumerable(IEnumerable ie)
    {
        if (ie is ICollection c) return c.Count;
        int n = 0; foreach (var _ in ie) n++; return n;
    }

    /// <summary>
    /// Expose raw record list for batch write operations. Returns all records of the given
    /// signature from the named plugin, or an empty list when the plugin/type isn't found.
    /// </summary>
    public static IReadOnlyList<Mutagen.Bethesda.Plugins.Records.IMajorRecordGetter> GetRecordsForBatch(
        object? envObj, string plugin, string sig)
    {
        var mod = ResolveMod(plugin, envObj);
        if (mod == null) return [];
        var idx = GetModIndex(mod, plugin);
        return idx.BySig.TryGetValue(sig, out var recs) ? recs : [];
    }

    public static IReadOnlyList<(string formKey, string editorId, string type)> QuerySearch(
        object? envObj, string plugin, string query, int limit = 50, int offset = 0)
    {
        var mod = ResolveMod(plugin, envObj);
        if (mod == null) return [];
        var idx = GetModIndex(mod, plugin);
        var hits = new List<(string, string, string)>();
        int skipped = 0;
        foreach (var kv in idx.BySig)
            foreach (var r in kv.Value)
            {
                var eid = r.EditorID ?? "";
                if (eid.Contains(query, StringComparison.OrdinalIgnoreCase) ||
                    r.FormKey.ToString().Contains(query, StringComparison.OrdinalIgnoreCase))
                {
                    if (skipped < offset) { skipped++; continue; }
                    hits.Add((r.FormKey.ToString(), eid, kv.Key));
                    if (hits.Count >= limit) return hits;
                }
            }
        return hits;
    }

    public sealed record SearchHit(string FormKey, string EditorID, string Type, string Plugin, string Name = "");

    /// <summary>Search every loaded plugin for records whose EditorID, FormID, or display Name
    /// contains the query (substring, case-insensitive). Optional signature filter ("KYWD",
    /// "WEAP", …). Winning version per FormKey wins (de-duplicated), capped for responsiveness.</summary>
    public static List<SearchHit> SearchAllRecords(object? envObj, string query, string? typeFilter = null, int limit = 200)
    {
        var hits = new List<SearchHit>();
        if (envObj == null) return hits;
        query ??= "";
        var seen = new HashSet<FormKey>();
        // typeFilter is a comma-separated set of record class names (from a FormLink's valid targets).
        var filterSet = string.IsNullOrWhiteSpace(typeFilter)
            ? null
            : new HashSet<string>(
                typeFilter.Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries),
                StringComparer.OrdinalIgnoreCase);

        foreach (var (name, mod) in AllLoadedMods(envObj))
        {
            var idx = GetModIndex(mod, name);
            foreach (var kv in idx.BySig)
            {
                if (filterSet != null && !filterSet.Contains(kv.Key)) continue;
                foreach (var r in kv.Value)
                {
                    var eid = r.EditorID ?? "";
                    var dispName = r is Mutagen.Bethesda.Plugins.Aspects.INamedGetter nm ? (nm.Name ?? "") : "";
                    if (query.Length > 0 &&
                        !eid.Contains(query, StringComparison.OrdinalIgnoreCase) &&
                        !dispName.Contains(query, StringComparison.OrdinalIgnoreCase) &&
                        !r.FormKey.ToString().Contains(query, StringComparison.OrdinalIgnoreCase)) continue;
                    if (!seen.Add(r.FormKey)) continue;
                    hits.Add(new SearchHit(r.FormKey.ToString(), eid, kv.Key, name, dispName));
                    if (hits.Count >= limit) return hits;
                }
            }
        }
        return hits;
    }

    public static string QueryRecordFields(object? envObj, string plugin, string formKeyOrEditorId)
    {
        // Build a bounded candidate list: the named plugin first, then loose-opened ESPs and
        // any plugin whose index is already built (avoids re-indexing a whole load order).
        var candidates = new List<(string name, object mod)>();
        var named = ResolveMod(plugin, envObj);
        if (named != null) candidates.Add((plugin, named));
        foreach (var m in AllLoadedMods(envObj))
        {
            bool alreadyNamed = candidates.Any(c => string.Equals(c.name, m.name, StringComparison.OrdinalIgnoreCase));
            if (alreadyNamed) continue;
            if (LooseMods.ContainsKey(m.name) || _modIndexCache.ContainsKey(m.name))
                candidates.Add(m);
        }

        FormKey.TryFactory(formKeyOrEditorId, out var fk);

        foreach (var (name, mod) in candidates)
        {
            var idx = GetModIndex(mod, name);
            Mutagen.Bethesda.Plugins.Records.IMajorRecordGetter? rec = null;
            if (!fk.IsNull && idx.ByFormKey.TryGetValue(fk, out var byFk))
                rec = byFk;
            else
                rec = idx.BySig.Values.SelectMany(v => v)
                         .FirstOrDefault(r => string.Equals(r.EditorID, formKeyOrEditorId, StringComparison.OrdinalIgnoreCase));
            if (rec == null) continue;

            var temp = new RecordNode { Key = rec.EditorID ?? rec.FormKey.ToString() };
            AddLeafInit(temp, "FormKey", rec.FormKey.ToString());
            AddLeafInit(temp, "EditorID", rec.EditorID ?? "");
            AddLeafInit(temp, "Type", rec.Registration.Name);
            AddLeafInit(temp, "SourcePlugin", name);
            WalkObject(rec, temp, 0, 4, name);

            var sb2 = new System.Text.StringBuilder();
            FlattenToText(temp, 0, sb2);
            return sb2.ToString();
        }

        var loaded = string.Join(", ", AllLoadedMods(envObj).Select(m => m.name));
        return $"(record '{formKeyOrEditorId}' not found in '{plugin}'. " +
               $"Loaded plugins: {(string.IsNullOrEmpty(loaded) ? "none" : loaded)}. " +
               $"Try search_records first, or use the exact plugin name from list_plugins.)";
    }

    // xEdit-style error check over a loaded plugin: deleted records + references to masters
    // the plugin doesn't declare (a genuine "missing master" / broken-ref class).
    public static string CheckPlugin(object? envObj, string plugin)
    {
        var mod = ResolveMod(plugin, envObj);
        if (mod == null) return $"(plugin '{plugin}' is not loaded)";
        if (mod is not Mutagen.Bethesda.Fallout4.IFallout4ModGetter f4) return "(not a Fallout 4 plugin)";

        var declared = new HashSet<ModKey> { f4.ModKey };
        foreach (var m in f4.MasterReferences) declared.Add(m.Master);

        // Use the live link cache for type-collision detection if available.
        var liveCache = envObj != null ? LinkCache : null;

        int count = 0, deleted = 0, broken = 0, typeCollisions = 0;
        var samples = new List<string>();
        foreach (var rec in f4.EnumerateMajorRecords())
        {
            count++;
            if (rec.IsDeleted)
            {
                deleted++;
                if (samples.Count < 50) samples.Add($"DELETED  {CheckLabel(rec)}");
            }
            foreach (var link in rec.EnumerateFormLinks())
            {
                if (link.FormKey.IsNull) continue;
                if (declared.Contains(link.FormKey.ModKey)) continue;
                broken++;
                if (samples.Count < 50) samples.Add($"REF TO UNDECLARED MASTER  {CheckLabel(rec)} -> {link.FormKey}");
            }

            // Check for type collision against the BASE record in the originating master plugin.
            // Using TryResolve (winning) is wrong here: if this plugin already overrides the FormKey,
            // TryResolve returns this plugin's own COBJ -- same type, no mismatch detected.
            // Instead, resolve from the specific master plugin (rec.FormKey.ModKey) to get the ground
            // truth type before any override. A COBJ override on a FormID whose master is ARMO is a
            // game-breaking record corruption (FallenWorldCrafting_Compat root cause).
            if (envObj != null && !f4.ModKey.Equals(rec.FormKey.ModKey))
            {
                var masterPlugin = rec.FormKey.ModKey.FileName;
                var masterRec = GetRecordVersion(envObj, masterPlugin, rec.FormKey);
                if (masterRec != null)
                {
                    var thisType = rec.Registration.GetterType;
                    var masterType = masterRec.Registration.GetterType;
                    if (!masterType.IsAssignableFrom(thisType) && !thisType.IsAssignableFrom(masterType))
                    {
                        typeCollisions++;
                        var masterEdid = string.IsNullOrEmpty(masterRec.EditorID)
                            ? masterRec.FormKey.ToString() : masterRec.EditorID;
                        if (samples.Count < 50)
                            samples.Add($"TYPE COLLISION (game-breaking)  {CheckLabel(rec)} overrides " +
                                        $"{masterType.Name} '{masterEdid}' [{masterRec.FormKey}] " +
                                        $"in {masterPlugin}");
                    }
                }
            }
        }

        var sb = new System.Text.StringBuilder();
        sb.AppendLine($"Checked {plugin}: {count} records, {deleted} deleted, {broken} reference(s) to undeclared masters, {typeCollisions} type collision(s).");
        if (deleted + broken + typeCollisions == 0) { sb.AppendLine("No issues found."); return sb.ToString(); }
        sb.AppendLine("Issues:");
        foreach (var s in samples) sb.AppendLine("  " + s);
        if (deleted + broken + typeCollisions > samples.Count) sb.AppendLine($"  ... and {deleted + broken + typeCollisions - samples.Count} more");
        return sb.ToString();
    }

    /// <summary>Remove a ConstructibleObject override from an editable (open) plugin by FormKey.
    /// Returns true when the record was found and removed. No-op if the plugin isn't editable
    /// or the record isn't in it.</summary>
    public static bool RemoveFromEditableMod(string plugin, Mutagen.Bethesda.Plugins.FormKey fk)
    {
        if (!EditableMods.TryGetValue(plugin, out var modObj)) return false;
        if (modObj is not Mutagen.Bethesda.Fallout4.Fallout4Mod mod) return false;
        return mod.ConstructibleObjects.Remove(fk);
    }

    private static string CheckLabel(Mutagen.Bethesda.Plugins.Records.IMajorRecordGetter r) =>
        $"{(string.IsNullOrEmpty(r.EditorID) ? r.FormKey.ToString() : r.EditorID)} [{r.FormKey}] ({r.Registration.Name})";

    // Vanilla core masters whose ACHR/REFR/cell-placed records aren't indexed by Mutagen's
    // top-level group cache. FormLinks pointing into these are always valid at runtime.
    // Single source of truth -- also gates the write layer, see ProtectedPlugins.
    private static readonly IReadOnlySet<string> VanillaMasters = ProtectedPlugins.VanillaMasters;

    private static bool IsVanillaRef(Mutagen.Bethesda.Plugins.FormKey fk) =>
        VanillaMasters.Contains(fk.ModKey.FileName.String);

    // Deep error scan using the load-order link cache: references that resolve to NOTHING
    // anywhere in the load order are dangling and will crash the game.
    // Vanilla-master refs are skipped — Mutagen can't index cell-placed ACHR/REFR records,
    // so those refs would all appear dangling even when valid.
    public static string ScanErrorsDeep(object? envObj, string plugin)
    {
        if (envObj == null) return "Deep error scan needs the loaded game environment. Use 'Load Env' first.";
        if (ResolveMod(plugin, envObj) is not Mutagen.Bethesda.Fallout4.IFallout4ModGetter mod)
            return $"(plugin '{plugin}' is not loaded)";

        var cache = (Mutagen.Bethesda.Plugins.Cache.ILinkCache)((dynamic)envObj).LinkCache;
        int dangling = 0, deleted = 0, count = 0;
        var samples = new List<string>();
        foreach (var rec in mod.EnumerateMajorRecords())
        {
            count++;
            if (rec.IsDeleted) { deleted++; if (samples.Count < 60) samples.Add($"DELETED  {CheckLabel(rec)}"); }
            foreach (var link in rec.EnumerateFormLinks())
            {
                if (link.FormKey.IsNull || IsVanillaRef(link.FormKey)) continue;
                if (!cache.TryResolveIdentifier<IMajorRecordGetter>(link.FormKey, out _))
                {
                    dangling++;
                    if (samples.Count < 60) samples.Add($"DANGLING REF (crash risk)  {CheckLabel(rec)} -> {link.FormKey}");
                }
            }
        }

        var sb = new System.Text.StringBuilder();
        sb.AppendLine($"Error scan of {plugin}: {count} records, {deleted} deleted, {dangling} unresolved reference(s).");
        if (deleted + dangling == 0) { sb.AppendLine("No crash-causing issues found."); return sb.ToString(); }
        sb.AppendLine("Issues:");
        foreach (var s in samples) sb.AppendLine("  " + s);
        if (deleted + dangling > samples.Count) sb.AppendLine($"  ... and {deleted + dangling - samples.Count} more");
        return sb.ToString();
    }

    // Scan a single plugin for broken FormLinks to non-vanilla masters. Returns grouped results.
    public static string ScanBrokenRefs(object? envObj, string plugin)
    {
        if (envObj == null) return "scan_broken_refs needs the loaded game environment. Use 'Load Env' first.";
        if (ResolveMod(plugin, envObj) is not Mutagen.Bethesda.Fallout4.IFallout4ModGetter mod)
            return $"(plugin '{plugin}' is not loaded)";

        var cache = (Mutagen.Bethesda.Plugins.Cache.ILinkCache)((dynamic)envObj).LinkCache;
        var loadedPlugins = BuildLoadedPluginSet(envObj);
        var result = ScanBrokenRefsInMod(cache, plugin, mod, loadedPlugins);
        return result ?? $"{plugin}: no broken references found.";
    }

    // Build the set of plugin filenames present in the loaded environment (env + editable mods).
    // Used by the broken-ref scan to distinguish "target plugin not in load order" (skip — might
    // be CC content or external content not managed by MO2) from "plugin loaded but record missing"
    // (real broken ref worth reporting).
    private static HashSet<string> BuildLoadedPluginSet(object? envObj)
    {
        var set = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var kv in EditableMods) set.Add(kv.Key);
        foreach (var kv in LooseMods) set.Add(kv.Key);
        if (envObj != null)
        {
            dynamic env = envObj;
            foreach (var l in ((IEnumerable)env.LoadOrder.ListedOrder).Cast<dynamic>())
                if (l.Mod != null) set.Add((string)l.ModKey.FileName.String);
        }
        return set;
    }

    // Returns null when the plugin is clean (no broken refs), so callers can skip it.
    // A "broken reference" is a FormLink that points to a FormID inside a LOADED plugin where
    // that record does not exist — the target was deleted or never added. At runtime the engine
    // dereferences a null pointer when it tries to load that record, causing a crash.
    // Refs to vanilla masters (Fallout4.esm / DLC*.esm) are excluded: Mutagen can't index
    // cell-placed ACHR/REFR records so those would all appear broken even when valid.
    // Refs to plugins NOT in the load order are also excluded: those are typically CC content
    // or other external content not visible to the MO2-managed environment; they can't be
    // verified and would all appear broken (false positives).
    private static string? ScanBrokenRefsInMod(
        Mutagen.Bethesda.Plugins.Cache.ILinkCache cache, string pluginName,
        Mutagen.Bethesda.Fallout4.IFallout4ModGetter mod,
        HashSet<string> loadedPlugins)
    {
        int count = 0, dangling = 0;
        var byMaster = new Dictionary<string, int>(StringComparer.OrdinalIgnoreCase);
        var samples = new List<string>();

        foreach (var rec in mod.EnumerateMajorRecords())
        {
            count++;
            foreach (var link in rec.EnumerateFormLinks())
            {
                var fk = link.FormKey;
                if (fk.IsNull || IsVanillaRef(fk)) continue;
                // Skip refs to plugins not present in the load order: they can't be resolved
                // through Mutagen regardless of whether they're actually installed (CC content,
                // external loaders, etc.), so they would all appear broken.
                if (!loadedPlugins.Contains(fk.ModKey.FileName.String)) continue;
                if (!cache.TryResolveIdentifier<IMajorRecordGetter>(fk, out _))
                {
                    dangling++;
                    var master = fk.ModKey.FileName.String;
                    byMaster[master] = byMaster.TryGetValue(master, out var n) ? n + 1 : 1;
                    if (samples.Count < 80)
                        samples.Add($"  [{fk}] in {CheckLabel(rec)}");
                }
            }
        }

        if (dangling == 0) return null;

        var sb = new System.Text.StringBuilder();
        sb.AppendLine($"{pluginName}  ({dangling} broken ref(s) across {count} records)");
        sb.AppendLine("  Broken refs by target plugin:");
        foreach (var kv in byMaster.OrderByDescending(x => x.Value))
            sb.AppendLine($"    {kv.Value,5}x  {kv.Key}");
        sb.AppendLine("  Sample broken refs:");
        foreach (var s in samples) sb.AppendLine(s);
        if (dangling > samples.Count) sb.AppendLine($"  ... and {dangling - samples.Count} more");
        return sb.ToString();
    }

    // Scan every non-vanilla plugin in the load order for dangling FormLinks.
    // Only plugins with actual broken refs appear in the output.
    public static string ScanAllPluginsForBrokenRefs(object? envObj)
    {
        if (envObj == null) return "scan_all_plugins needs the loaded game environment. Use 'Load Env' first.";

        var cache = (Mutagen.Bethesda.Plugins.Cache.ILinkCache)((dynamic)envObj).LinkCache;
        var loadedPlugins = BuildLoadedPluginSet(envObj);
        var results = new System.Text.StringBuilder();
        int totalPlugins = 0, dirtyPlugins = 0, totalBroken = 0;

        foreach (var (name, modObj) in AllLoadedMods(envObj))
        {
            if (VanillaMasters.Contains(name)) continue;
            if (modObj is not Mutagen.Bethesda.Fallout4.IFallout4ModGetter mod) continue;
            totalPlugins++;
            var pluginResult = ScanBrokenRefsInMod(cache, name, mod, loadedPlugins);
            if (pluginResult == null) continue; // clean — skip entirely
            dirtyPlugins++;
            var m = System.Text.RegularExpressions.Regex.Match(pluginResult, @"(\d+) broken ref");
            if (m.Success && int.TryParse(m.Groups[1].Value, out var n)) totalBroken += n;
            results.AppendLine(pluginResult);
        }

        if (dirtyPlugins == 0)
            return $"All {totalPlugins} non-vanilla plugins are clean — no broken references found.";

        return $"=== Crash Risk Scan: {dirtyPlugins} of {totalPlugins} plugins have broken refs — {totalBroken} total ===\n\n" +
               results.ToString();
    }

    // Conflict scan: records in this plugin that other loaded plugins also define/override.
    public static string ScanConflicts(object? envObj, string plugin)
    {
        if (envObj == null) return "Conflict scan needs the loaded game environment. Use 'Load Env' first.";
        dynamic env = envObj;

        // One pass over the load order: FormKey -> plugins that touch it.
        var owners = new Dictionary<FormKey, List<string>>();
        foreach (var l in (IEnumerable)env.LoadOrder.ListedOrder)
        {
            dynamic ld = l;
            if (ld.Mod == null) continue;
            string pname = ld.ModKey.FileName.String;
            if (ld.Mod is Mutagen.Bethesda.Fallout4.IFallout4ModGetter m)
                foreach (var rec in m.EnumerateMajorRecords())
                {
                    if (!owners.TryGetValue(rec.FormKey, out var lst)) { lst = new(); owners[rec.FormKey] = lst; }
                    if (!lst.Contains(pname)) lst.Add(pname);
                }
        }

        if (ResolveMod(plugin, envObj) is not Mutagen.Bethesda.Fallout4.IFallout4ModGetter mod)
            return $"(plugin '{plugin}' is not loaded)";

        int conflicts = 0;
        var samples = new List<string>();
        foreach (var rec in mod.EnumerateMajorRecords())
        {
            if (owners.TryGetValue(rec.FormKey, out var lst) && lst.Count > 1)
            {
                conflicts++;
                if (samples.Count < 60) samples.Add($"{CheckLabel(rec)}  <- {string.Join(", ", lst)}");
            }
        }

        var sb = new System.Text.StringBuilder();
        sb.AppendLine($"Conflict scan of {plugin}: {conflicts} record(s) also touched by other plugins.");
        if (conflicts == 0) { sb.AppendLine("No conflicts with other loaded plugins."); return sb.ToString(); }
        sb.AppendLine("Conflicts:");
        foreach (var s in samples) sb.AppendLine("  " + s);
        if (conflicts > samples.Count) sb.AppendLine($"  ... and {conflicts - samples.Count} more");
        return sb.ToString();
    }

    // Resolve an EditorID to a FormKey across loaded plugins (bounded to loose + already
    // indexed, so a full load order isn't re-enumerated). Used for setting FormLink fields.
    public static FormKey ResolveEditorIdToFormKey(object? envObj, string editorId)
    {
        foreach (var (name, mod) in AllLoadedMods(envObj))
        {
            if (!LooseMods.ContainsKey(name) && !_modIndexCache.ContainsKey(name)) continue;
            var idx = GetModIndex(mod, name);
            var rec = idx.BySig.Values.SelectMany(v => v)
                .FirstOrDefault(r => string.Equals(r.EditorID, editorId, StringComparison.OrdinalIgnoreCase));
            if (rec != null) return rec.FormKey;
        }
        return FormKey.Null;
    }

    private static void FlattenToText(RecordNode node, int depth, System.Text.StringBuilder sb)
    {
        foreach (var c in node.Children)
        {
            var indent = new string(' ', depth * 2);
            // Summary nodes (conditions/components) print their one readable line and stop, so the
            // dump shows "GetGlobalValue(...) == 0" not a dozen ParameterOne*/RunOnType sub-rows.
            if (c.IsLeaf || c.IsSummary) sb.AppendLine($"{indent}{c.Key}: {c.Value}");
            else { sb.AppendLine($"{indent}{c.Key}:"); FlattenToText(c, depth + 1, sb); }
        }
    }

    // ---- reflection walker ----------------------------------------------

    [ThreadStatic]
    private static HashSet<object>? _visiting;  // cycle guard

    private static HashSet<object> Visiting => _visiting ??= new HashSet<object>();

    private static readonly System.Collections.Concurrent.ConcurrentDictionary<Type, PropertyInfo[]> _propCache = new();

    // Hard cap on nodes produced per record walk. Large records (NPC_ with packages,
    // factions, items, AI data, leveled entries) can otherwise expand into tens of
    // thousands of nodes and freeze the populate. Reset at each top-level (depth 0) walk.
    [ThreadStatic] private static int _walkNodeCount;
    private const int MaxWalkNodes = 5000;

    private static void WalkObject(object obj, RecordNode parent, int depth, int maxDepth, string pluginName = "")
    {
        if (depth == 0) _walkNodeCount = 0;
        if (depth > maxDepth) return;
        if (_walkNodeCount >= MaxWalkNodes) return;
        if (obj == null!) return;

        var type = obj.GetType();

        // Primitives / enums -> just set parent value and return
        if (_leafTypes.Contains(type) || type.IsEnum)
        {
            if (parent.IsLeaf) parent.Values[pluginName] = obj.ToString()!;
            return;
        }

        // A FormLink is a LEAF wherever it appears -- including as a list item (e.g. COBJ
        // Categories). Without this, walking the item recurses into its CLR internals
        // (FormKey, then .Type -> System.Type -> Assembly -> the whole framework graph),
        // which produced hundreds of bogus "Type.Assembly.Defined..." rows.
        // IMPORTANT: a record (IMajorRecordGetter) also implements IFormLinkIdentifier, so it must
        // be EXCLUDED here -- otherwise the record being walked is treated as a leaf and yields no
        // fields at all. Only a real form-link *reference* (not a record) is a leaf.
        if (obj is not Mutagen.Bethesda.Plugins.Records.IMajorRecordGetter
            && obj is Mutagen.Bethesda.Plugins.IFormLinkIdentifier topLink)
        {
            if (parent.IsLeaf)
            {
                parent.Values[pluginName] = FormatFormLink(topLink);
                // A FormLink list item (Categories, Keywords, FormList entries) is editable via the
                // record picker, same as a FormLink property -- so these are pickable too.
                if (parent.EditKind != Models.FieldEditKind.Ref)
                {
                    parent.EditKind = Models.FieldEditKind.Ref;
                    (parent.RefType, parent.RefTypes) = FormLinkInfo(obj.GetType());
                }
            }
            return;
        }

        // TranslatedString (GMST values, FULL names, etc.) -> show its text, not its internals.
        if (obj is Mutagen.Bethesda.Strings.ITranslatedStringGetter ts)
        {
            if (parent.IsLeaf) parent.Values[pluginName] = ts.String ?? ts.ToString() ?? "";
            return;
        }

        // Identity structs are single values, not sub-trees -- otherwise a record's FormKey
        // explodes into FormKey.ID / FormKey.ModKey.FileName.String / ... noise rows.
        if (obj is FormKey || obj is ModKey)
        {
            if (parent.IsLeaf) parent.Values[pluginName] = obj.ToString() ?? "";
            return;
        }

        // Never descend into CLR/reflection metadata. Record any such value as a single string.
        if (obj is Type || obj is System.Reflection.MemberInfo || obj is System.Reflection.Assembly
            || obj is System.Reflection.Module || obj is System.Reflection.ParameterInfo
            || (type.Namespace ?? "").StartsWith("System.Reflection", StringComparison.Ordinal))
        {
            if (parent.IsLeaf) parent.Values[pluginName] = obj.ToString() ?? "";
            return;
        }

        // Cycle guard
        if (!Visiting.Add(obj)) return;
        try
        {
            var props = _propCache.GetOrAdd(type, t => 
                t.GetProperties(BindingFlags.Public | BindingFlags.Instance)
                 .Where(p => p.CanRead && p.GetIndexParameters().Length == 0)
                 .Where(p => !_skipProps.Contains(p.Name))
                 .ToArray());

            foreach (var prop in props)
            {
                object? val;
                try { val = prop.GetValue(obj); } catch { continue; }
                if (val == null) continue;

                var valType = val.GetType();

                // Skip another RecordNode ancestor type (Mutagen link etc.)
                if (val is Mutagen.Bethesda.Plugins.Records.IMajorRecordGetter) continue;

                if (_walkNodeCount >= MaxWalkNodes) break;

                var child = parent.GetChild(prop.Name);
                if (child == null)
                {
                    child = new RecordNode { Key = prop.Name, Parent = parent };
                    parent.Children.Add(child);
                    _walkNodeCount++;
                }

                if (_leafTypes.Contains(valType) || valType.IsEnum)
                {
                    child.Values[pluginName] = val.ToString()!;
                    // Editor hint: bool -> checkbox, enum -> dropdown of its names (xEdit-style).
                    if (valType == typeof(bool)) child.EditKind = Models.FieldEditKind.Bool;
                    else if (valType.IsEnum && child.EnumOptions == null)
                    {
                        child.EditKind = Models.FieldEditKind.Enum;
                        child.EnumOptions = Enum.GetNames(valType);
                    }
                }
                else if (val is string s)
                {
                    child.Values[pluginName] = s;
                }
                else if (val is Mutagen.Bethesda.Plugins.IFormLinkIdentifier fli)
                {
                    child.Values[pluginName] = FormatFormLink(fli);
                    child.EditKind = Models.FieldEditKind.Ref;   // edits via the record picker
                    if (child.RefType == null)
                        (child.RefType, child.RefTypes) = FormLinkInfo(prop.PropertyType);
                }
                // A TranslatedString (Description, FULL name, ...) is IEnumerable over its localized
                // strings, so without this it would explode into [0].Key/[0].Value rows. Show its text.
                else if (val is Mutagen.Bethesda.Strings.ITranslatedStringGetter tstr)
                {
                    child.Values[pluginName] = tstr.String ?? tstr.ToString() ?? "";
                }
                else if (val is IEnumerable enumerable and not string)
                {
                    // Whole-collection collapse first (e.g. a byte blob like Model.Data -> "N bytes").
                    if (Services.Rendering.ElementRenderer.TryRenderByteBlob(enumerable, out var blob))
                    {
                        child.Values[pluginName] = blob;
                    }
                    else
                    {
                        var items = new List<object>();
                        foreach (var it in enumerable) if (it != null) items.Add(it);

                        // A list of pure FormLinks (Keywords, Categories, FormList Items, ...) is an
                        // unordered SET: the game ignores order. Sort by FormKey so a reordered-but-
                        // identical list doesn't light up as a fake conflict (this is what xEdit's
                        // "(sorted)" subrecords do). Ordered data (Conditions/Components, whose items
                        // are structs, not FormLinks) is left in place.
                        if (items.Count > 1 && items.All(x =>
                                x is Mutagen.Bethesda.Plugins.IFormLinkIdentifier
                                && x is not Mutagen.Bethesda.Plugins.Records.IMajorRecordGetter))
                            items.Sort((a, b) => string.CompareOrdinal(
                                ((Mutagen.Bethesda.Plugins.IFormLinkIdentifier)a).FormKey.ToString(),
                                ((Mutagen.Bethesda.Plugins.IFormLinkIdentifier)b).FormKey.ToString()));

                        int i = 0;
                        foreach (var item in items)
                        {
                            if (_walkNodeCount >= MaxWalkNodes) break;
                            var entryKey = $"[{i}]";
                            var entry = child.GetChild(entryKey);
                            if (entry == null)
                            {
                                entry = new RecordNode { Key = entryKey, Parent = child };
                                child.Children.Add(entry);
                                _walkNodeCount++;
                            }
                            if (Services.Rendering.ElementRenderer.TryRenderLine(item, out var friendly))
                            {
                                // One readable line for display; keep the child fields so they stay
                                // editable in the Record tree (e.g. a component's Count).
                                entry.Values[pluginName] = friendly;
                                entry.IsSummary = true;
                            }
                            else
                            {
                                entry.Values[pluginName] = BuildSummary(item);
                            }
                            WalkObject(item, entry, depth + 1, maxDepth, pluginName);
                            i++;
                        }
                    }
                }
                // A renderable property value (e.g. an Activator's MarkerColor) collapses to one line
                // (#CC4C33) instead of expanding into R/G/B/A/IsKnownColor/... reflection rows.
                else if (Services.Rendering.ElementRenderer.TryRenderLine(val, out var friendlyVal))
                {
                    child.Values[pluginName] = friendlyVal;
                }
                else
                {
                    WalkObject(val, child, depth + 1, maxDepth, pluginName);
                }
            }
        }
        finally
        {
            Visiting.Remove(obj);
        }
    }

    internal static string FormatCondition(Mutagen.Bethesda.Fallout4.IConditionGetter cond)
    {
        var op = cond.CompareOperator switch
        {
            Mutagen.Bethesda.Fallout4.CompareOperator.EqualTo => "==",
            Mutagen.Bethesda.Fallout4.CompareOperator.NotEqualTo => "!=",
            Mutagen.Bethesda.Fallout4.CompareOperator.GreaterThan => ">",
            Mutagen.Bethesda.Fallout4.CompareOperator.GreaterThanOrEqualTo => ">=",
            Mutagen.Bethesda.Fallout4.CompareOperator.LessThan => "<",
            Mutagen.Bethesda.Fallout4.CompareOperator.LessThanOrEqualTo => "<=",
            _ => cond.CompareOperator.ToString(),
        };

        // Right-hand side: a constant (ConditionFloat) or a global var (ConditionGlobal).
        string rhs = cond switch
        {
            Mutagen.Bethesda.Fallout4.IConditionGlobalGetter g => FormatFormLink(g.ComparisonValue),
            Mutagen.Bethesda.Fallout4.IConditionFloatGetter f => f.ComparisonValue.ToString("0.###"),
            _ => "?",
        };

        var data = cond.Data;
        string fn = data is Mutagen.Bethesda.Fallout4.IFunctionConditionDataGetter fc
            ? fc.Function.ToString() : data.GetType().Name;

        var args = new List<string>();
        if (data is Mutagen.Bethesda.Fallout4.IFunctionConditionDataGetter fcd)
        {
            if (!fcd.ParameterOneRecord.FormKey.IsNull) args.Add(FormatFormLink(fcd.ParameterOneRecord));
            else if (fcd.ParameterOneNumber != 0) args.Add(fcd.ParameterOneNumber.ToString());
            if (!fcd.ParameterTwoRecord.FormKey.IsNull) args.Add(FormatFormLink(fcd.ParameterTwoRecord));
            else if (fcd.ParameterTwoNumber != 0) args.Add(fcd.ParameterTwoNumber.ToString());
        }

        var line = $"{fn}({string.Join(", ", args)}) {op} {rhs}";
        if (data.RunOnType != Mutagen.Bethesda.Fallout4.Condition.RunOnType.Subject)
        {
            line += $" on {data.RunOnType}";
            if (!data.Reference.FormKey.IsNull) line += $" {FormatFormLink(data.Reference)}";
        }
        return line;
    }

    private static string BuildSummary(object item)
    {
        if (item == null) return "";
        var type = item.GetType();
        if (_leafTypes.Contains(type) || type.IsEnum || item is string)
            return item.ToString() ?? "";

        if (item is Mutagen.Bethesda.Plugins.IFormLinkIdentifier fli)
            return fli.FormKey.IsNull ? "Null" : fli.FormKey.ToString();

        var parts = new List<string>();
        string[] summaryProps = { "Level", "Count", "Reference", "Magnitude", "Duration", "BaseEffect", "EditorID", "FormKey", "Value", "FunctionType", "Property", "Step", "Keyword" };

        var props = _propCache.GetOrAdd(type, t => t.GetProperties(BindingFlags.Public | BindingFlags.Instance).Where(p => p.CanRead).ToArray());
        
        foreach (var pName in summaryProps)
        {
            var p = props.FirstOrDefault(x => x.Name == pName);
            if (p != null)
            {
                try
                {
                    var val = p.GetValue(item);
                    if (val != null)
                    {
                        if (val is Mutagen.Bethesda.Plugins.IFormLinkIdentifier f)
                        {
                            if (!f.FormKey.IsNull) parts.Add(f.FormKey.ToString());
                        }
                        else if (val is string s && !string.IsNullOrEmpty(s))
                        {
                            parts.Add(s);
                        }
                        else
                        {
                            parts.Add(val.ToString()!);
                        }
                    }
                } catch { }
            }
        }

        if (parts.Count > 0)
            return string.Join(" ", parts);
            
        return "";
    }

    // ---- helpers --------------------------------------------------------

    private static void AddLeafInit(RecordNode parent, string key, string value, string pluginName = "")
    {
        var node = new RecordNode { Key = key, Parent = parent };
        node.Values[pluginName] = value;
        parent.Children.Add(node);
    }
}
