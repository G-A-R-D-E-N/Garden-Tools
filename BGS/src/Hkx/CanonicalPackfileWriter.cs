using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;

namespace OpenCommonwealth.Services.Hkx;

public static class CanonicalPackfileWriter
{
    public sealed record Resize(int At, int Elements);

    private sealed record Part(string Kind, int Align, int Stride, int At, int SourceLength, int Length,
                                        int? HeaderAt, bool Relative);

    private sealed record Growth(int Part, int At, int Stride, int Template, int SourceCount, int Count);

    private const int ObjectAlign = 16;

    private const int ArrayAlign = 16;

    private const int StringAlign = 2;

    private const int HeaderSize = 16;

    private const int MaxDepth = 8;

    private static readonly string[] MetaClasses =
        { "hkClass", "hkClassMember", "hkClassEnum", "hkClassEnumItem" };

    public static byte[]? Write(PackfileImage image, IReadOnlyList<Resize>? resizes = null,
                                HavokClassTypes? types = null)
    {
        types ??= HavokClassTypes.Shipped;

        var data = image.Section("__data__");
        var names = image.Section("__classnames__");
        if (data == null || names == null) return null;

        PackfileObjects objects;
        try { objects = new PackfileObjects(image); }
        catch (InvalidOperationException) { return null; }

        if (objects.Instances.Count == 0) return null;
        if (objects.Instances.Any(i => !types.Knows(i.ClassName))) return null;

        int pointer = objects.PointerWidth;
        var aims = new Dictionary<int, int>();
        foreach (var (source, destination) in data.Locals()) aims.TryAdd(source, destination);
        foreach (var (source, section, destination) in data.Globals())
            if (section == image.Sections.IndexOf(data)) aims.TryAdd(source, destination);

        var parts = new List<Part>();
        var strings = new HashSet<int>();

        foreach (var instance in objects.Instances)
        {
            int size = types[instance.ClassName]?.Size ?? 0;
            if (size <= 0) return null;

            parts.Add(new Part("object", ObjectAlign, 0, instance.Offset, size, size, null, false));
            Extras(parts, strings, objects, types, aims, instance.Offset, instance.ClassName, pointer, 0);
        }

        foreach (var gap in Gaps(parts, data.Data.Length))
        {
            int after = -1;
            for (int k = 0; k < parts.Count; k++)
                if (parts[k].Length > 0 && parts[k].At + parts[k].SourceLength == gap.At) { after = k; break; }

            parts.Insert(after + 1, new Part("opaque", 1, 0, gap.At, gap.Length, gap.Length, null, false));
        }

        var planned = Plan(parts, resizes);
        if (planned == null) return null;

        var at = Place(parts);
        int end = parts.Count == 0 ? 0 : at[^1] + parts[^1].Length;
        var written = new byte[AlignTo(end, ObjectAlign)];

        for (int k = 0; k < parts.Count; k++) Copy(parts[k], data.Data, at[k], written);


        var moved = Moved(parts, at);
        foreach (var part in parts)
        {
            if (part.HeaderAt is not int header || part.Stride <= 0 || part.Relative) continue;
            if (!moved.TryGetValue(header, out int movedHeader)) continue;
            BitConverter.GetBytes(part.Length / part.Stride).CopyTo(written, movedHeader + pointer);
        }

        int sectionIndex = image.Sections.IndexOf(data);

        var globals = Globals(parts, planned, at, moved, objects, types, data, sectionIndex);
        if (globals == null) return null;

        var locals = Locals(planned, at, moved, objects, types, data);
        if (locals == null) return null;

        var classNames = ClassNames(objects);
        var nameAt = NameOffsets(classNames);
        if (nameAt == null) return null;

        var virtuals = new List<(int Source, int Section, int Destination)>();
        foreach (var instance in objects.Instances)
        {
            if (!moved.TryGetValue(instance.Offset, out int source)) return null;
            if (!nameAt.TryGetValue(instance.ClassName, out int nameOffset)) return null;
            virtuals.Add((source, image.Sections.IndexOf(names), nameOffset));
        }

        data.Data = written;
        data.SetGlobals(globals);
        data.SetLocals(locals);
        data.SetVirtuals(virtuals);
        names.Data = classNames;
        return image.Rebuild();
    }

    private static void Extras(List<Part> parts, HashSet<int> strings, PackfileObjects objects,
                               HavokClassTypes types, Dictionary<int, int> aims, int offset,
                               string className, int pointer, int depth)
    {

        if (depth > MaxDepth) return;

        foreach (var member in types.Members(className).OrderBy(m => m.Offset))
        {
            if (!member.Written) continue;
            int at = offset + member.Offset;
            int copies = Math.Max(1, member.ArrSize);

            if (member.VType is "TYPE_STRINGPTR" or "TYPE_CSTRING")
            {
                for (int i = 0; i < copies; i++)
                    Text(parts, strings, objects, aims, at + i * pointer, "string");
                continue;
            }

            if (member.VType == "TYPE_STRUCT")
            {
                if (member.CType == null || !types.Knows(member.CType)) continue;

                int stride = types[member.CType]?.Size ?? 0;
                if (stride <= 0 && copies > 1) continue;

                for (int i = 0; i < copies; i++)
                    Extras(parts, strings, objects, types, aims, at + i * stride, member.CType, pointer,
                           depth + 1);
                continue;
            }

            if (member.VType is not ("TYPE_ARRAY" or "TYPE_SIMPLEARRAY" or "TYPE_RELARRAY")) continue;

            for (int i = 0; i < copies; i++)
            {
                int header = at + i * HeaderSize;
                var array = objects.ArrayAt(header);
                if (array == null || array.Count == 0) continue;

                if (member.VSub == "TYPE_STRUCT" && member.CType != null && types.Knows(member.CType))
                {
                    int stride = types[member.CType]?.Size ?? 0;
                    if (stride <= 0) continue;

                    parts.Add(new Part("struct array", ArrayAlign, stride, array.At, array.Count * stride,
                                       array.Count * stride, header, member.VType == "TYPE_RELARRAY"));

                    for (int e = 0; e < array.Count; e++)
                        Extras(parts, strings, objects, types, aims, array.At + e * stride, member.CType,
                               pointer, depth + 1);
                    continue;
                }

                int width = member.VSub == "TYPE_VARIANT" ? 2 * pointer
                          : member.VSub is "TYPE_POINTER" or "TYPE_STRINGPTR" or "TYPE_CSTRING" or "TYPE_ULONG"
                              ? pointer
                          : HavokClassTypes.Width(member.VSub);
                if (width <= 0) continue;

                string kind = member.VSub == "TYPE_POINTER" ? "pointer array"
                            : member.VSub is "TYPE_STRINGPTR" or "TYPE_CSTRING" ? "string array"
                            : member.VSub == "TYPE_VARIANT" ? "variant array"
                            : "value array";

                parts.Add(new Part(kind, ArrayAlign, width, array.At, array.Count * width,
                                   array.Count * width, header, member.VType == "TYPE_RELARRAY"));

                if (member.VSub is not ("TYPE_STRINGPTR" or "TYPE_CSTRING")) continue;

                for (int e = 0; e < array.Count; e++)
                    Text(parts, strings, objects, aims, array.At + e * pointer, "element string");
            }
        }
    }

    private static void Text(List<Part> parts, HashSet<int> strings, PackfileObjects objects,
                             Dictionary<int, int> aims, int site, string kind)
    {
        if (!aims.TryGetValue(site, out int text)) return;
        if (!strings.Add(text)) return;

        int length = objects.RunToNull(text);
        if (length <= 0) return;

        parts.Add(new Part(kind, StringAlign, 0, text, length, length, null, false));
    }

    private static List<Growth>? Plan(List<Part> parts, IReadOnlyList<Resize>? resizes)
    {
        var growths = new List<Growth>();
        if (resizes == null) return growths;

        foreach (var resize in resizes)
        {
            int index = parts.FindIndex(p => p.At == resize.At &&
                                             p.Kind.EndsWith("array", StringComparison.Ordinal));
            if (index < 0 || resize.Elements < 0) return null;

            var part = parts[index];
            if (part.Stride <= 0 || part.Relative || part.SourceLength % part.Stride != 0) return null;

            int sourceCount = part.SourceLength / part.Stride;
            if (resize.Elements > sourceCount && sourceCount == 0) return null;

            parts[index] = part with { Length = resize.Elements * part.Stride };

            if (resize.Elements > sourceCount)
                growths.Add(new Growth(index, part.At, part.Stride,
                                       part.At + part.SourceLength - part.Stride, sourceCount,
                                       resize.Elements));
        }

        return growths;
    }

    private static List<(int At, int Length)> Gaps(List<Part> parts, int length)
    {
        var gaps = new List<(int At, int Length)>();
        int covered = 0;

        foreach (var part in parts.Where(p => p.Length > 0).OrderBy(p => p.At))
        {
            if (part.At > covered) gaps.Add((covered, part.At - covered));
            covered = Math.Max(covered, part.At + part.SourceLength);
        }

        if (covered < length) gaps.Add((covered, length - covered));
        return gaps;
    }

    private static int[] Place(List<Part> parts)
    {
        var at = new int[parts.Count];
        int cursor = 0;

        for (int k = 0; k < parts.Count; k++)
        {
            cursor = AlignTo(cursor, parts[k].Align);
            at[k] = cursor;
            cursor += parts[k].Length;
        }

        return at;
    }

    private static void Copy(Part part, byte[] source, int to, byte[] into)
    {
        int length = Math.Min(part.SourceLength, part.Length);
        Array.Copy(source, part.At, into, to, length);
        if (part.Length <= part.SourceLength || part.Stride <= 0) return;

        for (int filled = length; filled < part.Length; filled += part.Stride)
            Array.Copy(source, part.At + part.SourceLength - part.Stride, into, to + filled,
                       Math.Min(part.Stride, part.Length - filled));
    }

    private static Dictionary<int, int> Moved(List<Part> parts, int[] at)
    {
        var moved = new Dictionary<int, int>();

        for (int k = 0; k < parts.Count; k++)
        {
            var part = parts[k];
            int count = Math.Min(part.SourceLength, part.Length);
            for (int i = 0; i < count; i++) moved.TryAdd(part.At + i, at[k] + i);
        }

        return moved;
    }

    private static List<(int Source, int Section, int Destination)>? Globals(
        List<Part> parts, List<Growth> growths, int[] at, Dictionary<int, int> moved,
        PackfileObjects objects, HavokClassTypes types, PackfileSection data, int sectionIndex)
    {
        var table = new Dictionary<int, (int Section, int Destination)>();
        foreach (var (source, section, destination) in data.Globals())
            table.TryAdd(source, (section, destination));

        var order = FixupOrder.Sources(objects, types, data, global: true);
        var after = GrowthPoints(growths, order);

        var result = new List<(int Source, int Section, int Destination)>();
        var used = new HashSet<int>();

        for (int i = 0; i < order.Count; i++)
        {
            int source = order[i];
            if (used.Add(source) && table.TryGetValue(source, out var aim))
            {
                if (!moved.TryGetValue(source, out int movedSource)) continue;

                int destination = aim.Destination;
                if (aim.Section == sectionIndex && !moved.TryGetValue(destination, out destination))
                    return null;

                result.Add((movedSource, aim.Section, destination));
            }

            if (after.TryGetValue(i, out var pending))
                foreach (var growth in pending)
                    foreach (int oldSource in Template(table, growth))
                    {
                        var template = table[oldSource];
                        int destination = template.Destination;
                        if (template.Section == sectionIndex && !moved.TryGetValue(destination, out destination))
                            return null;

                        for (int e = growth.SourceCount; e < growth.Count; e++)
                            result.Add((at[growth.Part] + e * growth.Stride + (oldSource - growth.Template),
                                        template.Section, destination));
                    }
        }

        foreach (var (source, aim) in table)
        {
            if (used.Contains(source)) continue;
            if (!moved.TryGetValue(source, out int movedSource)) continue;

            int destination = aim.Destination;
            if (aim.Section == sectionIndex && !moved.TryGetValue(destination, out destination))
                return null;

            result.Add((movedSource, aim.Section, destination));
        }

        return result;
    }

    private static List<(int Source, int Destination)>? Locals(
        List<Growth> growths, int[] at, Dictionary<int, int> moved, PackfileObjects objects,
        HavokClassTypes types, PackfileSection data)
    {
        var table = new Dictionary<int, int>();
        foreach (var (source, destination) in data.Locals()) table.TryAdd(source, destination);

        var order = FixupOrder.Sources(objects, types, data, global: false);
        var after = GrowthPoints(growths, order);

        var result = new List<(int Source, int Destination)>();
        var used = new HashSet<int>();

        for (int i = 0; i < order.Count; i++)
        {
            int source = order[i];
            if (used.Add(source) && table.TryGetValue(source, out int destination))
            {
                if (!moved.TryGetValue(source, out int movedSource)) continue;
                if (!moved.TryGetValue(destination, out int movedDestination)) return null;

                result.Add((movedSource, movedDestination));
            }

            if (after.TryGetValue(i, out var pending))
                foreach (var growth in pending)
                    foreach (int oldSource in Template(table, growth))
                    {
                        if (!moved.TryGetValue(table[oldSource], out int movedDestination)) return null;

                        for (int e = growth.SourceCount; e < growth.Count; e++)
                            result.Add((at[growth.Part] + e * growth.Stride + (oldSource - growth.Template),
                                        movedDestination));
                    }
        }

        foreach (var (source, destination) in table)
        {
            if (used.Contains(source)) continue;
            if (!moved.TryGetValue(source, out int movedSource)) continue;
            if (!moved.TryGetValue(destination, out int movedDestination)) return null;

            result.Add((movedSource, movedDestination));
        }

        return result;
    }

    private static Dictionary<int, List<Growth>> GrowthPoints(List<Growth> growths, List<int> order)
    {
        var after = new Dictionary<int, List<Growth>>();

        foreach (var growth in growths)
        {
            int last = -1;
            for (int i = 0; i < order.Count; i++)
            {
                int source = order[i];
                if (source >= growth.Template && source < growth.Template + growth.Stride) last = i;
            }

            if (last < 0) continue;
            if (!after.TryGetValue(last, out var list)) after[last] = list = new List<Growth>();
            list.Add(growth);
        }

        return after;
    }

    private static List<int> Template<T>(Dictionary<int, T> table, Growth growth)
    {
        var found = new List<int>();
        for (int i = 0; i < growth.Stride; i++)
        {
            int source = growth.Template + i;
            if (table.ContainsKey(source)) found.Add(source);
        }

        return found;
    }

    private static byte[] ClassNames(PackfileObjects objects)
    {
        var seen = new Dictionary<string, uint>(StringComparer.Ordinal);
        foreach (var (signature, name) in objects.ClassNames()) seen.TryAdd(name, signature);

        var ordered = new List<string>();
        foreach (string meta in MetaClasses)
            if (seen.ContainsKey(meta)) ordered.Add(meta);

        foreach (var instance in objects.Instances)
            if (seen.ContainsKey(instance.ClassName) && !ordered.Contains(instance.ClassName))
                ordered.Add(instance.ClassName);

        foreach (string name in seen.Keys.OrderBy(n => n, StringComparer.Ordinal))
            if (!ordered.Contains(name)) ordered.Add(name);

        var blob = new List<byte>();
        foreach (string name in ordered)
        {
            blob.AddRange(BitConverter.GetBytes(seen[name]));
            blob.Add(0x09);
            blob.AddRange(Encoding.ASCII.GetBytes(name));
            blob.Add(0x00);
        }

        return blob.ToArray();
    }

    private static Dictionary<string, int>? NameOffsets(byte[] table)
    {
        var at = new Dictionary<string, int>(StringComparer.Ordinal);
        for (int i = 0; i + 5 <= table.Length; )
        {
            int end = Array.IndexOf(table, (byte)0, i + 5);
            if (end < 0) return null;
            at[Encoding.ASCII.GetString(table, i + 5, end - i - 5)] = i + 5;
            i = end + 1;
        }

        return at;
    }

    private static int AlignTo(int value, int to) => to <= 1 ? value : (value + to - 1) / to * to;
}
