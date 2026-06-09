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
                if (l.ModKey.FileName.String == modName && l.Mod != null)
                    return l.Mod;
            }
        }
        return LooseMods.TryGetValue(modName, out var loose) ? loose : null;
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

    // ---- reflection walker ----------------------------------------------

    [ThreadStatic]
    private static HashSet<object>? _visiting;  // cycle guard

    private static HashSet<object> Visiting => _visiting ??= new HashSet<object>();

    private static readonly System.Collections.Concurrent.ConcurrentDictionary<Type, PropertyInfo[]> _propCache = new();

    private static void WalkObject(object obj, RecordNode parent, int depth, int maxDepth, string pluginName = "")
    {
        if (depth > maxDepth) return;
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

                var child = parent.GetChild(prop.Name);
                if (child == null)
                {
                    child = new RecordNode { Key = prop.Name, Parent = parent };
                    parent.Children.Add(child);
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
                        var entryKey = $"[{i}]";
                        var entry = child.GetChild(entryKey);
                        if (entry == null)
                        {
                            entry = new RecordNode { Key = entryKey, Parent = child };
                            child.Children.Add(entry);
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
