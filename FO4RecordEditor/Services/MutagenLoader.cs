using System.Collections;
using System.IO;
using System.Reflection;
using FO4RecordEditor.Models;
using Mutagen.Bethesda.Fallout4;
using Mutagen.Bethesda.Plugins;

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

    public static RecordNode LoadEsp(string espPath, IProgress<string>? progress = null)
    {
        var root = new RecordNode { Key = Path.GetFileName(espPath), FilePath = espPath };
        progress?.Report($"Opening {Path.GetFileName(espPath)}...");

        var modPath = ModPath.FromPath(espPath);
        using var mod = Fallout4Mod.CreateFromBinaryOverlay(modPath, Fallout4Release.Fallout4);

        progress?.Report("Enumerating records...");

        var groups = new Dictionary<string, RecordNode>(StringComparer.Ordinal);
        int count = 0;

        RecordNode GetGroup(string sig)
        {
            if (!groups.TryGetValue(sig, out var g))
            {
                g = new RecordNode { Key = sig, Parent = root };
                groups[sig] = g;
            }
            return g;
        }

        foreach (var rec in mod.EnumerateMajorRecords())
        {
            var sig = rec.Registration.Name;
            var grp = GetGroup(sig);

            var recNode = new RecordNode
            {
                Key    = !string.IsNullOrEmpty(rec.EditorID) ? rec.EditorID : rec.FormKey.ToString(),
                Parent = grp,
            };

            // Always-present header
            AddLeafInit(recNode, "FormKey",  rec.FormKey.ToString());
            AddLeafInit(recNode, "EditorID", rec.EditorID ?? "");
            AddLeafInit(recNode, "Type",     sig);

            // Generic reflection walk for record-specific fields
            WalkObject(rec, recNode, depth: 0, maxDepth: 4);

            grp.Children.Add(recNode);
            count++;
            if (count % 500 == 0) progress?.Report($"Loaded {count} records...");
        }

        // Sort groups and attach to root
        foreach (var g in groups.Values.OrderBy(g => g.Key))
        {
            g.Parent = root;
            root.Children.Add(g);
        }

        progress?.Report($"Done -- {count} records in {groups.Count} groups.");
        return root;
    }

    // ---- reflection walker ----------------------------------------------

    private static readonly HashSet<object> _visiting = [];  // cycle guard

    private static void WalkObject(object obj, RecordNode parent, int depth, int maxDepth)
    {
        if (depth > maxDepth) return;
        if (obj == null!) return;

        var type = obj.GetType();

        // Primitives / enums -> just set parent value and return
        if (_leafTypes.Contains(type) || type.IsEnum)
        {
            if (parent.IsLeaf) parent.Value = obj.ToString()!;
            return;
        }

        // Cycle guard
        if (!_visiting.Add(obj)) return;
        try
        {
            foreach (var prop in type.GetProperties(BindingFlags.Public | BindingFlags.Instance)
                                     .Where(p => p.CanRead && p.GetIndexParameters().Length == 0)
                                     .Where(p => !_skipProps.Contains(p.Name)))
            {
                object? val;
                try { val = prop.GetValue(obj); } catch { continue; }
                if (val == null) continue;

                var valType = val.GetType();

                // Skip another RecordNode ancestor type (Mutagen link etc.)
                if (val is Mutagen.Bethesda.Plugins.Records.IMajorRecordGetter) continue;

                var child = new RecordNode { Key = prop.Name, Parent = parent };

                if (_leafTypes.Contains(valType) || valType.IsEnum)
                {
                    child.Value = val.ToString()!;
                    parent.Children.Add(child);
                }
                else if (val is string s)
                {
                    child.Value = s;
                    parent.Children.Add(child);
                }
                else if (val is Mutagen.Bethesda.Plugins.IFormLinkIdentifier fli)
                {
                    child.Value = fli.FormKey.ToString();
                    parent.Children.Add(child);
                }
                else if (val is IEnumerable enumerable and not string)
                {
                    var items = new RecordNode { Key = prop.Name, Parent = parent };
                    int i = 0;
                    foreach (var item in enumerable)
                    {
                        if (item == null) continue;
                        var entry = new RecordNode { Key = $"[{i++}]", Parent = items };
                        WalkObject(item, entry, depth + 1, maxDepth);
                        if (entry.IsLeaf && string.IsNullOrEmpty(entry.Value)) continue;
                        items.Children.Add(entry);
                        if (i > 500) { AddLeafInit(items, "...", $"(truncated at 500)"); break; }
                    }
                    if (items.Children.Count > 0) parent.Children.Add(items);
                }
                else
                {
                    WalkObject(val, child, depth + 1, maxDepth);
                    if (!child.IsLeaf || !string.IsNullOrEmpty(child.Value))
                        parent.Children.Add(child);
                }
            }
        }
        finally
        {
            _visiting.Remove(obj);
        }
    }

    // ---- helpers --------------------------------------------------------

    private static void AddLeafInit(RecordNode parent, string key, string value)
    {
        var node = new RecordNode { Key = key, Parent = parent };
        // Set via backing field to avoid marking dirty on initial load
        typeof(RecordNode)
            .GetField("_value", BindingFlags.NonPublic | BindingFlags.Instance)
            ?.SetValue(node, value);
        parent.Children.Add(node);
    }
}
