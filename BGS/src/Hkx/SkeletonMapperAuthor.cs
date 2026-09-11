using System;
using System.Collections.Generic;
using System.Numerics;

namespace OpenCommonwealth.Services.Hkx;

public sealed record HavokSkeletonMappingPair(
    IReadOnlyList<HavokRagdollMapping> Forward,
    IReadOnlyList<HavokRagdollMapping> Reverse);

public static class SkeletonMapperAuthor
{
    public const int RagdollMappingType = 0;

    public static HavokSkeletonMappingPair Reciprocal(
        HavokRagdollSkeleton skeletonA,
        HavokRagdollSkeleton skeletonB,
        IReadOnlyList<(int BoneA, int BoneB)> pairs)
    {
        var forward = FromReferencePose(skeletonA, skeletonB, pairs);
        return new HavokSkeletonMappingPair(forward, Reverse(forward));
    }

    public static IReadOnlyList<HavokRagdollMapping> FromReferencePose(
        HavokRagdollSkeleton skeletonA,
        HavokRagdollSkeleton skeletonB,
        IReadOnlyList<(int BoneA, int BoneB)> pairs)
    {
        ArgumentNullException.ThrowIfNull(skeletonA);
        ArgumentNullException.ThrowIfNull(skeletonB);
        ArgumentNullException.ThrowIfNull(pairs);

        var refusals = Refusals(skeletonA, skeletonB, pairs);
        if (refusals.Count > 0) throw new ArgumentException(string.Join("; ", refusals), nameof(pairs));

        var framesA = skeletonA.WorldReferenceFrames();
        var framesB = skeletonB.WorldReferenceFrames();

        var rows = new List<HavokRagdollMapping>(pairs.Count);
        foreach (var (boneA, boneB) in pairs)
        {
            var composed = Compose(Invert(framesA[boneA]), framesB[boneB]);
            rows.Add(new HavokRagdollMapping(boneA, boneB, composed.Position, composed.Rotation));
        }
        return rows;
    }

    public static IReadOnlyList<HavokRagdollMapping> Reverse(IReadOnlyList<HavokRagdollMapping> rows)
    {
        ArgumentNullException.ThrowIfNull(rows);

        var reversed = new List<HavokRagdollMapping>(rows.Count);
        foreach (var row in rows)
        {
            var inverse = Invert(Frame(row.AFromBTranslation, row.AFromBRotation));
            reversed.Add(new HavokRagdollMapping(row.BoneB, row.BoneA, inverse.Position, inverse.Rotation));
        }
        return reversed;
    }

    public static IReadOnlyList<string> Refusals(
        HavokRagdollSkeleton skeletonA,
        HavokRagdollSkeleton skeletonB,
        IReadOnlyList<(int BoneA, int BoneB)> pairs)
    {
        ArgumentNullException.ThrowIfNull(skeletonA);
        ArgumentNullException.ThrowIfNull(skeletonB);
        ArgumentNullException.ThrowIfNull(pairs);

        var refusals = new List<string>();
        if (pairs.Count == 0) refusals.Add("a mapping needs at least one bone pair");

        var mappedA = new HashSet<int>();
        var mappedB = new HashSet<int>();
        for (int i = 0; i < pairs.Count; i++)
        {
            var (boneA, boneB) = pairs[i];

            if (boneA < 0 || boneA >= skeletonA.BoneNames.Count)
                refusals.Add($"pair {i}: bone {boneA} is not a bone of {Describe(skeletonA)}");
            else if (!mappedA.Add(boneA))
                refusals.Add($"pair {i}: bone '{Name(skeletonA, boneA)}' of {Describe(skeletonA)} is already mapped");

            if (boneB < 0 || boneB >= skeletonB.BoneNames.Count)
                refusals.Add($"pair {i}: bone {boneB} is not a bone of {Describe(skeletonB)}");
            else if (!mappedB.Add(boneB))
                refusals.Add($"pair {i}: bone '{Name(skeletonB, boneB)}' of {Describe(skeletonB)} is already mapped");
        }

        return refusals;
    }

    private static HavokBoneFrame Compose(HavokBoneFrame outer, HavokBoneFrame inner) =>
        new(outer.Position + Vector3.Transform(inner.Position, outer.Rotation),
            Normalize(outer.Rotation * inner.Rotation));

    private static HavokBoneFrame Invert(HavokBoneFrame frame)
    {
        var inverse = Quaternion.Inverse(frame.Rotation);
        return new HavokBoneFrame(-Vector3.Transform(frame.Position, inverse), inverse);
    }

    private static HavokBoneFrame Frame(Vector3 position, Quaternion rotation) => new(position, Normalize(rotation));

    private static Quaternion Normalize(Quaternion rotation) =>
        rotation.LengthSquared() < 1e-10f ? Quaternion.Identity : Quaternion.Normalize(rotation);

    private static string Name(HavokRagdollSkeleton skeleton, int bone) =>
        bone >= 0 && bone < skeleton.BoneNames.Count ? skeleton.BoneNames[bone] : $"bone {bone}";

    private static string Describe(HavokRagdollSkeleton skeleton) =>
        skeleton.Name.Length > 0 ? $"skeleton '{skeleton.Name}'" : "the skeleton";
}
