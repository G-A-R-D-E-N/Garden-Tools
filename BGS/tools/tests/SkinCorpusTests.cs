using System.Numerics;
using BehaviourStudio.App;
using OpenCommonwealth.Services.Archive;
using OpenCommonwealth.Services.Hkx;
using OpenCommonwealth.Services.Nif;
using Xunit;
using Xunit.Abstractions;
using R = Radicle.Interop;

namespace BehaviourStudio.Tests;

public sealed class SkinCorpusTests(ITestOutputHelper output)
{
    [SkinDataFact]
    public void ReferencePoseCorpusComparesActualInterimAndNodeResolver()
    {
        string folder = Environment.GetEnvironmentVariable("BGS_SKIN_DATA")!;
        using var data = GameData.Discover(folder);
        var meshes = data.EnumerateWinners("Meshes/Actors", p => p.EndsWith(".nif", StringComparison.OrdinalIgnoreCase));
        var skeletons = data.EnumerateWinners("Meshes/Actors", p => p.EndsWith("skeleton.hkx", StringComparison.OrdinalIgnoreCase));
        var assets = meshes.Concat(skeletons).ToArray();
        var byName = assets.GroupBy(a => a.VirtualPath.Replace('\\', '/'), StringComparer.OrdinalIgnoreCase)
            .ToDictionary(g => g.Key, g => g.First(), StringComparer.OrdinalIgnoreCase);
        var candidates = assets.Where(a => a.VirtualPath.EndsWith(".nif", StringComparison.OrdinalIgnoreCase))
            .OrderBy(a => Priority(a.VirtualPath)).ThenBy(a => a.VirtualPath, StringComparer.OrdinalIgnoreCase);
        int files = 0, vertices = 0, rawMissing = 0, interimMissing = 0, newMissing = 0;
        double oldSum = 0, newSum = 0;
        float oldWorst = 0, newWorst = 0;
        string oldWorstPath = "", newWorstPath = "";
        foreach (var asset in candidates)
        {
            if (files >= 55) break;
            string path = asset.VirtualPath.Replace('\\', '/');
            string? pair = Pair(path);
            if (pair == null) continue;
            byte[] bytes = data.Read(asset)!;
            var shapes = NifGeometry.Shapes(NifFile.Parse(bytes, path)).Where(s => s.BoneNames.Count > 0).ToArray();
            if (shapes.Length == 0) continue;
            var skeleton = new HkxBinaryReader().ReadSkeleton(data.Read(byName[pair])!);
            if (skeleton.BoneNames.Count == 0) continue;
            var pose = AnimationPose.ReferencePose(skeleton);
            var tree = new NodeTree(skeleton);
            tree.Add(R.NifFile.FromBytes(data.Read(byName[Path.ChangeExtension(pair, ".nif").Replace('\\', '/')])!).Nodes, "skeleton.nif");
            tree.Add(R.NifFile.FromBytes(bytes).Nodes, "body NIF");
            int count = 0, raw = 0, oldMissing = 0, missing = 0;
            double os = 0, ns = 0;
            float om = 0, nm = 0;
            var oldShapes = NifGeometry.Shapes(NifFile.Parse(bytes, path)).Where(s => s.BoneNames.Count > 0).ToArray();
            for (int s = 0; s < shapes.Length; s++)
            {
                var shape = shapes[s];
                var binding = SkinnedMesh.Bind(shape, skeleton);
                raw += binding.Unmatched.Count;
                SkinnedMesh.Resolve(binding, shape, tree);
                missing += binding.Unmatched.Count;
                var old = oldShapes[s];
                MainWindow.ApproximateHelperBoneInterim(old, skeleton);
                var oldBinding = SkinnedMesh.Bind(old, skeleton);
                oldMissing += oldBinding.Unmatched.Count;
                int anchor = Array.FindIndex(binding.ToSkeleton, b => b >= 0);
                if (anchor < 0 || SkinnedMesh.BoneMatrix(shape, binding, pose, anchor) is not { } placement ||
                    !Matrix4x4.Invert(placement, out var inverse)) continue;
                var actual = SkinnedMesh.Pose(shape, binding, pose, skeleton);
                var interim = SkinnedMesh.Pose(old, oldBinding, pose, skeleton);
                for (int i = 0; i < actual.Length; i++)
                {
                    float oe = Vector3.Distance(Vector3.Transform(interim[i], inverse), shape.Vertices[i]);
                    float ne = Vector3.Distance(Vector3.Transform(actual[i], inverse), shape.Vertices[i]);
                    Assert.True(float.IsFinite(oe) && float.IsFinite(ne), path);
                    os += oe; ns += ne; om = Math.Max(om, oe); nm = Math.Max(nm, ne); count++;
                }
            }
            if (count == 0) continue;
            Assert.True(missing <= raw, path);
            files++; vertices += count; rawMissing += raw; interimMissing += oldMissing; newMissing += missing;
            oldSum += os; newSum += ns;
            if (om > oldWorst) { oldWorst = om; oldWorstPath = path; }
            if (nm > newWorst) { newWorst = nm; newWorstPath = path; }
            output.WriteLine($"CORPUS {path} | vertices {count} | unresolved {raw}/{oldMissing}/{missing} | interim max/mean {om:F6}/{os / count:F6} | resolver {nm:F6}/{ns / count:F6} | skeleton {pair}");
        }
        output.WriteLine($"TOTAL files {files}, vertices {vertices}, unresolved raw/interim/resolver {rawMissing}/{interimMissing}/{newMissing}, interim max/mean {oldWorst:F6}/{oldSum / vertices:F6}, resolver {newWorst:F6}/{newSum / vertices:F6}; worst interim {oldWorstPath}; worst resolver {newWorstPath}");
        Assert.Equal(55, files);

        string? Pair(string path)
        {
            var parts = path.Split('/');
            for (int n = parts.Length - 1; n > 0; n--)
            {
                string root = string.Join('/', parts.Take(n));
                foreach (string hkx in new[] { root + "/skeleton.hkx", root + "/CharacterAssets/skeleton.hkx" })
                    if (byName.ContainsKey(hkx) && byName.ContainsKey(Path.ChangeExtension(hkx, ".nif").Replace('\\', '/'))) return hkx;
            }
            return null;
        }
    }

    private static int Priority(string path)
    {
        string name = Path.GetFileName(path).ToLowerInvariant();
        if (name == "malebody.nif") return 0;
        if (name == "femalebody.nif") return 1;
        if (name.Contains("1stperson") && name.Contains("body")) return 2;
        if (path.Contains("radroach", StringComparison.OrdinalIgnoreCase)) return 3;
        return 4;
    }
}
