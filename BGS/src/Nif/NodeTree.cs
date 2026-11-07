using System;
using System.Collections.Generic;
using System.Numerics;
using OpenCommonwealth.Services.Hkx;
using Radicle.Interop;

namespace OpenCommonwealth.Services.Nif;

public sealed class NodeTree
{
    public readonly record struct Link(int SkeletonBone, Matrix4x4 LocalChain, string Source);

    private readonly Dictionary<string, int> _bones = new(StringComparer.OrdinalIgnoreCase);
    private readonly List<(IReadOnlyList<NifNode> Nodes, string Source)> _trees = new();

    public NodeTree(HkxSkeleton skeleton)
    {
        for (int i = 0; i < skeleton.BoneNames.Count; i++) _bones.TryAdd(skeleton.BoneNames[i], i);
    }

    public void Add(IReadOnlyList<NifNode> nodes, string source) => _trees.Add((nodes, source));

    public Link? Resolve(string name)
    {
        foreach (var (nodes, source) in _trees)
        {
            foreach (var start in nodes)
            {
                if (!start.Name.Equals(name, StringComparison.OrdinalIgnoreCase)) continue;
                var chain = Matrix4x4.Identity;
                NifNode? node = start;
                for (int steps = 0; node != null && steps < nodes.Count; steps++)
                {
                    if (_bones.TryGetValue(node.Name, out int bone)) return new Link(bone, chain, source);
                    chain *= node.LocalMatrix;
                    node = node.Parent;
                }
            }
        }
        string parent = SkinnedMesh.ResolveSkinBoneName(name, _bones.ContainsKey);
        return _bones.TryGetValue(parent, out int fallback)
            ? new Link(fallback, Matrix4x4.Identity, "interim approximation") : null;
    }

    public static Matrix4x4 World(AnimationPose.Pose pose, Link link)
    {
        var bone = pose.Bones[link.SkeletonBone];
        return link.LocalChain * Matrix4x4.CreateFromQuaternion(bone.Rotation) *
               Matrix4x4.CreateTranslation(bone.Position);
    }
}
