using System;
using System.Collections.Generic;
using System.Linq;

namespace OpenCommonwealth.Services.Hkx;

public static class PackfileLayout
{

    public sealed record Item(string Kind, int At, int Length)
    {
        public bool IsString => Kind is "string" or "element string";

        // Relative-array payloads must stay on 16-byte lines when Align is set.
        public int Align;

        public int HeaderAt = -1;

        public override string ToString() => $"{Kind} at 0x{At:x} for {Length}";
    }

    public static List<Item>? Of(PackfileImage image, HavokClassTypes? types = null)
    {
        types ??= HavokClassTypes.Shipped;

        var data = image.Section("__data__");
        if (data == null) return null;

        PackfileObjects objects;
        try { objects = new PackfileObjects(image); }
        catch (InvalidOperationException) { return null; }

        if (objects.Instances.Any(i => !types.Knows(i.ClassName))) return null;

        var aims = new Dictionary<int, int>();
        foreach (var (source, destination) in data.Locals()) aims[source] = destination;

        var items = new List<Item>();
        var seen = new HashSet<int>();
        bool placed = true;

        void Walk(int offset, string className, int depth)
        {

            if (depth > 8) return;

            var laid = LayoutWalker.Active(types, className, objects.PointerWidth);
            if (laid == null) return;

            foreach (var member in types.Members(className)
                                       .OrderBy(m => laid.OffsetOf(m.Name) ?? m.Offset))
            {
                if (!member.Written) continue;
                int at = offset + (laid.OffsetOf(member.Name) ?? member.Offset);

                if (member.VType is "TYPE_STRINGPTR" or "TYPE_CSTRING")
                {
                    if (aims.TryGetValue(at, out int text) && seen.Add(text))
                        items.Add(new Item("string", text, Zeroed(data.Data, text)));
                    continue;
                }

                if (member.VType == "TYPE_STRUCT")
                {
                    if (member.CType != null && types.Knows(member.CType))
                        Walk(at, member.CType, depth + 1);
                    continue;
                }

                if (member.VType == "TYPE_RELARRAY")
                {
                    RelArray(at, member, depth);
                    continue;
                }

                if (member.VType is not ("TYPE_ARRAY" or "TYPE_SIMPLEARRAY")) continue;
                if (!aims.ContainsKey(at)) continue;

                if (member.VSub == "TYPE_STRUCT" && member.CType != null && types.Knows(member.CType))
                {
                    int stride = LayoutWalker.Active(types, member.CType, objects.PointerWidth)?.Size ?? 0;
                    if (stride <= 0) continue;

                    var structArray = objects.ArrayAt(at, stride);
                    if (structArray == null || structArray.Count == 0) continue;

                    if (seen.Add(structArray.At))
                        items.Add(new Item("struct array", structArray.At, structArray.Count * stride));

                    for (int i = 0; i < structArray.Count; i++)
                        Walk(structArray.At + i * stride, member.CType, depth + 1);
                    continue;
                }

                // hkUlong elements are pointer-sized and variants are two pointers, so both
                // arrays stride at the active pointer width, not the shipped eight-byte width.
                int width = ElementWidth(member, objects.PointerWidth);
                if (width <= 0) continue;

                var array = objects.ArrayAt(at, width);
                if (array == null || array.Count == 0) continue;

                string kind = member.VSub == "TYPE_POINTER" ? "pointer array"
                            : member.VSub is "TYPE_STRINGPTR" or "TYPE_CSTRING" ? "string array"
                            : member.VSub == "TYPE_VARIANT" ? "variant array"
                            : "value array";

                if (seen.Add(array.At)) items.Add(new Item(kind, array.At, array.Count * width));

                if (member.VSub is not ("TYPE_STRINGPTR" or "TYPE_CSTRING")) continue;

                for (int i = 0; i < array.Count; i++)
                    if (aims.TryGetValue(array.At + i * objects.PointerWidth, out int text) && seen.Add(text))
                        items.Add(new Item("element string", text, Zeroed(data.Data, text)));
            }
        }

        void RelArray(int at, HavokClassTypes.Member member, int depth)
        {
            if (objects.ReadIntAt(at) is not int raw) { placed = false; return; }
            if ((raw & 0xFFFF) == 0) return;

            string? elementClass = member.VSub == "TYPE_STRUCT" && member.CType != null && types.Knows(member.CType)
                                 ? member.CType
                                 : null;
            if (member.VSub == "TYPE_STRUCT" && elementClass == null) { placed = false; return; }

            int width = elementClass != null
                ? LayoutWalker.Active(types, elementClass, objects.PointerWidth)?.Size ?? 0
                : ElementWidth(member, objects.PointerWidth);
            var rel = width > 0 ? objects.RelArrayAt(at, width) : null;
            if (rel == null) { placed = false; return; }
            if (rel.Count == 0) return;

            items.Add(new Item("relarray", rel.At, rel.Count * width) { HeaderAt = at, Align = NativeAppend.Alignment });
            if (elementClass == null) return;

            for (int e = 0; e < rel.Count; e++)
                Walk(rel.At + e * width, elementClass, depth + 1);
        }

        foreach (var instance in objects.Instances)
        {
            var laid = LayoutWalker.Active(types, instance.ClassName, objects.PointerWidth);
            if (laid == null || laid.Size <= 0) return null;

            items.Add(new Item("object", instance.Offset, laid.Size));
            Walk(instance.Offset, instance.ClassName, 0);
        }

        if (!placed) return null;

        foreach (var relative in items.Where(i => i.Kind == "relarray"))
            if (items.Any(o => !ReferenceEquals(o, relative) &&
                               o.At < relative.At + relative.Length && relative.At < o.At + o.Length))
                return null;

        return items;
    }

    public static List<int> Where(IReadOnlyList<Item> items)
    {
        var at = new List<int>(items.Count);
        int cursor = 0;
        string previous = "";

        foreach (var item in items)
        {
            cursor = Align(cursor, item.Align > 0 ? item.Align : Boundary(item.Kind, previous));
            at.Add(cursor);
            cursor += item.Length;
            previous = item.Kind;
        }

        return at;
    }

    public static bool Rewrite(PackfileImage image, HavokClassTypes? types = null)
    {
        var data = image.Section("__data__");
        if (data == null) return false;

        var items = Of(image, types);
        if (items == null) return false;

        if (!Accounted(items, data.Data.Length)) return false;

        return RewriteAs(image, items);
    }

    public static bool RewriteAs(PackfileImage image, IReadOnlyList<Item> items)
    {
        var data = image.Section("__data__");
        if (data == null) return false;

        var at = Where(items);

        var byOldOffset = items.Select((item, k) => (item, To: at[k]))
                               .OrderBy(x => x.item.At).ToList();

        int? Moved(int offset)
        {
            int low = 0, high = byOldOffset.Count - 1;
            while (low <= high)
            {
                int mid = (low + high) / 2;
                var (item, to) = byOldOffset[mid];

                if (offset < item.At) high = mid - 1;
                else if (offset >= item.At + item.Length) low = mid + 1;
                else return to + (offset - item.At);
            }
            return null;
        }

        int self = image.Sections.IndexOf(data);
        if (image.ContentsClassNameSectionIndex == self) return false;

        int? contentsAt = null;
        if (image.ContentsSectionIndex == self)
        {
            if (Moved(image.ContentsSectionOffset) is not int movedRoot) return false;
            contentsAt = movedRoot;
        }

        var distances = new List<(int Header, int Distance)>();
        for (int k = 0; k < items.Count; k++)
        {
            if (items[k].HeaderAt < 0) continue;
            if (Moved(items[k].HeaderAt) is not int header) return false;

            long distance = (long)at[k] - header;
            if (distance <= 0 || distance > ushort.MaxValue) return false;
            distances.Add((header, (int)distance));
        }

        int end = items.Count == 0 ? 0 : at[^1] + items[^1].Length;
        var written = new byte[Align(end, NativeAppend.Alignment)];

        for (int k = 0; k < items.Count; k++)
        {
            if (items[k].At + items[k].Length > data.Data.Length) return false;
            Array.Copy(data.Data, items[k].At, written, at[k], items[k].Length);
        }

        var locals = new List<(int Source, int Destination)>();
        foreach (var (source, destination) in data.Locals())
        {
            if (Moved(source) is not int from || Moved(destination) is not int to) return false;
            locals.Add((from, to));
        }

        var globals = new List<(int Source, int Section, int Destination)>();
        foreach (var (source, section, destination) in data.Globals())
        {
            if (Moved(source) is not int from) return false;

            int to = section == image.Sections.IndexOf(data)
                     ? Moved(destination) ?? -1
                     : destination;
            if (to < 0) return false;

            globals.Add((from, section, to));
        }

        var virtuals = new List<(int Source, int Section, int Destination)>();
        foreach (var (source, section, destination) in data.Virtuals())
        {
            if (Moved(source) is not int from) return false;
            virtuals.Add((from, section, destination));
        }

        var relinked = new List<(PackfileSection Section, List<(int Source, int Section, int Destination)> Entries)>();
        foreach (var other in image.Sections)
        {
            if (other == data || !other.Globals().Any(g => g.Section == self)) continue;

            var entries = new List<(int Source, int Section, int Destination)>();
            foreach (var (source, section, destination) in other.Globals())
            {
                if (section != self)
                {
                    entries.Add((source, section, destination));
                    continue;
                }

                if (Moved(destination) is not int target) return false;
                entries.Add((source, section, target));
            }
            relinked.Add((other, entries));
        }

        foreach (var (header, distance) in distances)
            BitConverter.GetBytes((ushort)distance).CopyTo(written, header + 2);

        data.Data = written;
        data.SetLocals(locals);
        data.SetGlobals(globals);
        data.SetVirtuals(virtuals);
        foreach (var (other, entries) in relinked) other.SetGlobals(entries);
        if (contentsAt is int relocated) image.ContentsSectionOffset = relocated;
        return true;
    }

    public static List<List<Item>> ByObject(IReadOnlyList<Item> items)
    {
        var runs = new List<List<Item>>();

        foreach (var item in items)
        {
            if (item.Kind == "object" || runs.Count == 0) runs.Add(new List<Item>());
            runs[^1].Add(item);
        }

        return runs;
    }

    public static bool Reaches(IReadOnlyList<Item> items, PackfileSection data, int section)
    {
        var spans = items.Select(i => (i.At, End: i.At + i.Length)).OrderBy(x => x.At).ToList();

        bool Lands(int offset)
        {
            int low = 0, high = spans.Count - 1;
            while (low <= high)
            {
                int mid = (low + high) / 2;
                if (offset < spans[mid].At) high = mid - 1;
                else if (offset >= spans[mid].End) low = mid + 1;
                else return true;
            }
            return false;
        }

        foreach (var (source, destination) in data.Locals())
            if (!Lands(source) || !Lands(destination)) return false;

        foreach (var (source, which, destination) in data.Globals())
            if (!Lands(source) || (which == section && !Lands(destination))) return false;

        foreach (var (source, _, _) in data.Virtuals())
            if (!Lands(source)) return false;

        return true;
    }

    public static bool Accounted(IReadOnlyList<Item> items, int length)
    {
        int covered = 0;
        foreach (var item in items.OrderBy(i => i.At))
        {
            if (item.At - covered >= NativeAppend.Alignment) return false;
            covered = Math.Max(covered, item.At + item.Length);
        }

        return length - covered < NativeAppend.Alignment;
    }

    private static int ElementWidth(HavokClassTypes.Member member, int pointerWidth) => member.VSub switch
    {
        "TYPE_VARIANT" => 2 * pointerWidth,
        "TYPE_POINTER" or "TYPE_STRINGPTR" or "TYPE_CSTRING" or "TYPE_ULONG" => pointerWidth,
        _ => HavokClassTypes.Width(member.VSub),
    };

    private static int Boundary(string kind, string previous) => kind switch
    {
        // The default relative-array placement packs payloads right after the object's struct
        // bytes in member order. It is only a placement policy — the header's stored offset is
        // authoritative and is rewritten to wherever the payload lands — and the converter
        // overrides it with 16-byte line alignment via Item.Align when it builds its blocks.
        "relarray" => 1,

        "element string" => 2,

        "string" when previous.EndsWith("array", StringComparison.Ordinal) => 1,

        _ => NativeAppend.Alignment,
    };

    private static int Zeroed(byte[] data, int at)
    {
        int end = Array.IndexOf(data, (byte)0, at);
        return end < 0 ? data.Length - at : end - at + 1;
    }

    public static int Align(int value, int to) => (value + to - 1) / to * to;
}
