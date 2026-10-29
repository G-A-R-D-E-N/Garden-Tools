using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;

namespace OpenCommonwealth.Services.Hkx;

public static class FixupOrder
{

    private const int HeaderSize = 16;

    private const int MaxDepth = 8;

    public static void Reorder(PackfileImage image, HavokClassTypes? types = null)
    {
        types ??= HavokClassTypes.Shipped;

        var data = image.Section("__data__");
        if (data == null) return;

        var objects = new PackfileObjects(image);
        if (objects.Instances.Any(i => !types.Knows(i.ClassName))) return;

        var declared = HavokClassTypes.Shipped;

        data.SetGlobals(InWalkOrder(data.Globals().ToList(), g => g.Source,
                                    Sources(objects, types, data, global: true),
                                    s => LayoutDeclaresSite(objects, declared, s, global: true),
                                    "global"));

        data.SetLocals(InWalkOrder(data.Locals().ToList(), l => l.Source,
                                   Sources(objects, types, data, global: false),
                                   s => LayoutDeclaresSite(objects, declared, s, global: false),
                                   "local"));
    }

    private static List<T> InWalkOrder<T>(List<T> entries, Func<T, int> sourceOf, List<int> order,
                                          Func<int, bool> declared, string table)
    {
        var rank = new Dictionary<int, int>();
        for (int i = 0; i < order.Count; i++) rank.TryAdd(order[i], i);

        foreach (T entry in entries)
        {
            int source = sourceOf(entry);
            if (rank.ContainsKey(source) || !declared(source)) continue;

            throw new InvalidDataException(
                $"The {table} fixup table holds a site at 0x{source:x} that the class layout " +
                "declares, but the object walk did not place it, so the table cannot be reordered.");
        }

        return entries.Select((e, at) => (e, key: rank.TryGetValue(sourceOf(e), out int r) ? r : int.MaxValue, at))
                      .OrderBy(x => x.key).ThenBy(x => x.at)
                      .Select(x => x.e).ToList();
    }

    public static List<int> Sources(PackfileObjects objects, HavokClassTypes types,
                                    PackfileSection data, bool global)
    {
        var present = new HashSet<int>(global ? data.Globals().Select(g => g.Source)
                                              : data.Locals().Select(l => l.Source));
        var found = new List<int>();

        foreach (var instance in objects.Instances)
            Walk(objects, types, instance.Offset, instance.ClassName, present, found, global, 0);

        return found;
    }

    private static void Walk(PackfileObjects objects, HavokClassTypes types, int offset,
                             string className, HashSet<int> present, List<int> into,
                             bool global, int depth)
    {

        if (depth > MaxDepth) return;

        foreach (var member in types.Members(className).OrderBy(m => m.Offset))
        {
            if (!member.Written) continue;
            int at = offset + member.Offset;
            int copies = Math.Max(1, member.ArrSize);

            if (member.VType == "TYPE_POINTER")
            {
                if (!global) continue;
                for (int i = 0; i < copies; i++)
                {
                    int slot = at + i * objects.PointerWidth;
                    if (present.Contains(slot)) into.Add(slot);
                }
                continue;
            }

            if (member.VType is "TYPE_STRINGPTR" or "TYPE_CSTRING")
            {
                if (global) continue;
                for (int i = 0; i < copies; i++)
                {
                    int slot = at + i * objects.PointerWidth;
                    if (present.Contains(slot)) into.Add(slot);
                }
                continue;
            }

            if (member.VType == "TYPE_STRUCT")
            {
                if (member.CType == null || !types.Knows(member.CType)) continue;

                int stride = types[member.CType]?.Size ?? 0;
                if (stride <= 0 && copies > 1) continue;

                for (int i = 0; i < copies; i++)
                    Walk(objects, types, at + i * stride, member.CType, present, into, global,
                         depth + 1);
                continue;
            }

            if (member.VType is not ("TYPE_ARRAY" or "TYPE_SIMPLEARRAY" or "TYPE_RELARRAY")) continue;

            for (int i = 0; i < copies; i++)
            {
                int header = at + i * HeaderSize;

                if (!global && present.Contains(header)) into.Add(header);

                var array = objects.ArrayAt(header);
                if (array == null || array.Count == 0) continue;

                if (member.VSub == "TYPE_POINTER")
                {
                    if (!global) continue;
                    for (int e = 0; e < array.Count; e++)
                        if (present.Contains(array.At + e * 8)) into.Add(array.At + e * 8);
                }
                else if (member.VSub is "TYPE_STRINGPTR" or "TYPE_CSTRING")
                {
                    if (global) continue;
                    for (int e = 0; e < array.Count; e++)
                        if (present.Contains(array.At + e * 8)) into.Add(array.At + e * 8);
                }
                else if (member.VSub == "TYPE_STRUCT" && member.CType != null && types.Knows(member.CType))
                {
                    int stride = types[member.CType]?.Size ?? 0;
                    if (stride <= 0) continue;

                    for (int e = 0; e < array.Count; e++)
                        Walk(objects, types, array.At + e * stride, member.CType, present, into,
                             global, depth + 1);
                }
            }
        }
    }

    private static bool LayoutDeclaresSite(PackfileObjects objects, HavokClassTypes types, int source,
                                           bool global)
    {
        foreach (var instance in objects.Instances)
            if (DeclaresSiteAt(objects, types, instance.Offset, instance.ClassName, source, global, 0))
                return true;

        return false;
    }

    private static bool DeclaresSiteAt(PackfileObjects objects, HavokClassTypes types, int offset,
                                       string className, int target, bool global, int depth)
    {

        if (depth > MaxDepth) return false;

        foreach (var member in types.Members(className))
        {
            if (!member.Written) continue;
            int at = offset + member.Offset;
            int copies = Math.Max(1, member.ArrSize);

            if (member.VType == "TYPE_POINTER")
            {
                if (!global) continue;
                for (int i = 0; i < copies; i++)
                    if (at + i * objects.PointerWidth == target) return true;
                continue;
            }

            if (member.VType is "TYPE_STRINGPTR" or "TYPE_CSTRING")
            {
                if (global) continue;
                if (at == target) return true;
                continue;
            }

            if (member.VType == "TYPE_STRUCT")
            {
                if (member.CType == null || !types.Knows(member.CType)) continue;

                int stride = types[member.CType]?.Size ?? 0;
                if (stride <= 0 && copies > 1) continue;

                for (int i = 0; i < copies; i++)
                    if (DeclaresSiteAt(objects, types, at + i * stride, member.CType, target, global,
                                       depth + 1))
                        return true;
                continue;
            }

            if (member.VType is not ("TYPE_ARRAY" or "TYPE_SIMPLEARRAY" or "TYPE_RELARRAY")) continue;
            if (global) continue;

            for (int i = 0; i < copies; i++)
                if (at + i * HeaderSize == target) return true;
        }

        return false;
    }
}
