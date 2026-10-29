using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;

namespace OpenCommonwealth.Services.Hkx;

internal sealed record SkeletonMapperChange(SkeletonMapperNode Edited, SkeletonMapperNode Original);

internal static class SkeletonMapperWriteback
{
    private const string MapperClass = "hkaSkeletonMapper";
    private const string DataClass = "hkaSkeletonMapperData";
    private const string SimpleRowClass = "hkaSkeletonMapperDataSimpleMapping";
    private const string ChainRowClass = "hkaSkeletonMapperDataChainMapping";
    private const uint EmbeddedArrayStorage = 0x80000000u;

    internal static byte[] Write(byte[] source, IReadOnlyList<SkeletonMapperNode> nodes,
                                 IReadOnlyList<SkeletonMapperChange> changed, bool force = false)
    {
        var image = PackfileImage.Read(source);
        NativeLayout.RequireWritable(image);

        var objects = new PackfileObjects(image);
        var mismatched = HavokClassTypes.Shipped.SignatureProblems(objects.ClassNames());
        if (mismatched.Count > 0)
            throw new InvalidOperationException(
                "This file's classes are not the ones this build describes, so nothing was written " +
                $"into its bytes: {mismatched[0]}");

        var data = image.Section(PackfileObjects.DataSection)
            ?? throw new InvalidOperationException("this file has no data section, so nothing was written");
        int section = image.Sections.IndexOf(data);

        if (force && changed.Count > 0)
            throw new InvalidOperationException("a forced no-op rebuild cannot carry edits, so nothing was written");

        var edits = force ? NoOpEdits(nodes) : changed;
        foreach (var change in edits) RequireComplete(change);

        if (!edits.Any(change => Structural(change)))
        {
            var before = (byte[])data.Data.Clone();
            var ranges = new List<(int Start, int Length)>();
            foreach (var change in edits)
            {
                RequireStorage(image, objects, change.Edited, change.Edited.Rows.Count,
                               change.Edited.ChainMappings.Count);
                WriteValues(data.Data, objects, change.Edited, change.Original, ranges);
            }

            RequireConfined(data.Data, before, ranges);
            var patched = image.Rebuild();
            Verify(patched, nodes);
            return patched;
        }

        var instances = objects.Instances;
        var beforeData = (byte[])data.Data.Clone();
        var localsBefore = data.Locals().ToList();
        var globalsBefore = data.Globals().ToList();
        var headers = new List<int>();
        var retired = new List<(int Start, int Length)>();
        var appended = new List<(int Start, int Length)>();

        foreach (var change in edits)
        {
            if (!Structural(change)) continue;
            RequireStorage(image, objects, change.Edited, change.Original.Rows.Count,
                           change.Original.ChainMappings.Count);

            int header = DataAt(objects, change.Edited) + Offsets.Data("simpleMappings");
            int stride = Offsets.Stride(SimpleRowClass);
            int had = change.Original.Rows.Count;
            var run = had > 0 ? objects.ArrayAt(header, stride) : null;
            if (had > 0 && run == null)
                throw new InvalidOperationException(
                    $"mapper #{change.Edited.ObjectId} has {had} simple row(s) whose storage cannot be located, " +
                    "so its rows were not resized");

            var placed = Relocate(image, data, header, run?.At ?? 0, had, change.Edited.Rows.Count, stride, section);
            if (placed.Length > 0) appended.Add(placed);
            if (had > 0) retired.Add((run!.At, had * stride));
            headers.Add(header);
        }

        objects = new PackfileObjects(image);
        data = image.Section(PackfileObjects.DataSection)
            ?? throw new InvalidOperationException("this file has no data section, so nothing was written");

        var owned = new List<(int Start, int Length)>();
        foreach (var change in edits)
        {
            RequireStorage(image, objects, change.Edited, change.Edited.Rows.Count,
                           change.Edited.ChainMappings.Count);
            WriteValues(data.Data, objects, change.Edited, change.Original, owned);
        }

        var allowed = new List<(int Start, int Length)>(owned);
        allowed.AddRange(retired);
        foreach (int header in headers)
        {
            allowed.Add((header, image.Layout.PointerSize));
            allowed.Add((header + 8, 8));
        }

        RequireStructuralConfined(beforeData, data.Data, allowed, appended);
        RequireOtherInstancesIntact(beforeData, data.Data, instances,
                                    edits.Select(change => change.Edited.Instance.Offset).ToHashSet());
        RequireFixupsConfined(localsBefore, globalsBefore, data.Locals().ToList(), data.Globals().ToList(),
                              headers);

        FixupOrder.Reorder(image);
        var rebuilt = image.Rebuild();
        Verify(rebuilt, nodes);
        return rebuilt;
    }

    private static List<SkeletonMapperChange> NoOpEdits(IReadOnlyList<SkeletonMapperNode> nodes)
    {
        var edits = new List<SkeletonMapperChange>(nodes.Count);
        foreach (var node in nodes) edits.Add(new SkeletonMapperChange(node, node.Copy()));
        return edits;
    }

    private static void RequireComplete(SkeletonMapperChange change)
    {
        if (change.Edited.OptionalMembersRead) return;

        throw new InvalidOperationException(
            $"mapper #{change.Edited.ObjectId} carries chain mappings, unmapped bones, keepUnmappedLocal or " +
            "an extracted motion mapping this build cannot read, so nothing was written");
    }

    private static bool Structural(SkeletonMapperChange change) =>
        change.Edited.Rows.Count != change.Original.Rows.Count ||
        change.Edited.ChainMappings.Count != change.Original.ChainMappings.Count;

    private static int DataAt(PackfileObjects objects, SkeletonMapperNode node) =>
        objects.FieldAt(node.Instance, "mapping")
        ?? throw new InvalidOperationException(
            $"mapper #{node.ObjectId} has no mapping member this build can place");

    private static void RequireStorage(PackfileImage image, PackfileObjects objects, SkeletonMapperNode node,
                                       int rows, int chains)
    {
        int dataAt = DataAt(objects, node);
        RequireArray(image, objects, dataAt, node.ObjectId, "simpleMappings", SimpleRowClass, rows);
        RequireArray(image, objects, dataAt, node.ObjectId, "chainMappings", ChainRowClass, chains);
    }

    private static void RequireArray(PackfileImage image, PackfileObjects objects, int dataAt, int objectId,
                                     string member, string elementClass, int count)
    {
        if (count <= 0) return;

        int header = dataAt + Offsets.Data(member);
        int stride = Offsets.Stride(elementClass);
        var run = objects.ArrayAt(header, stride)
            ?? throw new InvalidOperationException(
                $"mapper #{objectId} has {count} {member} row(s) whose storage cannot be located, so nothing was written");

        var data = image.Section(PackfileObjects.DataSection)
            ?? throw new InvalidOperationException("this file has no data section");
        int section = image.Sections.IndexOf(data);

        long end = (long)run.At + (long)count * stride;
        bool Inside(int at) => at >= run.At && at < end;

        bool shared = data.Locals().Any(entry => entry.Source != header && Inside(entry.Destination)) ||
                      data.Globals().Any(entry =>
                          entry.Source != header && entry.Section == section && Inside(entry.Destination)) ||
                      image.Sections.Where(owner => !ReferenceEquals(owner, data)).Any(owner =>
                          owner.Globals().Any(entry => entry.Section == section && Inside(entry.Destination))) ||
                      data.Virtuals().Any(entry => Inside(entry.Source));

        if (shared)
            throw new InvalidOperationException(
                $"mapper #{objectId}.{member} shares its row storage with another object, so the edit was refused");
    }

    private static void WriteValues(byte[] data, PackfileObjects objects, SkeletonMapperNode edited,
                                    SkeletonMapperNode original, List<(int Start, int Length)>? ranges)
    {
        int dataAt = DataAt(objects, edited);
        WriteRows(data, objects, dataAt, edited, original, ranges);
        WriteChains(data, objects, dataAt, edited, original, ranges);

        if (edited.MappingType != original.MappingType)
        {
            int at = dataAt + Offsets.Data("mappingType");
            WriteInt(data, at, edited.MappingType);
            ranges?.Add((at, 4));
        }

        if (edited.KeepUnmappedLocal != original.KeepUnmappedLocal)
        {
            int at = dataAt + Offsets.Data("keepUnmappedLocal");
            data[at] = edited.KeepUnmappedLocal ? (byte)1 : (byte)0;
            ranges?.Add((at, 1));
        }
    }

    private static void WriteRows(byte[] data, PackfileObjects objects, int dataAt,
                                  SkeletonMapperNode edited, SkeletonMapperNode original,
                                  List<(int Start, int Length)>? ranges)
    {
        int stride = Offsets.Stride(SimpleRowClass);
        int boneAAt = Offsets.Member(SimpleRowClass, "boneA");
        int boneBAt = Offsets.Member(SimpleRowClass, "boneB");
        int transformAt = Offsets.Member(SimpleRowClass, "aFromBTransform");

        int header = dataAt + Offsets.Data("simpleMappings");
        var run = edited.Rows.Count > 0 ? objects.ArrayAt(header, stride) : null;
        if (edited.Rows.Count > 0 && run == null)
            throw new InvalidOperationException(
                $"mapper #{edited.ObjectId} has {edited.Rows.Count} simple row(s) whose storage cannot be located, so nothing was written");

        for (int i = 0; i < edited.Rows.Count; i++)
        {
            if (!NeedsWrite(edited.Rows, original.Rows, i)) continue;

            int rowAt = run!.At + i * stride;
            HavokSkeletonSimpleMappingRow row = edited.Rows[i];
            WriteNarrow(data, rowAt + boneAAt, 2, row.BoneA);
            WriteNarrow(data, rowAt + boneBAt, 2, row.BoneB);
            WriteTransform(data, rowAt + transformAt, row.AFromB);
            ranges?.Add((rowAt + boneAAt, 2));
            ranges?.Add((rowAt + boneBAt, 2));
            ranges?.Add((rowAt + transformAt, HavokSkeletonMappingTransform.LaneCount * sizeof(float)));
        }
    }

    private static void WriteChains(byte[] data, PackfileObjects objects, int dataAt,
                                    SkeletonMapperNode edited, SkeletonMapperNode original,
                                    List<(int Start, int Length)>? ranges)
    {
        RequireResized(edited.ChainMappings.Count, original.ChainMappings.Count, edited.ObjectId, "chain");
        if (edited.ChainMappings.Count == 0) return;

        int stride = Offsets.Stride(ChainRowClass);
        int startBoneAAt = Offsets.Member(ChainRowClass, "startBoneA");
        int endBoneAAt = Offsets.Member(ChainRowClass, "endBoneA");
        int startBoneBAt = Offsets.Member(ChainRowClass, "startBoneB");
        int endBoneBAt = Offsets.Member(ChainRowClass, "endBoneB");
        int startAt = Offsets.Member(ChainRowClass, "startAFromBTransform");
        int endAt = Offsets.Member(ChainRowClass, "endAFromBTransform");

        int header = dataAt + Offsets.Data("chainMappings");
        var run = objects.ArrayAt(header, stride);
        if (run == null)
            throw new InvalidOperationException(
                $"mapper #{edited.ObjectId} has {edited.ChainMappings.Count} chain row(s) whose storage cannot be located, so nothing was written");

        for (int i = 0; i < edited.ChainMappings.Count; i++)
        {
            if (!NeedsWrite(edited.ChainMappings, original.ChainMappings, i)) continue;

            int rowAt = run.At + i * stride;
            HavokSkeletonChainMappingRow row = edited.ChainMappings[i];
            WriteNarrow(data, rowAt + startBoneAAt, 2, row.StartBoneA);
            WriteNarrow(data, rowAt + endBoneAAt, 2, row.EndBoneA);
            WriteNarrow(data, rowAt + startBoneBAt, 2, row.StartBoneB);
            WriteNarrow(data, rowAt + endBoneBAt, 2, row.EndBoneB);
            WriteTransform(data, rowAt + startAt, row.StartAFromB);
            WriteTransform(data, rowAt + endAt, row.EndAFromB);
            ranges?.Add((rowAt + startBoneAAt, 2));
            ranges?.Add((rowAt + endBoneAAt, 2));
            ranges?.Add((rowAt + startBoneBAt, 2));
            ranges?.Add((rowAt + endBoneBAt, 2));
            ranges?.Add((rowAt + startAt, HavokSkeletonMappingTransform.LaneCount * sizeof(float)));
            ranges?.Add((rowAt + endAt, HavokSkeletonMappingTransform.LaneCount * sizeof(float)));
        }
    }

    private static bool NeedsWrite<T>(IReadOnlyList<T> edited, IReadOnlyList<T> original, int index) =>
        index >= original.Count || !EqualityComparer<T>.Default.Equals(edited[index], original[index]);

    private static void RequireResized(int edited, int original, int objectId, string what)
    {
        if (edited != original)
            throw new InvalidOperationException(
                $"mapper #{objectId} would have {edited} {what} row(s) where it holds {original}; " +
                "only simple rows are resized, through the native relocate path");
    }

    private static (int From, int Length) Relocate(PackfileImage image, PackfileSection data, int headerAt,
                                                   int hadAt, int had, int count, int stride, int section)
    {
        bool local = data.Locals().Any(entry => entry.Source == headerAt);
        bool global = data.Globals().Any(entry => entry.Source == headerAt);

        if (local && global)
            throw new InvalidOperationException(
                "this mapper's array pointer carries both a local and a global fixup, so its storage cannot be moved");
        if (!local && !global)
            throw new InvalidOperationException(
                "this mapper's array pointer is not a fixup, so its storage cannot be moved");

        if (count == 0)
        {
            if (local) data.SetLocal(headerAt, -1);
            if (global) data.SetGlobal(headerAt, section, -1);
            Array.Clear(data.Data, headerAt, 8);
            WriteArrayCount(data, headerAt, 0);
            if (had > 0) Array.Clear(data.Data, hadAt, had * stride);
            return (0, 0);
        }

        int carried = Math.Min(had, count);
        int from = data.Data.Length;
        data.AlignData(NativeAppend.Alignment);
        int run = data.AppendData(new byte[count * stride]);
        if (carried > 0) Array.Copy(data.Data, hadAt, data.Data, run, carried * stride);

        if (local) data.SetLocal(headerAt, run);
        else data.SetGlobal(headerAt, section, run);

        Move(data.Locals().ToList(), data.SetLocals, entry => entry.Source,
             (entry, at) => (at, entry.Destination));
        Move(data.Globals().ToList(), data.SetGlobals, entry => entry.Source,
             (entry, at) => (at, entry.Section, entry.Destination));

        WriteArrayCount(data, headerAt, count);
        if (had > 0) Array.Clear(data.Data, hadAt, had * stride);

        return (from, data.Data.Length - from);

        void Move<T>(List<T> entries, Action<IEnumerable<T>> write, Func<T, int> sourceOf,
                     Func<T, int, T> moved)
        {
            if (had == 0) return;

            var kept = new List<T>();
            foreach (T entry in entries)
            {
                int source = sourceOf(entry);
                if (source < hadAt || source >= hadAt + had * stride) { kept.Add(entry); continue; }
                if (source >= hadAt + carried * stride) continue;

                kept.Add(moved(entry, source - hadAt + run));
            }

            write(kept);
        }
    }

    private static void WriteArrayCount(PackfileSection data, int at, int count)
    {
        BitConverter.GetBytes(count).CopyTo(data.Data, at + 8);
        uint capacity = BitConverter.ToUInt32(data.Data, at + 12);
        uint flags = capacity & 0xC0000000u;
        if (count > 0) flags |= EmbeddedArrayStorage;
        BitConverter.GetBytes(flags | (uint)count).CopyTo(data.Data, at + 12);
    }

    private static void WriteNarrow(byte[] data, int at, int width, int value)
    {
        if (at < 0 || at + width > data.Length)
            throw new InvalidOperationException("a row field lies outside the data section, so nothing was written");

        for (int i = 0; i < width; i++) data[at + i] = (byte)(value >> (8 * i));
    }

    private static void WriteInt(byte[] data, int at, int value)
    {
        if (at < 0 || at + 4 > data.Length)
            throw new InvalidOperationException("a mapper field lies outside the data section, so nothing was written");
        BitConverter.GetBytes(value).CopyTo(data, at);
    }

    private static void WriteTransform(byte[] data, int at, HavokSkeletonMappingTransform transform)
    {
        if (at < 0 || at + HavokSkeletonMappingTransform.LaneCount * sizeof(float) > data.Length)
            throw new InvalidOperationException("a row transform lies outside the data section, so nothing was written");

        for (int lane = 0; lane < HavokSkeletonMappingTransform.LaneCount; lane++)
            BitConverter.GetBytes(transform[lane]).CopyTo(data, at + lane * sizeof(float));
    }

    private static void RequireConfined(byte[] produced, byte[] before,
                                        IReadOnlyList<(int Start, int Length)> ranges)
    {
        if (produced.Length != before.Length)
            throw new InvalidDataException(
                $"a value edit produced {produced.Length} bytes where the section holds {before.Length}");

        for (int at = 0; at < produced.Length; at++)
        {
            if (produced[at] == before[at]) continue;
            if (ranges.Any(range => at >= range.Start && at < range.Start + range.Length)) continue;

            throw new InvalidDataException(
                $"a value edit reached byte 0x{at:x}, which is outside the field it was meant to write, " +
                "so nothing was written");
        }
    }

    private static void RequireStructuralConfined(byte[] before, byte[] after,
                                                  IReadOnlyList<(int Start, int Length)> allowed,
                                                  IReadOnlyList<(int Start, int Length)> appended)
    {
        if (after.Length < before.Length)
            throw new InvalidDataException(
                $"a structural row edit shrank the data section to {after.Length} bytes from {before.Length}");

        for (int at = 0; at < before.Length; at++)
        {
            if (after[at] == before[at]) continue;
            if (allowed.Any(range => at >= range.Start && at < range.Start + range.Length)) continue;

            throw new InvalidDataException(
                $"a structural row edit reached byte 0x{at:x}, which is outside the row run, the array " +
                "header and the fields it owns, so nothing was written");
        }

        for (int at = before.Length; at < after.Length; at++)
            if (!appended.Any(range => at >= range.Start && at < range.Start + range.Length))
                throw new InvalidDataException(
                    $"a structural row edit wrote byte 0x{at:x} outside the run it appended, so nothing was written");
    }

    private static void RequireOtherInstancesIntact(byte[] before, byte[] after,
                                                    IReadOnlyList<PackfileObjects.Instance> instances,
                                                    IReadOnlySet<int> edited)
    {
        foreach (var instance in instances)
        {
            if (edited.Contains(instance.Offset)) continue;

            int? size = HavokClassTypes.Shipped[instance.ClassName]?.Size;
            if (size is not int span || span <= 0) continue;
            if (instance.Offset < 0 || instance.Offset + span > before.Length) continue;

            for (int i = 0; i < span; i++)
            {
                int at = instance.Offset + i;
                if (before[at] == after[at]) continue;

                throw new InvalidDataException(
                    $"a structural row edit reached byte 0x{at:x} inside {instance.ClassName} at " +
                    $"0x{instance.Offset:x}, which is another object instance, so nothing was written");
            }
        }
    }

    private static void RequireFixupsConfined(
        IReadOnlyList<(int Source, int Destination)> localsBefore,
        IReadOnlyList<(int Source, int Section, int Destination)> globalsBefore,
        IReadOnlyList<(int Source, int Destination)> localsAfter,
        IReadOnlyList<(int Source, int Section, int Destination)> globalsAfter,
        IReadOnlyList<int> headers)
    {
        RequireSameFixups(localsBefore.Where(entry => !headers.Contains(entry.Source)),
                          localsAfter.Where(entry => !headers.Contains(entry.Source)), "local");
        RequireSameFixups(globalsBefore.Where(entry => !headers.Contains(entry.Source)),
                          globalsAfter.Where(entry => !headers.Contains(entry.Source)), "global");
    }

    private static void RequireSameFixups<T>(IEnumerable<T> before, IEnumerable<T> after, string which)
    {
        var remaining = after.ToList();
        foreach (T entry in before)
        {
            if (remaining.Remove(entry)) continue;

            throw new InvalidDataException(
                $"a structural row edit rewrote the {which} fixup {entry}, which it does not own, " +
                "so nothing was written");
        }

        if (remaining.Count > 0)
            throw new InvalidDataException(
                $"a structural row edit added the {which} fixup {remaining[0]}, which it does not own, " +
                "so nothing was written");
    }

    private static void Verify(byte[] produced, IReadOnlyList<SkeletonMapperNode> nodes)
    {
        PackfileObjects objects;
        try
        {
            objects = new PackfileObjects(PackfileImage.Read(produced));
        }
        catch (Exception error)
        {
            throw new InvalidDataException("the written file could not be read back: " + error.Message, error);
        }

        var mismatched = HavokClassTypes.Shipped.SignatureProblems(objects.ClassNames());
        if (mismatched.Count > 0)
            throw new InvalidDataException("the written file's class signatures do not match this build: " + mismatched[0]);

        var read = new List<HavokSkeletonMapper>();
        foreach (var instance in objects.Instances)
            if (instance.ClassName == MapperClass &&
                SkeletonMapperReader.Read(objects, instance) is { } mapper)
                read.Add(mapper);

        if (read.Count != nodes.Count)
            throw new InvalidDataException(
                $"the written file holds {read.Count} readable mapper(s) where {nodes.Count} were expected, " +
                "so nothing was written");

        for (int i = 0; i < nodes.Count; i++)
            if (!Matches(nodes[i], read[i]))
                throw new InvalidDataException(
                    $"mapper #{read[i].ObjectId} did not read back as the model that was edited, so nothing was written");
    }

    private static bool Matches(SkeletonMapperNode node, HavokSkeletonMapper read) =>
        node.ObjectId == read.ObjectId &&
        node.SkeletonAObjectId == read.SkeletonAObjectId &&
        node.SkeletonBObjectId == read.SkeletonBObjectId &&
        node.MappingType == read.MappingType &&
        node.KeepUnmappedLocal == read.KeepUnmappedLocal &&
        node.Rows.SequenceEqual(read.Rows) &&
        node.ChainMappings.SequenceEqual(read.ChainMappings) &&
        node.UnmappedBones.SequenceEqual(read.UnmappedBones) &&
        node.ExtractedMotionMapping.Equals(read.ExtractedMotionMapping);

    private static class Offsets
    {
        internal static int Data(string member) => Member(DataClass, member);

        internal static int Member(string className, string member)
        {
            var field = HavokClasses.Shipped.Field(className, member)
                ?? throw new InvalidDataException($"the class table has no {className}.{member}");
            return field.Offset;
        }

        internal static int Stride(string className)
        {
            int? size = HavokClassTypes.Shipped[className]?.Size;
            if (size is not int value || value <= 0)
                throw new InvalidDataException($"the class table has no size for {className}");
            return value;
        }
    }
}
