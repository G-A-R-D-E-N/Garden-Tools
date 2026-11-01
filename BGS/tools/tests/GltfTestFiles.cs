using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Numerics;
using System.Text;
using System.Text.Json;
using OpenCommonwealth.Services.Hkx;

namespace BehaviourStudio.Tests;

internal static class GltfTestFiles
{
    private const float MetresPerUnit = GltfAnimationImport.MetresPerUnit;

    private static readonly Matrix4x4 ToGltf = Matrix4x4.CreateRotationX(-MathF.PI / 2);

    public static string SkeletonPath => TestFixtures.Path("Meshes/Actors/Character/CharacterAssets/skeleton.hkx");

    public static string PairedKillPath =>
        TestFixtures.Path("Meshes/Actors/Character/Animations/Paired/PairedKill2HMBashKneeAndHead_AttackerLead.hkx");

    public static HkxSkeleton Skeleton() => new HkxBinaryReader().ReadSkeleton(SkeletonPath);

    public static HkxAnimationData PairedKill() => new HkxBinaryReader().ReadAnimation(PairedKillPath);

    public static string TurnLeft90Path => TestFixtures.Path("Meshes/Actors/RadRoach/Animations/TurnLeft90.hkx");

    public static HkxAnimationData TurnLeft90() => new HkxBinaryReader().ReadAnimation(TurnLeft90Path);

    public static (float Position, float Rotation, float Scale) ComparePoses(HkxSkeleton skeleton,
                                                                             HkxAnimationData want,
                                                                             HkxAnimationData got)
    {
        var wantTrack = AnimationPose.TracksByBone(skeleton, want);
        var gotTrack = AnimationPose.TracksByBone(skeleton, got);
        float position = 0, rotation = 0, scale = 0;
        for (int bone = 0; bone < skeleton.BoneNames.Count; bone++)
            for (int frame = 0; frame < want.NumFrames; frame++)
            {
                var a = AnimationPose.Local(skeleton, want, wantTrack[bone], bone, frame);
                var b = AnimationPose.Local(skeleton, got, gotTrack[bone], bone, frame);
                position = MathF.Max(position, (a.Translation - b.Translation).Length());
                rotation = MathF.Max(rotation, Angle(a.Rotation, b.Rotation));
                scale = MathF.Max(scale, (a.Scale - b.Scale).Length());
            }
        return (position, rotation, scale);
    }

    public static float Angle(Quaternion a, Quaternion b)
    {
        var relative = Quaternion.Conjugate(Quaternion.Normalize(a)) * Quaternion.Normalize(b);
        float vector = MathF.Sqrt(relative.X * relative.X + relative.Y * relative.Y + relative.Z * relative.Z);
        return 2f * MathF.Atan2(vector, MathF.Abs(relative.W));
    }

    public static HkxSkeleton Prefix(HkxSkeleton skeleton, int count) => new()
    {
        Name = skeleton.Name,
        BoneNames = skeleton.BoneNames.Take(count).ToList(),
        ParentIndices = skeleton.ParentIndices.Take(count).ToList(),
        ReferencePose = skeleton.ReferencePose.Take(count).ToList(),
    };

    public static HkxSkeleton Single(HkxSkeleton skeleton, int bone) => new()
    {
        Name = skeleton.Name,
        BoneNames = new List<string> { skeleton.BoneNames[bone] },
        ParentIndices = new List<int> { -1 },
        ReferencePose = new List<HkxBonePose> { skeleton.ReferencePose[bone] },
    };

    public static HkxAnimationData OneTrack(HkxAnimationData animation, int track) => new()
    {
        AnimationClass = animation.AnimationClass,
        Duration = animation.Duration,
        NumFrames = animation.NumFrames,
        NumTracks = 1,
        FrameDuration = animation.FrameDuration,
        BoneNames = new List<string> { animation.BoneNames[track] },
        TrackToBoneIndices = new List<int> { 0 },
        Tracks = new List<HkxTrackData> { animation.Tracks[track] },
    };

    public static (string Json, byte[] Bin) Export(HkxSkeleton skeleton, HkxAnimationData? animation, bool embedded)
    {
        int bones = skeleton.BoneNames.Count;
        var binary = new BinaryBuilder();
        var nodes = new List<object>();
        for (int b = 0; b < bones; b++)
        {
            var rest = ToGltfPose(AnimationPose.Local(skeleton, null, -1, b, 0), skeleton.ParentIndices[b] < 0);
            var children = Enumerable.Range(0, bones).Where(c => skeleton.ParentIndices[c] == b).ToArray();
            nodes.Add(new { name = skeleton.BoneNames[b], children, translation = rest.T, rotation = rest.R, scale = rest.S });
        }

        var samplers = new List<object>();
        var channels = new List<object>();
        if (animation != null)
        {
            var byBone = AnimationPose.TracksByBone(skeleton, animation);
            float step = AnimationEdit.FrameDuration(animation);
            int times = binary.Add(Enumerable.Range(0, animation.NumFrames).Select(f => f * step).ToArray(), "SCALAR");
            for (int b = 0; b < bones; b++)
            {
                if (byBone[b] < 0) continue;
                var keys = Enumerable.Range(0, animation.NumFrames)
                    .Select(f => ToGltfPose(AnimationPose.Local(skeleton, animation, byBone[b], b, f),
                                            skeleton.ParentIndices[b] < 0))
                    .ToList();
                AddChannel(samplers, channels, b, "translation", times,
                           binary.Add(keys.SelectMany(k => k.T).ToArray(), "VEC3"));
                AddChannel(samplers, channels, b, "rotation", times,
                           binary.Add(keys.SelectMany(k => k.R).ToArray(), "VEC4"));
                AddChannel(samplers, channels, b, "scale", times,
                           binary.Add(keys.SelectMany(k => k.S).ToArray(), "VEC3"));
            }
        }

        var document = new Dictionary<string, object>
        {
            ["asset"] = new { version = "2.0" },
            ["nodes"] = nodes,
            ["skins"] = new[] { new { joints = Enumerable.Range(0, bones).ToArray() } },
            ["animations"] = animation == null
                ? Array.Empty<object>()
                : new object[] { new { name = "clip", samplers, channels } },
            ["accessors"] = binary.Accessors,
            ["bufferViews"] = binary.Views,
            ["buffers"] = new[] { binary.Describe(embedded) },
        };
        return (JsonSerializer.Serialize(document), binary.Bytes.ToArray());
    }

    public static (string Json, byte[] Bin) Tiny(TinyOptions options)
    {
        var binary = new BinaryBuilder();
        int times = binary.Add(new[] { 0f, 1f }, "SCALAR");
        int rootTranslation = binary.Add(new[] { 0f, 0f, 0f, 0f, 0.1f, 0f }, "VEC3");
        int halfSecond = binary.Add(new[] { 0f, 0.5f }, "SCALAR");
        int rootRotation = binary.Add(new[]
        {
            0f, 0f, 0f, 1f,
            0f, MathF.Sin(MathF.PI / 4), 0f, MathF.Cos(MathF.PI / 4),
        }, "VEC4");
        int comTranslation = options.Cubic
            ? binary.Add(new[] { 0f, 0f, 0f, 0f, 0.2f, 0f, 0f, 0f, 0f, 0f, 0f, 0f, 0f, 0.4f, 0f, 0f, 0f, 0f }, "VEC3")
            : binary.Add(new[] { 0f, 0.2f, 0f, 0f, 0.4f, 0f }, "VEC3");

        var samplers = new List<object>
        {
            new { input = times, output = rootTranslation, interpolation = "LINEAR" },
            new { input = halfSecond, output = rootRotation, interpolation = "STEP" },
            new { input = halfSecond, output = comTranslation, interpolation = options.Cubic ? "CUBICSPLINE" : "STEP" },
        };
        var channels = new List<object>
        {
            new { sampler = 0, target = new { node = 0, path = "translation" } },
            new { sampler = 1, target = new { node = 0, path = "rotation" } },
            new { sampler = 2, target = new { node = 1, path = "translation" } },
        };
        if (options.ArmatureAnimated) channels.Add(new { sampler = 0, target = new { node = 3, path = "translation" } });

        var animations = new List<object> { new { name = "tiny", samplers, channels } };
        if (options.TwoAnimations) animations.Add(new { name = "again", samplers, channels });

        var armature = new Dictionary<string, object> { ["name"] = "Armature", ["children"] = new[] { 0 } };
        if (options.ArmatureYUp)
            armature["rotation"] = new[] { -MathF.Sin(MathF.PI / 4), 0f, 0f, MathF.Cos(MathF.PI / 4) };
        else
            armature["translation"] = new[] { 0f, 0.5f, 0f };

        var nodes = new List<object>
        {
            new { name = "Root", children = options.RootParentsCom ? new[] { 1 } : Array.Empty<int>() },
            new { name = options.Com, children = new[] { 2 } },
            new { name = "Spine1" },
            armature,
        };

        var document = new Dictionary<string, object>
        {
            ["asset"] = new { version = "2.0" },
            ["nodes"] = nodes,
            ["skins"] = new[] { new { joints = new[] { 0, 1, 2 } } },
            ["animations"] = animations,
            ["accessors"] = binary.Accessors,
            ["bufferViews"] = binary.Views,
            ["buffers"] = new[] { binary.Describe(options.Embedded) },
        };
        return (JsonSerializer.Serialize(document), binary.Bytes.ToArray());
    }

    public static byte[] Glb(string json, byte[] bin)
    {
        byte[] jsonBytes = Pad(Encoding.UTF8.GetBytes(json), 0x20);
        byte[] binBytes = Pad(bin, 0);

        using var stream = new MemoryStream();
        using (var writer = new BinaryWriter(stream))
        {
            writer.Write(0x46546C67u);
            writer.Write(2u);
            writer.Write((uint)(12 + 8 + jsonBytes.Length + 8 + binBytes.Length));
            writer.Write((uint)jsonBytes.Length);
            writer.Write(0x4E4F534Au);
            writer.Write(jsonBytes);
            writer.Write((uint)binBytes.Length);
            writer.Write(0x004E4942u);
            writer.Write(binBytes);
        }
        return stream.ToArray();
    }

    public static Dictionary<string, Vector3> WorldPositions(string json)
    {
        using var document = JsonDocument.Parse(json);
        var nodes = document.RootElement.GetProperty("nodes").EnumerateArray().ToArray();
        var parent = Enumerable.Repeat(-1, nodes.Length).ToArray();
        for (int i = 0; i < nodes.Length; i++)
            if (nodes[i].TryGetProperty("children", out var children))
                foreach (var child in children.EnumerateArray()) parent[child.GetInt32()] = i;

        Matrix4x4 World(int i)
        {
            var local = Matrix4x4.CreateScale(Vec3Of(nodes[i], "scale", Vector3.One)) *
                        Matrix4x4.CreateFromQuaternion(QuatOf(nodes[i])) *
                        Matrix4x4.CreateTranslation(Vec3Of(nodes[i], "translation", Vector3.Zero));
            return parent[i] >= 0 ? local * World(parent[i]) : local;
        }

        return Enumerable.Range(0, nodes.Length)
            .ToDictionary(i => nodes[i].GetProperty("name").GetString()!, i => World(i).Translation);
    }

    private static Vector3 Vec3Of(JsonElement node, string name, Vector3 fallback) =>
        node.TryGetProperty(name, out var v) ? new Vector3(v[0].GetSingle(), v[1].GetSingle(), v[2].GetSingle()) : fallback;

    private static Quaternion QuatOf(JsonElement node) =>
        node.TryGetProperty("rotation", out var q)
            ? new Quaternion(q[0].GetSingle(), q[1].GetSingle(), q[2].GetSingle(), q[3].GetSingle())
            : Quaternion.Identity;

    private static void AddChannel(List<object> samplers, List<object> channels, int node, string path,
                                   int input, int output)
    {
        samplers.Add(new { input, output, interpolation = "LINEAR" });
        channels.Add(new { sampler = samplers.Count - 1, target = new { node, path } });
    }

    private static (float[] T, float[] R, float[] S) ToGltfPose(HkxBonePose pose, bool root)
    {
        var m = Matrix4x4.CreateScale(pose.Scale) * Matrix4x4.CreateFromQuaternion(pose.Rotation) *
                Matrix4x4.CreateTranslation(pose.Translation);
        if (root) m *= ToGltf;

        Matrix4x4.Decompose(m, out var s, out var r, out var t);
        t *= MetresPerUnit;
        return (new[] { t.X, t.Y, t.Z }, new[] { r.X, r.Y, r.Z, r.W }, new[] { s.X, s.Y, s.Z });
    }

    private static byte[] Pad(byte[] bytes, byte fill)
    {
        var padded = new List<byte>(bytes);
        while (padded.Count % 4 != 0) padded.Add(fill);
        return padded.ToArray();
    }

    private sealed class BinaryBuilder
    {
        public List<byte> Bytes { get; } = new();
        public List<object> Accessors { get; } = new();
        public List<object> Views { get; } = new();

        public int Add(float[] values, string type)
        {
            int components = type switch { "SCALAR" => 1, "VEC3" => 3, _ => 4 };
            while (Bytes.Count % 4 != 0) Bytes.Add(0);
            Views.Add(new { buffer = 0, byteOffset = Bytes.Count, byteLength = values.Length * 4 });
            foreach (float value in values) Bytes.AddRange(BitConverter.GetBytes(value));
            Accessors.Add(new { bufferView = Views.Count - 1, componentType = 5126, count = values.Length / components, type });
            return Accessors.Count - 1;
        }

        public object Describe(bool embedded)
        {
            if (embedded)
                return new { byteLength = Bytes.Count, uri = "data:application/octet-stream;base64," + Convert.ToBase64String(Bytes.ToArray()) };
            return new { byteLength = Bytes.Count };
        }
    }
}

internal sealed record TinyOptions(bool Embedded = false, bool Cubic = false, string Com = "COM",
                                   bool RootParentsCom = true, bool ArmatureAnimated = false,
                                   bool TwoAnimations = false, bool ArmatureYUp = false);

internal sealed class GltfWork : IDisposable
{
    private readonly string _folder = Directory.CreateTempSubdirectory("bgs-gltf").FullName;

    public string At(string name) => Path.Combine(_folder, name);

    public string Write(string name, byte[] bytes)
    {
        string path = At(name);
        File.WriteAllBytes(path, bytes);
        return path;
    }

    public string Tiny(TinyOptions options, bool binary)
    {
        var (json, bin) = GltfTestFiles.Tiny(options with { Embedded = !binary });
        return binary
            ? Write("tiny.glb", GltfTestFiles.Glb(json, bin))
            : Write("tiny.gltf", Encoding.UTF8.GetBytes(json));
    }

    public void Dispose()
    {
        try { Directory.Delete(_folder, recursive: true); }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException) { }
    }
}
