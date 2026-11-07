using System;
using System.Collections.Generic;
using System.Linq;
using System.Numerics;

namespace OpenCommonwealth.Services.Hkx;

public sealed class HavokSkeletonMappingTransform : IEquatable<HavokSkeletonMappingTransform>
{
    public const int LaneCount = 12;
    public const int TranslationLane = 0;
    public const int RotationLane = 4;
    public const int ScaleLane = 8;

    public static readonly HavokSkeletonMappingTransform Identity =
        From(Vector3.Zero, Quaternion.Identity, Vector3.One);

    private readonly float[] _lanes;

    public HavokSkeletonMappingTransform(float[] lanes)
    {
        ArgumentNullException.ThrowIfNull(lanes);
        if (lanes.Length != LaneCount)
            throw new ArgumentException($"a mapping transform holds {LaneCount} lanes, not {lanes.Length}",
                                        nameof(lanes));

        _lanes = (float[])lanes.Clone();
    }

    public static HavokSkeletonMappingTransform From(Vector3 translation, Quaternion rotation, Vector3 scale)
    {
        var lanes = new float[LaneCount];
        lanes[0] = translation.X;
        lanes[1] = translation.Y;
        lanes[2] = translation.Z;
        lanes[4] = rotation.X;
        lanes[5] = rotation.Y;
        lanes[6] = rotation.Z;
        lanes[7] = rotation.W;
        lanes[8] = scale.X;
        lanes[9] = scale.Y;
        lanes[10] = scale.Z;
        return new HavokSkeletonMappingTransform(lanes);
    }

    public float this[int lane]
    {
        get
        {
            if (lane < 0 || lane >= LaneCount) throw new ArgumentOutOfRangeException(nameof(lane));
            return _lanes[lane];
        }
    }

    public float[] Lanes => (float[])_lanes.Clone();

    public Vector3 Translation => new(_lanes[0], _lanes[1], _lanes[2]);

    public Quaternion Rotation => new(_lanes[4], _lanes[5], _lanes[6], _lanes[7]);

    public Vector3 Scale => new(_lanes[8], _lanes[9], _lanes[10]);

    public bool IsFinite =>
        _lanes.All(float.IsFinite);

    public bool HasUnitRotation(float tolerance) =>
        !HasRotation || MathF.Abs(Rotation.Length() - 1f) <= tolerance;

    private bool HasRotation => Rotation.LengthSquared() > 1e-12f;

    public HavokSkeletonMappingTransform WithTranslation(Vector3 translation) => Replaced(
        new[] { translation.X, translation.Y, translation.Z }, TranslationLane);

    public HavokSkeletonMappingTransform WithRotation(Quaternion rotation) => Replaced(
        new[] { rotation.X, rotation.Y, rotation.Z, rotation.W }, RotationLane);

    public HavokSkeletonMappingTransform WithScale(Vector3 scale) => Replaced(
        new[] { scale.X, scale.Y, scale.Z }, ScaleLane);

    private HavokSkeletonMappingTransform Replaced(IReadOnlyList<float> values, int first)
    {
        var lanes = (float[])_lanes.Clone();
        for (int i = 0; i < values.Count; i++) lanes[first + i] = values[i];
        return new HavokSkeletonMappingTransform(lanes);
    }

    public bool Equals(HavokSkeletonMappingTransform? other)
    {
        if (other is null) return false;
        for (int i = 0; i < LaneCount; i++)
            if (!_lanes[i].Equals(other._lanes[i])) return false;
        return true;
    }

    public override bool Equals(object? obj) => Equals(obj as HavokSkeletonMappingTransform);

    public override int GetHashCode()
    {
        var hash = new HashCode();
        foreach (float lane in _lanes) hash.Add(lane);
        return hash.ToHashCode();
    }

    public override string ToString() =>
        $"({Translation.X:0.######} {Translation.Y:0.######} {Translation.Z:0.######}) " +
        $"({Rotation.X:0.######} {Rotation.Y:0.######} {Rotation.Z:0.######} {Rotation.W:0.######}) " +
        $"({Scale.X:0.######} {Scale.Y:0.######} {Scale.Z:0.######})";
}

public sealed record HavokSkeletonSimpleMappingRow(
    int BoneA,
    int BoneB,
    HavokSkeletonMappingTransform AFromB);

public sealed record HavokSkeletonChainMappingRow(
    int StartBoneA,
    int EndBoneA,
    int StartBoneB,
    int EndBoneB,
    HavokSkeletonMappingTransform StartAFromB,
    HavokSkeletonMappingTransform EndAFromB);

public sealed record HavokSkeletonMapper(
    int ObjectId,
    int SkeletonAObjectId,
    int SkeletonBObjectId,
    HavokRagdollSkeleton SkeletonA,
    HavokRagdollSkeleton SkeletonB,
    int MappingType,
    IReadOnlyList<HavokRagdollMapping> SimpleMappings,
    IReadOnlyList<HavokSkeletonSimpleMappingRow> Rows,
    IReadOnlyList<HavokSkeletonChainMappingRow> ChainMappings,
    IReadOnlyList<int> UnmappedBones,
    bool KeepUnmappedLocal,
    HavokSkeletonMappingTransform ExtractedMotionMapping,
    bool OptionalMembersRead);

public static class SkeletonMapperReader
{
    private const string MapperClass = "hkaSkeletonMapper";
    private const string MapperDataClass = "hkaSkeletonMapperData";
    private const string SimpleMappingClass = "hkaSkeletonMapperDataSimpleMapping";
    private const string ChainMappingClass = "hkaSkeletonMapperDataChainMapping";
    private const string SkeletonClass = "hkaSkeleton";

    public static IReadOnlyList<HavokSkeletonMapper> Read(PackfileObjects objects)
    {
        ArgumentNullException.ThrowIfNull(objects);

        var result = new List<HavokSkeletonMapper>();
        foreach (var instance in objects.Instances.Where(item => item.ClassName == MapperClass))
            if (ReadMapper(objects, instance) is { } mapper)
                result.Add(mapper);
        return result;
    }

    public static HavokSkeletonMapper? Read(PackfileObjects objects, PackfileObjects.Instance instance)
    {
        ArgumentNullException.ThrowIfNull(objects);
        ArgumentNullException.ThrowIfNull(instance);
        return instance.ClassName == MapperClass ? ReadMapper(objects, instance) : null;
    }

    internal static HavokRagdollSkeleton? ReadSkeleton(
        PackfileObjects objects,
        PackfileObjects.Instance instance)
    {
        if (instance.ClassName != SkeletonClass)
            return null;

        var reader = new MemberReader(objects);
        string name = "";
        if (reader.At(instance.Offset, SkeletonClass, "name") is { } nameAt)
            name = objects.ReadStringAt(nameAt) ?? "";

        var skeleton = new HavokRagdollSkeleton { Name = name };

        if (reader.At(instance.Offset, SkeletonClass, "parentIndices") is { } parentsAt &&
            objects.ArrayAt(parentsAt, 2) is { } parents)
            for (int i = 0; i < parents.Count; i++)
            {
                int? raw = objects.ReadNarrowAt(parents.At + i * 2, 2);
                if (raw == null) return null;
                skeleton.ParentIndices.Add(unchecked((short)raw.Value));
            }

        if (reader.At(instance.Offset, SkeletonClass, "bones") is { } bonesAt &&
            objects.ArrayAt(bonesAt, 16) is { } bones)
            for (int i = 0; i < bones.Count; i++)
            {
                string? boneName = objects.ReadStringAt(bones.At + i * 16);
                if (boneName == null) return null;
                skeleton.BoneNames.Add(boneName);
            }

        if (skeleton.BoneNames.Count == 0)
            return null;

        if (reader.At(instance.Offset, SkeletonClass, "referencePose") is { } poseAt &&
            objects.ArrayAt(poseAt, 48) is { } poses)
            for (int i = 0; i < poses.Count; i++)
            {
                float[]? row = objects.ReadFloatsAt(poses.At + i * 48, 12);
                if (row == null) return null;
                skeleton.ReferencePose.Add(new HkxBonePose(
                    new Vector3(row[0], row[1], row[2]),
                    new Quaternion(row[4], row[5], row[6], row[7]),
                    new Vector3(row[8], row[9], row[10])));
            }

        return skeleton;
    }

    private static HavokSkeletonMapper? ReadMapper(
        PackfileObjects objects,
        PackfileObjects.Instance instance)
    {
        var mapperLayout = LayoutWalker.Active(HavokClassTypes.Shipped, MapperClass, objects.PointerWidth);
        int? mappingAt = mapperLayout?.OffsetOf("mapping") is { } mappingOffset
            ? instance.Offset + mappingOffset
            : null;
        if (mappingAt == null)
            return null;

        var dataLayout = LayoutWalker.Active(HavokClassTypes.Shipped, MapperDataClass, objects.PointerWidth);
        if (dataLayout == null ||
            dataLayout.OffsetOf("skeletonA") is not { } skeletonAOffset ||
            dataLayout.OffsetOf("skeletonB") is not { } skeletonBOffset ||
            dataLayout.OffsetOf("simpleMappings") is not { } rowsOffset ||
            dataLayout.OffsetOf("mappingType") is not { } typeOffset)
            return null;

        var skeletonAInstance = objects.ReadRefAt(mappingAt.Value + skeletonAOffset, out _);
        var skeletonBInstance = objects.ReadRefAt(mappingAt.Value + skeletonBOffset, out _);
        if (skeletonAInstance?.ClassName != SkeletonClass || skeletonBInstance?.ClassName != SkeletonClass)
            return null;

        var skeletonA = ReadSkeleton(objects, skeletonAInstance);
        var skeletonB = ReadSkeleton(objects, skeletonBInstance);
        int? mappingType = objects.ReadNarrowAt(mappingAt.Value + typeOffset, 4);
        if (skeletonA == null || skeletonB == null || mappingType == null)
            return null;

        bool complete = ReadOptionalMembers(objects, dataLayout, mappingAt.Value,
                                            out bool keepUnmappedLocal,
                                            out HavokSkeletonMappingTransform extractedMotion);

        var simpleLayout = LayoutWalker.Active(HavokClassTypes.Shipped, SimpleMappingClass, objects.PointerWidth);
        if (simpleLayout == null || simpleLayout.Size <= 0 ||
            simpleLayout.OffsetOf("boneA") is not { } boneAOffset ||
            simpleLayout.OffsetOf("boneB") is not { } boneBOffset ||
            simpleLayout.OffsetOf("aFromBTransform") is not { } transformOffset)
            return null;

        var rows = objects.ArrayAt(mappingAt.Value + rowsOffset, simpleLayout.Size);
        if (rows == null)
            return null;

        var mappings = new List<HavokRagdollMapping>(rows.Count);
        var rawRows = new List<HavokSkeletonSimpleMappingRow>(rows.Count);
        for (int i = 0; i < rows.Count; i++)
        {
            int row = rows.At + i * simpleLayout.Size;
            int? boneA = objects.ReadNarrowAt(row + boneAOffset, 2);
            int? boneB = objects.ReadNarrowAt(row + boneBOffset, 2);
            float[]? values = objects.ReadFloatsAt(row + transformOffset, HavokSkeletonMappingTransform.LaneCount);
            if (boneA == null || boneB == null || values == null)
                return null;

            var transform = new HavokSkeletonMappingTransform(values);
            var translation = new Vector3(values[0], values[1], values[2]);
            var rotation = new Quaternion(values[4], values[5], values[6], values[7]);
            if (!Finite(translation) || !Finite(rotation) || rotation.LengthSquared() < 1e-10f)
                return null;

            rawRows.Add(new HavokSkeletonSimpleMappingRow(
                unchecked((short)boneA.Value), unchecked((short)boneB.Value), transform));
            mappings.Add(new HavokRagdollMapping(
                unchecked((short)boneA.Value),
                unchecked((short)boneB.Value),
                translation,
                Quaternion.Normalize(rotation)));
        }

        var rawChains = new List<HavokSkeletonChainMappingRow>();
        if (!ReadChains(objects, mappingAt.Value, dataLayout.OffsetOf("chainMappings"), rawChains))
        {
            rawChains.Clear();
            complete = false;
        }

        var unmapped = new List<int>();
        if (!ReadUnmappedBones(objects, mappingAt.Value, dataLayout.OffsetOf("unmappedBones"), unmapped))
        {
            unmapped.Clear();
            complete = false;
        }

        return new HavokSkeletonMapper(
            NativeGraphModel.FirstId + objects.IndexOf(instance),
            NativeGraphModel.FirstId + objects.IndexOf(skeletonAInstance),
            NativeGraphModel.FirstId + objects.IndexOf(skeletonBInstance),
            skeletonA,
            skeletonB,
            mappingType.Value,
            mappings,
            rawRows,
            rawChains,
            unmapped,
            keepUnmappedLocal,
            extractedMotion,
            complete);
    }

    private static bool ReadOptionalMembers(
        PackfileObjects objects,
        ObjectLayout dataLayout,
        int mappingAt,
        out bool keepUnmappedLocal,
        out HavokSkeletonMappingTransform extractedMotion)
    {
        keepUnmappedLocal = false;
        extractedMotion = HavokSkeletonMappingTransform.Identity;
        bool complete = true;

        if (dataLayout.OffsetOf("keepUnmappedLocal") is { } keepOffset &&
            objects.ReadNarrowAt(mappingAt + keepOffset, 1) is { } keepUnmapped)
            keepUnmappedLocal = keepUnmapped != 0;
        else
            complete = false;

        if (dataLayout.OffsetOf("extractedMotionMapping") is { } extractedOffset &&
            objects.ReadFloatsAt(mappingAt + extractedOffset, HavokSkeletonMappingTransform.LaneCount)
                is { } extracted)
            extractedMotion = new HavokSkeletonMappingTransform(extracted);
        else
            complete = false;

        return complete;
    }

    private static bool ReadChains(
        PackfileObjects objects,
        int mappingAt,
        int? chainsOffset,
        List<HavokSkeletonChainMappingRow> into)
    {
        var chainLayout = LayoutWalker.Active(HavokClassTypes.Shipped, ChainMappingClass, objects.PointerWidth);
        if (chainsOffset == null ||
            chainLayout == null || chainLayout.Size <= 0 ||
            chainLayout.OffsetOf("startBoneA") is not { } startBoneAOffset ||
            chainLayout.OffsetOf("endBoneA") is not { } endBoneAOffset ||
            chainLayout.OffsetOf("startBoneB") is not { } startBoneBOffset ||
            chainLayout.OffsetOf("endBoneB") is not { } endBoneBOffset ||
            chainLayout.OffsetOf("startAFromBTransform") is not { } startTransformOffset ||
            chainLayout.OffsetOf("endAFromBTransform") is not { } endTransformOffset)
            return false;

        if (objects.ArrayAt(mappingAt + chainsOffset.Value, chainLayout.Size) is not { } chains)
            return false;

        for (int i = 0; i < chains.Count; i++)
        {
            int chain = chains.At + i * chainLayout.Size;
            int? startBoneA = objects.ReadNarrowAt(chain + startBoneAOffset, 2);
            int? endBoneA = objects.ReadNarrowAt(chain + endBoneAOffset, 2);
            int? startBoneB = objects.ReadNarrowAt(chain + startBoneBOffset, 2);
            int? endBoneB = objects.ReadNarrowAt(chain + endBoneBOffset, 2);
            float[]? startValues = objects.ReadFloatsAt(chain + startTransformOffset,
                                                        HavokSkeletonMappingTransform.LaneCount);
            float[]? endValues = objects.ReadFloatsAt(chain + endTransformOffset,
                                                      HavokSkeletonMappingTransform.LaneCount);
            if (startBoneA == null || endBoneA == null || startBoneB == null || endBoneB == null ||
                startValues == null || endValues == null)
                return false;

            into.Add(new HavokSkeletonChainMappingRow(
                unchecked((short)startBoneA.Value),
                unchecked((short)endBoneA.Value),
                unchecked((short)startBoneB.Value),
                unchecked((short)endBoneB.Value),
                new HavokSkeletonMappingTransform(startValues),
                new HavokSkeletonMappingTransform(endValues)));
        }

        return true;
    }

    private static bool ReadUnmappedBones(
        PackfileObjects objects,
        int mappingAt,
        int? unmappedOffset,
        List<int> into)
    {
        if (unmappedOffset == null)
            return false;

        if (objects.ArrayAt(mappingAt + unmappedOffset.Value, 2) is not { } array)
            return false;

        for (int i = 0; i < array.Count; i++)
        {
            int? bone = objects.ReadNarrowAt(array.At + i * 2, 2);
            if (bone == null) return false;
            into.Add(unchecked((short)bone.Value));
        }

        return true;
    }

    private static bool Finite(Vector3 value) =>
        float.IsFinite(value.X) && float.IsFinite(value.Y) && float.IsFinite(value.Z);

    private static bool Finite(Quaternion value) =>
        float.IsFinite(value.X) && float.IsFinite(value.Y) &&
        float.IsFinite(value.Z) && float.IsFinite(value.W);

    private sealed class MemberReader
    {
        public MemberReader(PackfileObjects objects) => Objects = objects;

        private PackfileObjects Objects { get; }

        public int? At(int baseAt, string className, string member) =>
            LayoutWalker.Active(HavokClassTypes.Shipped, className, Objects.PointerWidth)?.OffsetOf(member) is { } offset
                ? baseAt + offset
                : null;
    }
}
