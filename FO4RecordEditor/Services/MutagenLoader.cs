using System.Collections;
using System.IO;
using System.Reflection;
using FO4RecordEditor.Models;
using Mutagen.Bethesda;
using Mutagen.Bethesda.Fallout4;
using Mutagen.Bethesda.Plugins;
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

    public static readonly Dictionary<string, object> LooseMods = new(StringComparer.OrdinalIgnoreCase);

    // Tracks file paths for loose-opened ESPs so OpenPlugin can reload them as mutable.
    public static readonly Dictionary<string, string> LooseModPaths = new(StringComparer.OrdinalIgnoreCase);

    // Plugins the AI has opened for editing (mutable). These take priority over the env /
    // loose read-only copies for ALL reads, so edits are immediately visible in the tree and
    // to get_record (hot loading). WriteService registers entries here.
    public static readonly Dictionary<string, object> EditableMods = new(StringComparer.OrdinalIgnoreCase);

    // ---- per-mod record index (built once, reused for every group/record load) ----
    // Without this, every group expansion re-enumerated the ENTIRE mod (O(allRecords)
    // per group), which hangs on large masters like Fallout4.esm.
    private sealed class ModIndex
    {
        public readonly Dictionary<string, List<Mutagen.Bethesda.Plugins.Records.IMajorRecordGetter>> BySig =
            new(StringComparer.Ordinal);
        public readonly Dictionary<FormKey, Mutagen.Bethesda.Plugins.Records.IMajorRecordGetter> ByFormKey = new();
    }

    private static readonly Dictionary<string, ModIndex> _modIndexCache = new(StringComparer.OrdinalIgnoreCase);

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
        if (_modIndexCache.TryGetValue(modName, out var cached)) return cached;

        var idx = new ModIndex();
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

    public static void InvalidateModIndex(string modName) => _modIndexCache.Remove(modName);

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
        IProgress<(string message, double? percent)>? progress = null)
    {
        progress?.Report(("Initializing Game Environment...", 0));
        _modIndexCache.Clear();
        var env = GameEnvironment.Typical.Builder<IFallout4Mod, IFallout4ModGetter>(GameRelease.Fallout4).Build();

        var plugins = new List<string>();
        foreach (var l in env.LoadOrder.ListedOrder)
            if (l.Mod != null) plugins.Add(l.ModKey.FileName.String);

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

        var versions = new List<(string plugin, Mutagen.Bethesda.Plugins.Records.IMajorRecordGetter rec)>();
        if (envObj != null)
        {
            dynamic env = envObj;
            foreach (var l in (IEnumerable)env.LoadOrder.ListedOrder)
            {
                dynamic ld = l;
                if (ld.Mod is not Mutagen.Bethesda.Fallout4.IFallout4ModGetter mod) continue;
                string name = (string)ld.ModKey.FileName.String;
                if (GetModIndex(mod, name).ByFormKey.TryGetValue(fk, out var rec))
                    versions.Add((name, rec));
            }
        }

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
        var mod = ResolveMod(plugin, envObj);
        if (mod == null) return null;
        return GetModIndex(mod, plugin).ByFormKey.TryGetValue(fk, out var r) ? r : null;
    }

    /// <summary>Plugins (in load order) that carry a version of the given FormKey.</summary>
    public static List<string> GetConflictingPlugins(object? envObj, FormKey fk)
    {
        var list = new List<string>();
        if (envObj == null) return list;
        dynamic env = envObj;
        foreach (var l in (IEnumerable)env.LoadOrder.ListedOrder)
        {
            dynamic ld = l;
            if (ld.Mod is not Mutagen.Bethesda.Fallout4.IFallout4ModGetter mod) continue;
            string name = (string)ld.ModKey.FileName.String;
            if (GetModIndex(mod, name).ByFormKey.ContainsKey(fk)) list.Add(name);
        }
        return list;
    }

    /// <summary>
    /// Build the field-level conflict matrix for one record: walk every plugin's version into a
    /// shared node tree (each leaf accumulates a value per plugin) then flatten to rows.
    /// </summary>
    public static ConflictMatrix? BuildConflictMatrix(object? envObj, string formKeyStr)
    {
        if (envObj == null || !FormKey.TryFactory(formKeyStr, out var fk)) return null;
        dynamic env = envObj;

        var versions = new List<(string plugin, Mutagen.Bethesda.Plugins.Records.IMajorRecordGetter rec)>();
        foreach (var l in (IEnumerable)env.LoadOrder.ListedOrder)
        {
            dynamic ld = l;
            if (ld.Mod is not Mutagen.Bethesda.Fallout4.IFallout4ModGetter mod) continue;
            string name = (string)ld.ModKey.FileName.String;
            if (GetModIndex(mod, name).ByFormKey.TryGetValue(fk, out var rec))
                versions.Add((name, rec));
        }
        if (versions.Count == 0) return null;

        var root = new RecordNode { Key = "root" };
        foreach (var (plugin, rec) in versions)
            WalkObject(rec, root, 0, 6, plugin);

        var plugins = versions.Select(v => v.plugin).ToList();
        var rows = new List<ConflictFieldRow>();
        FlattenConflictRows(root, "", plugins, rows);

        var last = versions[^1];
        return new ConflictMatrix
        {
            FormKey = fk.ToString(),
            EditorID = last.rec.EditorID ?? "",
            Type = last.rec.Registration.Name,
            Plugins = plugins,
            Winner = last.plugin,
            Rows = rows,
        };
    }

    private static void FlattenConflictRows(RecordNode node, string path, List<string> plugins, List<ConflictFieldRow> rows)
    {
        foreach (var child in node.Children)
        {
            string sep = child.Key.StartsWith("[") ? "" : (path.Length == 0 ? "" : ".");
            string p = path + sep + child.Key;
            if (child.Values.Count > 0)
            {
                var vals = plugins.Select(pl => child.Values.TryGetValue(pl, out var v) ? v : "").ToList();
                var present = vals.Where(v => v.Length > 0).ToList();
                bool anyMissing = present.Count < plugins.Count;
                bool differs = present.Distinct().Count() > 1 || (present.Count > 0 && anyMissing);
                rows.Add(new ConflictFieldRow { Field = p, Values = vals, Differs = differs });
            }
            FlattenConflictRows(child, p, plugins, rows);
        }
    }

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
        return GetModIndex(mod, plugin).BySig.Keys.OrderBy(x => x).ToList();
    }

    public static IReadOnlyList<(string formKey, string editorId)> QueryRecordsOfType(
        object? envObj, string plugin, string sig, int limit = 100)
    {
        var mod = ResolveMod(plugin, envObj);
        if (mod == null) return [];
        var idx = GetModIndex(mod, plugin);
        if (!idx.BySig.TryGetValue(sig, out var recs)) return [];
        return recs.Take(limit).Select(r => (r.FormKey.ToString(), r.EditorID ?? "")).ToList();
    }

    public static IReadOnlyList<(string formKey, string editorId, string type)> QuerySearch(
        object? envObj, string plugin, string query, int limit = 50)
    {
        var mod = ResolveMod(plugin, envObj);
        if (mod == null) return [];
        var idx = GetModIndex(mod, plugin);
        var hits = new List<(string, string, string)>();
        foreach (var kv in idx.BySig)
            foreach (var r in kv.Value)
            {
                var eid = r.EditorID ?? "";
                if (eid.Contains(query, StringComparison.OrdinalIgnoreCase) ||
                    r.FormKey.ToString().Contains(query, StringComparison.OrdinalIgnoreCase))
                {
                    hits.Add((r.FormKey.ToString(), eid, kv.Key));
                    if (hits.Count >= limit) return hits;
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

        int count = 0, deleted = 0, broken = 0;
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
        }

        var sb = new System.Text.StringBuilder();
        sb.AppendLine($"Checked {plugin}: {count} records, {deleted} deleted, {broken} reference(s) to undeclared masters.");
        if (deleted + broken == 0) { sb.AppendLine("No issues found."); return sb.ToString(); }
        sb.AppendLine("Issues:");
        foreach (var s in samples) sb.AppendLine("  " + s);
        if (deleted + broken > samples.Count) sb.AppendLine($"  ... and {deleted + broken - samples.Count} more");
        return sb.ToString();
    }

    private static string CheckLabel(Mutagen.Bethesda.Plugins.Records.IMajorRecordGetter r) =>
        $"{(string.IsNullOrEmpty(r.EditorID) ? r.FormKey.ToString() : r.EditorID)} [{r.FormKey}] ({r.Registration.Name})";

    // Deep error scan using the load-order link cache: references that resolve to NOTHING
    // anywhere in the load order are dangling and will crash the game.
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
                if (link.FormKey.IsNull) continue;
                if (!cache.TryResolveIdentifier(link.FormKey, out _))
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
            if (c.IsLeaf) sb.AppendLine($"{indent}{c.Key}: {c.Value}");
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
                }
                else if (val is string s)
                {
                    child.Values[pluginName] = s;
                }
                else if (val is Mutagen.Bethesda.Plugins.IFormLinkIdentifier fli)
                {
                    child.Values[pluginName] = fli.FormKey.ToString();
                }
                else if (val is IEnumerable enumerable and not string)
                {
                    int i = 0;
                    foreach (var item in enumerable)
                    {
                        if (item == null) continue;
                        if (_walkNodeCount >= MaxWalkNodes) break;
                        var entryKey = $"[{i}]";
                        var entry = child.GetChild(entryKey);
                        if (entry == null)
                        {
                            entry = new RecordNode { Key = entryKey, Parent = child };
                            child.Children.Add(entry);
                            _walkNodeCount++;
                        }
                        entry.Values[pluginName] = BuildSummary(item);
                        WalkObject(item, entry, depth + 1, maxDepth, pluginName);
                        i++;
                    }
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

    public static void SaveEsp(RecordNode root, object envObj)
    {
        if (root.FilePath == null || !root.AnyDirty) return;

        try
        {
            var modPath = ModPath.FromPath(root.FilePath);
            // In a real app we'd want to use the environment's specific mod, 
            // but for a simple "Save" we can reload and apply changes if it's a standalone ESP.
            // If it's part of an environment, we should use the mutable version from the environment.
            
            dynamic env = envObj;
            var modKey = ModKey.FromNameAndExtension(Path.GetFileName(root.FilePath));
            
            // Try to find the mod in the environment
            IFallout4Mod? mod = null;
            try {
                mod = env.LoadOrder[modKey].Mod;
            } catch { }

            if (mod == null) {
                // Standalone load
                var disposable = Fallout4Mod.CreateFromBinaryOverlay(modPath, Fallout4Release.Fallout4);
                // In a real implementation we would apply changes here
                root.ClearDirty();
                return;
            }

            foreach (var node in root.Descendants().Where(n => n.IsDirty && n.IsLeaf))
            {
                // To properly save back to Mutagen, we need to map RecordNode paths back to Mutagen properties.
                // This is complex for a generic walker. For now, we'll log that saving is partially implemented.
                // A full implementation would require a two-way mapping or using Mutagen's patch system.
            }

            // mod.WriteToBinary(modPath);
            root.ClearDirty();
        }
        catch { }
    }

    private static void AddLeafInit(RecordNode parent, string key, string value, string pluginName = "")
    {
        var node = new RecordNode { Key = key, Parent = parent };
        node.Values[pluginName] = value;
        parent.Children.Add(node);
    }
}
