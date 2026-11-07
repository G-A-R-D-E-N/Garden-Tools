using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using OpenCommonwealth.Services.Hkx;
using OpenCommonwealth.Services.Nif;
using Radicle.Interop;

namespace BehaviourStudio.App;

public partial class MainWindow
{
    private IReadOnlyList<NifNode>? _skinSkeletonNodes;
    private string? _pendingSkinMesh;

    private static IReadOnlyList<NifNode>? ReadSkinNodes(Func<byte[]?> read)
    {
        try
        {
            return read() is { } bytes ? Radicle.Interop.NifFile.FromBytes(bytes).Nodes : null;
        }
        catch (Exception) { return null; }
    }

    private void PrepareSkinSkeleton(string key, string? archiveEntry)
    {
        _skinSkeletonNodes = null;
        IEnumerable<OpenCommonwealth.Services.Archive.GameData> sources = _gameData == null
            ? _scopedGameData : new[] { _gameData }.Concat(_scopedGameData);
        foreach (var data in sources)
        {
            _skinSkeletonNodes = ReadSkinNodes(() =>
            {
                string? packed = archiveEntry ?? PackedAnimationKey(data, key, null);
                string? entry = packed == null ? null : data.SkeletonEntryName(packed);
                return entry != null && data.TryReadSkeletonEntry(Path.ChangeExtension(entry, ".nif"), out var bytes)
                    ? bytes : null;
            });
            if (_skinSkeletonNodes != null) return;
        }
        _skinSkeletonNodes = ReadSkinNodes(() => SiblingSkeletonPath(key) is { } sibling
            ? File.ReadAllBytes(Path.ChangeExtension(sibling, ".nif")) : null);
    }

    private NodeTree MeshNodeTree(string path, HkxSkeleton skeleton)
    {
        var tree = new NodeTree(skeleton);
        if (_skinSkeletonNodes != null) tree.Add(_skinSkeletonNodes, "skeleton.nif");
        if (ReadSkinNodes(() => File.ReadAllBytes(path)) is { } nodes) tree.Add(nodes, "body NIF");
        return tree;
    }

    private void RebindMesh(HkxSkeleton skeleton)
    {
        if (_meshShapes.Count == 0) return;
        var tree = MeshNodeTree(_meshPath, skeleton);
        for (int i = 0; i < _meshShapes.Count; i++)
        {
            var (shape, _, edges) = _meshShapes[i];
            var binding = SkinnedMesh.Bind(shape, skeleton);
            SkinnedMesh.Resolve(binding, shape, tree);
            _meshShapes[i] = (shape, binding, edges);
        }
    }

    private string SkinResolutionSummary()
    {
        int total = _meshShapes.Sum(m => m.Binding.Total);
        int exact = _meshShapes.Sum(m => m.Binding.ToSkeleton.Count(i => i >= 0));
        var sources = _meshShapes.SelectMany(m => m.Binding.Resolved)
            .Where(link => link.HasValue).GroupBy(link => link!.Value.Source)
            .Select(group => $"{group.Count()} from {group.Key}");
        int missing = _meshShapes.Sum(m => m.Binding.Unmatched.Count);
        return $"{total - missing} of {total} bones: {exact} from the Havok skeleton" +
               string.Concat(sources.Select(source => ", " + source));
    }
}
