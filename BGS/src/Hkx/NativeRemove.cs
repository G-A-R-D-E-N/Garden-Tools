using System;
using System.Collections.Generic;
using System.Linq;

namespace OpenCommonwealth.Services.Hkx;

public static class NativeRemove
{
    public sealed record Orphaned(int Id, int PointersCleared, int ElementsDropped)
    {
        public bool Reached => PointersCleared > 0 || ElementsDropped > 0;

        public override string ToString() =>
            $"#{Id}: {PointersCleared} pointer(s) cleared, {ElementsDropped} array element(s) dropped";
    }

    private readonly record struct Run(int FieldAt, int At, int Count);

    public sealed record Deleted(int Objects, int Bytes, int FixupsDropped)
    {
        public override string ToString() =>
            $"{Objects} object(s) taken out, {Bytes} byte(s) shorter, {FixupsDropped} pointer(s) dropped";
    }

    public static Deleted Delete(PackfileImage image, IReadOnlyCollection<int> ids,
                                 HavokClassTypes? types = null)
    {
        NativeLayout.RequireWritable(image);
        types ??= HavokClassTypes.Shipped;

        var data = image.Section("__data__")
                   ?? throw new InvalidOperationException("The file has no __data__ section.");

        var objects = new PackfileObjects(image, HavokClasses.Shipped);

        var items = PackfileLayout.Of(image, types)
                    ?? throw new InvalidOperationException(
                        "This file holds a class this build cannot describe, so nothing was deleted: " +
                        "working out where the rest would go needs every object accounted for.");

        int section = image.Sections.IndexOf(data);

        if (!PackfileLayout.Reaches(items, data, section))
            throw new InvalidOperationException(
                "Something in this file points at bytes the reader cannot place, so nothing was " +
                "deleted: laying it out again would drop what that pointer names.");

        var runs = PackfileLayout.ByObject(items);
        if (runs.Count != objects.Instances.Count)
            throw new InvalidOperationException(
                $"The walk found {runs.Count} object(s) where the file lists {objects.Instances.Count}, " +
                "so nothing was deleted.");

        var going = new List<int>();
        foreach (int id in ids.Distinct())
        {
            int index = id - NativeGraphModel.FirstId;
            if (index < 0 || index >= objects.Instances.Count)
                throw new InvalidOperationException(
                    $"#{id} is not in this file, which holds #{NativeGraphModel.FirstId} to " +
                    $"#{NativeGraphModel.FirstId + objects.Instances.Count - 1}.");
            going.Add(index);
        }

        if (going.Count == 0) return new Deleted(0, 0, 0);

        var leaving = new List<(int At, int End)>();
        foreach (int index in going)
            foreach (var item in runs[index])
                leaving.Add((item.At, item.At + item.Length));

        bool Inside(int offset) => leaving.Exists(r => offset >= r.At && offset < r.End);

        foreach (var holder in image.Sections)
            foreach (var (source, whichSection, destination) in holder.Globals())
            {
                if (whichSection != section || !Inside(destination)) continue;

                if (holder != data)
                    throw new InvalidOperationException(
                        $"Section {holder.Tag} still points at 0x{destination:x} in __data__, from offset " +
                        $"0x{source:x}, so nothing was deleted. Detach it first.");

                if (Inside(source)) continue;

                var pointed = objects.Instances.FirstOrDefault(i => i.Offset == destination);
                string what = pointed == null
                    ? $"0x{destination:x} inside an object being removed"
                    : $"#{NativeGraphModel.FirstId + objects.IndexOf(pointed)}";
                throw new InvalidOperationException(
                    $"Something still points at {what}, from offset " +
                    $"0x{source:x}, so nothing was deleted. Detach it first.");
            }

        foreach (var (source, destination) in data.Locals())
            if (!Inside(source) && Inside(destination))
                throw new InvalidOperationException(
                    $"The data at 0x{source:x} stays but points at 0x{destination:x}, which is being removed " +
                    "(shared data), so nothing was deleted.");

        if (image.ContentsSectionIndex == section && Inside(image.ContentsSectionOffset))
            throw new InvalidOperationException(
                "The file's root object is being removed, so nothing was deleted.");

        int before = data.Data.Length;
        int dropped = 0;

        var locals = new List<(int Source, int Destination)>();
        foreach (var entry in data.Locals())
        {
            if (Inside(entry.Source) || Inside(entry.Destination)) { dropped++; continue; }
            locals.Add(entry);
        }

        var globals = new List<(int Source, int Section, int Destination)>();
        foreach (var entry in data.Globals())
        {
            if (Inside(entry.Source)) { dropped++; continue; }
            globals.Add(entry);
        }

        var virtuals = new List<(int Source, int Section, int Destination)>();
        foreach (var entry in data.Virtuals())
        {
            if (Inside(entry.Source)) { dropped++; continue; }
            virtuals.Add(entry);
        }

        data.SetLocals(locals);
        data.SetGlobals(globals);
        data.SetVirtuals(virtuals);

        var kept = new List<PackfileLayout.Item>();
        for (int index = 0; index < runs.Count; index++)
            if (!going.Contains(index)) kept.AddRange(runs[index]);

        if (!PackfileLayout.RewriteAs(image, kept))
            throw new InvalidOperationException(
                "The file could not be laid out again after the deletion, so nothing was written.");

        return new Deleted(going.Count, before - data.Data.Length, dropped);
    }

    public static Orphaned Orphan(PackfileImage image, int id, HavokClassTypes? types = null)
    {
        NativeLayout.RequireWritable(image);
        types ??= HavokClassTypes.Shipped;

        var data = image.Section("__data__")
                   ?? throw new InvalidOperationException("The file has no __data__ section.");
        var objects = new PackfileObjects(image, HavokClasses.Shipped);

        int index = id - NativeGraphModel.FirstId;
        if (index < 0 || index >= objects.Instances.Count)
            throw new InvalidOperationException(
                $"#{id} is not in this file, which holds #{NativeGraphModel.FirstId} to " +
                $"#{NativeGraphModel.FirstId + objects.Instances.Count - 1}.");

        int target = objects.Instances[index].Offset;
        int section = image.Sections.IndexOf(data);

        var incoming = data.Globals().Where(g => g.Section == section && g.Destination == target)
                           .Select(g => g.Source).ToList();

        if (incoming.Count == 0) return new Orphaned(id, 0, 0);

        var runs = PointerRuns(objects, types);

        var inRuns = new Dictionary<int, List<int>>();
        var plain = new List<int>();

        foreach (int source in incoming)
        {
            var run = runs.FirstOrDefault(r => source >= r.At && source < r.At + r.Count * 8);
            if (run.Count == 0) { plain.Add(source); continue; }

            if (!inRuns.TryGetValue(run.FieldAt, out var hits)) inRuns[run.FieldAt] = hits = new List<int>();
            hits.Add((source - run.At) / 8);
        }

        int dropped = 0;
        var plans = new List<(Run Run, List<(int Section, int Destination)> Keep)>();
        foreach (var (fieldAt, hits) in inRuns)
        {
            var run = runs.First(r => r.FieldAt == fieldAt);
            var keep = new List<(int Section, int Destination)>();

            for (int e = 0; e < run.Count; e++)
            {
                if (hits.Contains(e)) { dropped++; continue; }

                int site = run.At + e * 8;
                if (data.Locals().Any(l => l.Source == site))
                    throw new InvalidOperationException(
                        $"The array at 0x{run.At:x} holds a local pointer at 0x{site:x} that cannot be " +
                        "carried over, so nothing was orphaned.");

                (int Section, int Destination)? held = null;
                foreach (var g in data.Globals())
                    if (g.Source == site) held = (g.Section, g.Destination);
                keep.Add(held ?? (0, -1));
            }

            plans.Add((run, keep));
        }

        foreach (int source in plain) data.SetGlobal(source, 0, -1);

        foreach (var (run, keep) in plans) Shrink(image, data, run.FieldAt, run, keep);

        FixupOrder.Reorder(image, types);
        return new Orphaned(id, plain.Count, dropped);
    }

    private static void Shrink(PackfileImage image, PackfileSection data, int fieldAt,
                               Run run, List<(int Section, int Destination)> keep)
    {
        int at = keep.Count == 0 ? -1 : data.AppendData(new byte[keep.Count * 8]);
        data.SetLocal(fieldAt, at);

        var entries = data.Globals().ToList();
        int from = run.At, to = run.At + run.Count * 8;
        int first = entries.FindIndex(e => e.Source >= from && e.Source < to);
        if (first < 0) first = entries.Count;
        entries.RemoveAll(e => e.Source >= from && e.Source < to);

        var replacements = new List<(int, int, int)>();
        for (int e = 0; e < keep.Count; e++)
            if (keep[e].Destination >= 0) replacements.Add((at + e * 8, keep[e].Section, keep[e].Destination));

        entries.InsertRange(Math.Min(first, entries.Count), replacements);
        data.SetGlobals(entries);

        BitConverter.GetBytes(keep.Count).CopyTo(data.Data, fieldAt + 8);
        uint capacity = BitConverter.ToUInt32(data.Data, fieldAt + 12);
        BitConverter.GetBytes((capacity & 0xC0000000u) | (uint)keep.Count).CopyTo(data.Data, fieldAt + 12);
    }

    private static List<Run> PointerRuns(PackfileObjects objects, HavokClassTypes types)
    {
        var runs = new List<Run>();
        foreach (var instance in objects.Instances)
            Collect(objects, types, instance.Offset, instance.ClassName, runs, 0);
        return runs;
    }

    private static void Collect(PackfileObjects objects, HavokClassTypes types, int offset,
                                string className, List<Run> runs, int depth)
    {

        if (depth > 8 || !types.Knows(className)) return;

        foreach (var member in types.Members(className))
        {
            if (!member.Written) continue;
            int at = offset + member.Offset;

            if (member.VType == "TYPE_STRUCT")
            {
                if (member.CType != null) Collect(objects, types, at, member.CType, runs, depth + 1);
                continue;
            }

            if (member.VType is not ("TYPE_ARRAY" or "TYPE_SIMPLEARRAY" or "TYPE_RELARRAY")) continue;

            var array = objects.ArrayAt(at);
            if (array == null || array.Count == 0) continue;

            if (member.VSub == "TYPE_POINTER") { runs.Add(new Run(at, array.At, array.Count)); continue; }

            if (member.VSub != "TYPE_STRUCT" || member.CType == null) continue;

            int stride = types[member.CType]?.Size ?? 0;
            if (stride <= 0) continue;

            for (int e = 0; e < array.Count; e++)
                Collect(objects, types, array.At + e * stride, member.CType, runs, depth + 1);
        }
    }
}
