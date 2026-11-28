using System;
using System.Collections.Generic;
using System.Linq;
using System.Numerics;
using OpenCommonwealth.Services.Hkx;

namespace OpenCommonwealth.Services.Nif;

public static class SkinnedMesh
{
    public sealed class Binding
    {

        public int[] ToSkeleton = Array.Empty<int>();
        public NodeTree.Link?[] Resolved = Array.Empty<NodeTree.Link?>();

        public readonly List<string> Unmatched = new();
        public readonly List<string> Ambiguous = new();

        public int Matched => ToSkeleton.Count(b => b >= 0);
        public int Total => ToSkeleton.Length;
        public bool Complete => Unmatched.Count == 0 && Total > 0;

        public override string ToString() =>
            Total == 0 ? "the shape names no bones"
            : Complete ? $"all {Total} bones matched the skeleton"
            : $"{Matched} of {Total} bones matched; no skeleton bone called " +
              string.Join(", ", Unmatched.Take(6)) +
              (Unmatched.Count > 6 ? $", and {Unmatched.Count - 6} more" : "");
    }

    public sealed class SkinScratch
    {
        public Matrix4x4[] BoneMatrices = Array.Empty<Matrix4x4>();
        public bool[] BoneUsable = Array.Empty<bool>();
    }

    public static Binding Bind(NifShape shape, HkxSkeleton skeleton)
    {
        var byName = new Dictionary<string, int>(StringComparer.OrdinalIgnoreCase);
        for (int i = 0; i < skeleton.BoneNames.Count; i++) byName.TryAdd(skeleton.BoneNames[i], i);

        var binding = new Binding { ToSkeleton = new int[shape.BoneNames.Count] };
        for (int i = 0; i < shape.BoneNames.Count; i++)
        {
            binding.ToSkeleton[i] = byName.TryGetValue(shape.BoneNames[i], out int at) ? at : -1;
            if (binding.ToSkeleton[i] < 0) binding.Unmatched.Add(shape.BoneNames[i]);
        }
        return binding;
    }

    public static HashSet<string> DuplicateBoneNames(HkxSkeleton skeleton)
    {
        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var duplicates = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (string name in skeleton.BoneNames)
            if (!seen.Add(name)) duplicates.Add(name);
        return duplicates;
    }

    public static void Resolve(Binding binding, NifShape shape, NodeTree tree)
    {
        binding.Resolved = new NodeTree.Link?[binding.Total];
        binding.Unmatched.Clear();
        binding.Ambiguous.Clear();
        for (int b = 0; b < binding.Total; b++)
        {
            string name = shape.BoneNames[b];
            if (binding.ToSkeleton[b] >= 0)
            {
                if (tree.IsDuplicate(name)) binding.Ambiguous.Add(name);
                continue;
            }
            binding.Resolved[b] = tree.Resolve(name);
            if (tree.IsAmbiguous(name)) binding.Ambiguous.Add(name);
            if (binding.Resolved[b] == null) binding.Unmatched.Add(name);
        }
    }

    public static string? BindingWarning(IEnumerable<Binding> bindings)
    {
        var all = bindings.ToList();
        var missing = all.SelectMany(b => b.Unmatched).Distinct().ToList();
        var ambiguous = all.SelectMany(b => b.Ambiguous).Distinct(StringComparer.OrdinalIgnoreCase).ToList();

        var parts = new List<string>();
        if (missing.Count > 0)
            parts.Add($"{missing.Count} bone{(missing.Count == 1 ? "" : "s")} did not match this " +
                      $"skeleton: {string.Join(", ", missing.Take(6))}" +
                      (missing.Count > 6 ? ", and more" : "") +
                      ". Those bones keep their whole weight, so a vertex weighted only to them follows " +
                      "this frame's placement of the bones that did match.");
        if (ambiguous.Count > 0)
            parts.Add($"{string.Join(", ", ambiguous.Take(6))}" +
                      (ambiguous.Count > 6 ? ", and more" : "") +
                      $" {(ambiguous.Count == 1 ? "matches" : "match")} more than one skeleton bone or helper node, " +
                      "so the preview used the first match.");
        return parts.Count == 0 ? null : string.Join(" ", parts);
    }

    private static Matrix4x4 Bind(Matrix4x4 stored)
    {
        var m = Matrix4x4.Transpose(stored);
        m.M41 = stored.M41;
        m.M42 = stored.M42;
        m.M43 = stored.M43;
        m.M14 = 0;
        m.M24 = 0;
        m.M34 = 0;
        m.M44 = 1;
        return m;
    }

    public static Matrix4x4? BoneMatrix(NifShape shape, Binding binding, AnimationPose.Pose pose,
                                        int bone)
    {
        if (bone < 0 || bone >= binding.ToSkeleton.Length || bone >= shape.SkinToBone.Count)
            return null;

        int at = binding.ToSkeleton[bone];
        if (at < 0)
        {
            if (bone >= binding.Resolved.Length || binding.Resolved[bone] is not { } resolved) return null;
            return resolved.SkeletonBone < pose.Bones.Count
                ? Bind(shape.SkinToBone[bone]) * NodeTree.World(pose, resolved)
                : null;
        }
        if (at >= pose.Bones.Count) return null;

        return Bind(shape.SkinToBone[bone]) * pose.Bones[at].World;
    }

    public static Vector3[] Pose(NifShape shape, Binding binding, AnimationPose.Pose pose,
                                 HkxSkeleton skeleton) =>
        Pose(shape, binding, pose, skeleton, Array.Empty<Vector3>(), new SkinScratch());

    public static Vector3[] Pose(NifShape shape, Binding binding, AnimationPose.Pose pose,
                                 HkxSkeleton skeleton, Vector3[] moved, SkinScratch scratch)
    {
        int vertexCount = shape.Vertices.Count;
        if (moved.Length != vertexCount) moved = new Vector3[vertexCount];

        if (!shape.IsSkinned || shape.SkinToBone.Count != shape.BoneNames.Count)
        {
            shape.Vertices.CopyTo(moved, 0);
            return moved;
        }

        int boneCount = shape.BoneNames.Count;
        if (scratch.BoneMatrices.Length != boneCount)
        {
            scratch.BoneMatrices = new Matrix4x4[boneCount];
            scratch.BoneUsable = new bool[boneCount];
        }

        var boneMatrix = scratch.BoneMatrices;
        var usable = scratch.BoneUsable;
        Array.Clear(usable);
        for (int b = 0; b < boneCount; b++)
        {
            if (BoneMatrix(shape, binding, pose, b) is not { } matrix) continue;
            boneMatrix[b] = matrix;
            usable[b] = true;
        }

        var frame = Placement(shape, binding, pose) ?? Matrix4x4.Identity;

        for (int v = 0; v < moved.Length; v++)
        {
            var source = shape.Vertices[v];
            var resting = Vector3.Transform(source, frame);
            var sum = Vector3.Zero;
            float total = 0;

            for (int s = 0; s < 4; s++)
            {
                float weight = shape.BoneWeights[v * 4 + s];
                if (!float.IsFinite(weight) || weight <= 0) continue;

                total += weight;

                int b = shape.BoneIndices[v * 4 + s];
                if (b < 0 || b >= boneMatrix.Length || !usable[b])
                {
                    sum += resting * weight;
                    continue;
                }

                sum += Vector3.Transform(source, boneMatrix[b]) * weight;
            }

            moved[v] = total > 0.0001f ? sum / total : resting;
        }

        return moved;
    }

    public static string ResolveSkinBoneName(string skinBoneName, Func<string, bool> hasBone)
    {
        if (hasBone(skinBoneName)) return skinBoneName;

        int skin = skinBoneName.LastIndexOf("_skin", StringComparison.OrdinalIgnoreCase);
        if (skin <= 0 || skin != skinBoneName.Length - 5) return skinBoneName;

        string parent = skinBoneName[..skin];
        return hasBone(parent) ? parent : skinBoneName;
    }

    public static Matrix4x4? Placement(NifShape shape, Binding binding, AnimationPose.Pose pose)
    {
        for (int b = 0; b < shape.BoneNames.Count; b++)
            if (BoneMatrix(shape, binding, pose, b) is { } m) return m;

        return null;
    }

    public static float BindError(NifShape shape, Binding binding, HkxSkeleton skeleton) =>
        BindError(shape, binding, skeleton, out _);

    public static float BindError(NifShape shape, Binding binding, HkxSkeleton skeleton, out int measured)
    {
        var rest = AnimationPose.ReferencePose(skeleton);
        var composed = new List<Matrix4x4>();

        for (int b = 0; b < shape.BoneNames.Count; b++)
            if (BoneMatrix(shape, binding, rest, b) is { } m) composed.Add(m);

        measured = composed.Count;
        if (measured < 2) return 0;

        float worst = 0;
        foreach (var m in composed)
            worst = Math.Max(worst, Disagreement(composed[0], m));

        return worst;
    }

    public static float Disagreement(Matrix4x4 a, Matrix4x4 b)
    {
        float worst = 0;
        for (int i = 0; i < 8; i++)
        {
            var corner = new Vector3((i & 1) == 0 ? -50 : 50,
                                     (i & 2) == 0 ? -50 : 50,
                                     (i & 4) == 0 ? -50 : 50);
            worst = Math.Max(worst, Vector3.Distance(Vector3.Transform(corner, a),
                                                     Vector3.Transform(corner, b)));
        }
        return worst;
    }

    public static bool FullyBound(NifShape shape, Binding binding, int vertex)
    {
        for (int s = 0; s < 4; s++)
        {
            if (shape.BoneWeights[vertex * 4 + s] <= 0) continue;

            int b = shape.BoneIndices[vertex * 4 + s];
            if (b < 0 || b >= binding.ToSkeleton.Length || binding.ToSkeleton[b] < 0) return false;
        }
        return true;
    }

    public static List<(int From, int To)> Edges(NifShape shape)
    {
        var seen = new HashSet<long>();
        var edges = new List<(int, int)>();

        for (int t = 0; t + 2 < shape.Indices.Count; t += 3)
        {
            int a = shape.Indices[t];
            int b = shape.Indices[t + 1];
            int c = shape.Indices[t + 2];
            if (a < 0 || a >= shape.Vertices.Count || b < 0 || b >= shape.Vertices.Count ||
                c < 0 || c >= shape.Vertices.Count) continue;

            foreach ((int x, int y) in new[] { (a, b), (b, c), (c, a) })
            {
                long key = x < y ? ((long)x << 32) | (uint)y : ((long)y << 32) | (uint)x;
                if (seen.Add(key)) edges.Add((x, y));
            }
        }

        return edges;
    }
}
