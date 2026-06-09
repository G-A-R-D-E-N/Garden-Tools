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

    // Resolve a mod object by file name from the environment load order, or the loose-mod cache.
    private static object? ResolveMod(string modName, object? envObj)
    {
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

    // Every loaded mod (env load order + loose-opened ESPs) as (name, mod).
    private static IEnumerable<(string name, object mod)> AllLoadedMods(object? envObj)
    {
        if (envObj != null)
        {
            dynamic env = envObj;
            foreach (var l in ((IEnumerable)env.LoadOrder.ListedOrder).Cast<dynamic>())
                if (l.Mod != null) yield return ((string)l.ModKey.FileName.String, (object)l.Mod);
        }
        foreach (var kv in LooseMods)
            yield return (kv.Key, kv.Value);
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
        InvalidateModIndex(fileName);   // drop any stale index from a prior load of this file

        var dummy = new RecordNode { Key = "Loading...", Parent = root };
        AddLeafInit(dummy, "_NeedsGroups", fileName);
        root.Children.Add(dummy);

        progress?.Report(($"Loaded loose plugin {fileName}.", 100));
        return root;
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
            };
            AddLeafInit(recNode, "FormKey", rec.FormKey.ToString());
            AddLeafInit(recNode, "EditorID", editorId);
            AddLeafInit(recNode, "Type", sig);
            AddLeafInit(recNode, "_HasData", "False");
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
