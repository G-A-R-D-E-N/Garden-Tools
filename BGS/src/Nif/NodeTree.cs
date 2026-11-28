using System;
using System.Collections.Generic;
using System.Linq;
using System.Numerics;
using OpenCommonwealth.Services.Hkx;
using Radicle.Interop;
using HkxSkeleton = OpenCommonwealth.Services.Hkx.HkxSkeleton;

namespace OpenCommonwealth.Services.Nif;

public sealed class NodeTree
{
    public readonly record struct Link(int SkeletonBone, Matrix4x4 LocalChain, string Source);

    private readonly Dictionary<string, int> _bones = new(StringComparer.OrdinalIgnoreCase);
    private readonly HashSet<string> _duplicates;
    private readonly List<(IReadOnlyList<NifNode> Nodes, string Source)> _trees = new();

    public NodeTree(HkxSkeleton skeleton)
    {
        _duplicates = SkinnedMesh.DuplicateBoneNames(skeleton);
        for (int i = 0; i < skeleton.BoneNames.Count; i++) _bones.TryAdd(skeleton.BoneNames[i], i);
    }

    public void Add(IReadOnlyList<NifNode> nodes, string source) => _trees.Add((nodes, source));

    public bool IsDuplicate(string name) => _duplicates.Contains(name);

    public Link? Resolve(string name)
    {
        foreach (var (link, _) in Candidates(name)) return link;
        string parent = SkinnedMesh.ResolveSkinBoneName(name, _bones.ContainsKey);
        return _bones.TryGetValue(parent, out int fallback)
            ? new Link(fallback, Matrix4x4.Identity, "interim approximation") : null;
    }

    public bool IsAmbiguous(string name)
    {
        var found = Candidates(name).ToList();
        return found.Any(candidate => candidate.Duplicate) ||
               found.Select(candidate => (candidate.Link.SkeletonBone, candidate.Link.LocalChain)).Distinct().Count() > 1;
    }

    private IEnumerable<(Link Link, bool Duplicate)> Candidates(string name)
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
                    if (_bones.TryGetValue(node.Name, out int bone))
                    {
                        yield return (new Link(bone, chain, source), _duplicates.Contains(node.Name));
                        break;
                    }
                    chain *= node.LocalMatrix;
                    node = node.Parent;
                }
            }
        }
    }

    public static Matrix4x4 World(AnimationPose.Pose pose, Link link) =>
        link.LocalChain * pose.Bones[link.SkeletonBone].World;
}
