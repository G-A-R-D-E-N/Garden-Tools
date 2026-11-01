using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Numerics;
using System.Text.Json;

namespace OpenCommonwealth.Services.Hkx;

public sealed class GltfNode
{
    public string Name { get; init; } = "";
    public int Parent { get; set; } = -1;
    public Vector3 Translation { get; init; } = Vector3.Zero;
    public Quaternion Rotation { get; init; } = Quaternion.Identity;
    public Vector3 Scale { get; init; } = Vector3.One;
}

public sealed class GltfChannel
{
    public int Node { get; init; }
    public string Path { get; init; } = "";
    public string Interpolation { get; init; } = "LINEAR";
    public float[] Times { get; init; } = Array.Empty<float>();
    public float[] Values { get; init; } = Array.Empty<float>();
}

public sealed class GltfAnimation
{
    public string Name { get; init; } = "";
    public List<GltfChannel> Channels { get; } = new();
}

public sealed class GltfScene
{
    private const int FloatComponent = 5126;

    public List<GltfNode> Nodes { get; } = new();
    public List<int[]> Skins { get; } = new();
    public List<GltfAnimation> Animations { get; } = new();

    public static GltfScene Read(string path)
    {
        string extension = Path.GetExtension(path);
        if (!extension.Equals(".gltf", StringComparison.OrdinalIgnoreCase) &&
            !extension.Equals(".glb", StringComparison.OrdinalIgnoreCase))
            throw new NotSupportedException($"{Path.GetFileName(path)} is not a .gltf or .glb file.");

        string directory = Path.GetDirectoryName(Path.GetFullPath(path)) ?? ".";
        return Parse(File.ReadAllBytes(path), directory);
    }

    public static GltfScene Parse(byte[] file, string directory)
    {
        ArgumentNullException.ThrowIfNull(file);

        byte[] json = file;
        byte[]? bin = null;
        if (IsBinary(file)) (json, bin) = SplitBinary(file);

        using var document = Parsed(json);
        var root = document.RootElement;
        var buffers = ReadBuffers(root, directory, bin);
        var scene = new GltfScene();

        var nodes = Items(root, "nodes");
        foreach (var node in nodes) scene.Nodes.Add(ReadNode(node));
        for (int i = 0; i < nodes.Length; i++)
            foreach (int child in Ints(nodes[i], "children"))
            {
                if (child < 0 || child >= scene.Nodes.Count)
                    throw new InvalidDataException($"node {i} lists child {child}, which does not exist.");
                if (scene.Nodes[child].Parent >= 0)
                    throw new InvalidDataException($"node {child} is listed under two parents.");
                scene.Nodes[child].Parent = i;
            }

        for (int i = 0; i < scene.Nodes.Count; i++)
        {
            int steps = 0;
            for (int p = scene.Nodes[i].Parent; p >= 0; p = scene.Nodes[p].Parent)
                if (++steps > scene.Nodes.Count)
                    throw new InvalidDataException("the glTF node hierarchy has a cycle.");
        }

        foreach (var skin in Items(root, "skins"))
        {
            int[] joints = Ints(skin, "joints").ToArray();
            foreach (int joint in joints)
                if (joint < 0 || joint >= scene.Nodes.Count)
                    throw new InvalidDataException($"a skin names joint {joint}, which does not exist.");
            scene.Skins.Add(joints);
        }

        var accessors = Items(root, "accessors");
        var views = Items(root, "bufferViews");
        var decoded = new Dictionary<int, (float[] Values, int Count, int Width)>();
        (float[] Values, int Count, int Width) Accessor(int index)
        {
            if (!decoded.TryGetValue(index, out var value))
                decoded[index] = value = ReadAccessor(accessors, views, buffers, index);
            return value;
        }
        foreach (var animation in Items(root, "animations"))
        {
            var samplers = Items(animation, "samplers");
            var clip = new GltfAnimation { Name = Text(animation, "name") };

            foreach (var channel in Items(animation, "channels"))
            {
                if (!channel.TryGetProperty("target", out var target)) continue;

                int node = Int(target, "node", -1);
                string path = Text(target, "path");
                if (node < 0 || node >= scene.Nodes.Count)
                    throw new InvalidDataException($"an animation channel targets node {node}, which does not exist.");

                if (path == "weights")
                {
                    clip.Channels.Add(new GltfChannel { Node = node, Path = path });
                    continue;
                }

                int sampler = Int(channel, "sampler", -1);
                if (sampler < 0 || sampler >= samplers.Length)
                    throw new InvalidDataException($"an animation channel names sampler {sampler}, which does not exist.");

                string interpolation = Text(samplers[sampler], "interpolation");
                if (interpolation.Length == 0) interpolation = "LINEAR";

                var times = Accessor(Int(samplers[sampler], "input", -1));
                var values = Accessor(Int(samplers[sampler], "output", -1));

                int perKey = interpolation == "CUBICSPLINE" ? 3 : 1;
                if (times.Width != 1 || times.Count < 1 || values.Count != times.Count * perKey)
                    throw new InvalidDataException($"a {path} sampler has {values.Count} value(s) for {times.Count} key(s).");

                int width = path switch
                {
                    "translation" or "scale" => 3,
                    "rotation" => 4,
                    _ => throw new NotSupportedException($"animation channels that target {path} are not read."),
                };
                if (values.Width != width)
                    throw new InvalidDataException($"a {path} sampler has {values.Width}-component values, not {width}.");

                for (int k = 1; k < times.Values.Length; k++)
                    if (!(times.Values[k] > times.Values[k - 1]))
                        throw new InvalidDataException($"a {path} sampler's key times do not increase.");

                clip.Channels.Add(new GltfChannel
                {
                    Node = node,
                    Path = path,
                    Interpolation = interpolation,
                    Times = times.Values,
                    Values = values.Values,
                });
            }

            scene.Animations.Add(clip);
        }

        return scene;
    }

    private static bool IsBinary(byte[] file) =>
        file.Length >= 4 && file[0] == 'g' && file[1] == 'l' && file[2] == 'T' && file[3] == 'F';

    private static (byte[] Json, byte[]? Bin) SplitBinary(byte[] file)
    {
        if (file.Length < 12 || BinaryPrimitives.ReadUInt32LittleEndian(file.AsSpan(4)) != 2)
            throw new InvalidDataException(".glb header is not glTF version 2.");

        long length = Math.Min((long)BinaryPrimitives.ReadUInt32LittleEndian(file.AsSpan(8)), file.Length);
        byte[]? json = null, bin = null;
        long at = 12;
        while (at + 8 <= length)
        {
            long size = BinaryPrimitives.ReadUInt32LittleEndian(file.AsSpan((int)at));
            uint type = BinaryPrimitives.ReadUInt32LittleEndian(file.AsSpan((int)at + 4));
            at += 8;
            if (size > length - at) throw new InvalidDataException(".glb chunk runs past the end of the file.");

            byte[] chunk = file[(int)at..(int)(at + size)];
            if (type == 0x4E4F534A && json == null) json = chunk;
            else if (type == 0x004E4942 && bin == null) bin = chunk;
            at += size;
        }

        return (json ?? throw new InvalidDataException(".glb has no JSON chunk."), bin);
    }

    private static byte[][] ReadBuffers(JsonElement root, string directory, byte[]? bin)
    {
        var items = Items(root, "buffers");
        var buffers = new byte[items.Length][];
        for (int i = 0; i < items.Length; i++)
        {
            byte[] data;
            if (items[i].TryGetProperty("uri", out var uri)) data = ReadUri(uri.GetString() ?? "", directory);
            else if (i == 0 && bin != null) data = bin;
            else throw new InvalidDataException($"buffer {i} has no uri and is not a .glb's binary chunk.");

            if (Int(items[i], "byteLength", data.Length) > data.Length)
                throw new InvalidDataException($"buffer {i} declares more bytes than it holds.");
            buffers[i] = data;
        }
        return buffers;
    }

    private static byte[] ReadUri(string uri, string directory)
    {
        if (uri.StartsWith("data:", StringComparison.OrdinalIgnoreCase))
        {
            int comma = uri.IndexOf(',');
            if (comma < 0) throw new InvalidDataException("a data: buffer has no payload.");
            return Convert.FromBase64String(uri[(comma + 1)..]);
        }

        string relative = Uri.UnescapeDataString(uri);
        if (relative.Contains("://", StringComparison.Ordinal) || Path.IsPathRooted(relative))
            throw new NotSupportedException($"external buffer '{uri}' must be a relative file beside the glTF.");

        string root = Path.GetFullPath(directory);
        string full = Path.GetFullPath(Path.Combine(root, relative));
        string inside = Path.GetRelativePath(root, full);
        if (inside == ".." || inside.StartsWith(".." + Path.DirectorySeparatorChar, StringComparison.Ordinal) ||
            Path.IsPathRooted(inside))
            throw new NotSupportedException($"external buffer '{uri}' lies outside the glTF's folder.");
        return File.ReadAllBytes(full);
    }

    private static JsonDocument Parsed(byte[] json)
    {
        try
        {
            return JsonDocument.Parse(json);
        }
        catch (JsonException error)
        {
            throw new InvalidDataException("the glTF JSON is malformed: " + error.Message, error);
        }
    }

    private static GltfNode ReadNode(JsonElement node)
    {
        var translation = Vector3Of(node, "translation", Vector3.Zero);
        var rotation = QuaternionOf(node, "rotation");
        var scale = Vector3Of(node, "scale", Vector3.One);

        if (node.TryGetProperty("matrix", out var matrix))
        {
            var m = matrix.EnumerateArray().Select(v => v.GetSingle()).ToArray();
            if (m.Length != 16) throw new InvalidDataException("a node matrix has no 16 values.");
            var full = new Matrix4x4(m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7],
                                     m[8], m[9], m[10], m[11], m[12], m[13], m[14], m[15]);
            if (!Matrix4x4.Decompose(full, out scale, out rotation, out translation))
                throw new InvalidDataException($"node '{Text(node, "name")}' has a degenerate matrix.");
        }

        return new GltfNode
        {
            Name = Text(node, "name"),
            Translation = translation,
            Rotation = rotation,
            Scale = scale,
        };
    }

    private static (float[] Values, int Count, int Width) ReadAccessor(
        JsonElement[] accessors, JsonElement[] views, byte[][] buffers, int index)
    {
        if (index < 0 || index >= accessors.Length)
            throw new InvalidDataException($"an animation names accessor {index}, which does not exist.");

        var accessor = accessors[index];
        if (accessor.TryGetProperty("sparse", out _))
            throw new NotSupportedException("sparse accessors are not read, so the glTF was refused rather than read as zeros.");
        if (Int(accessor, "componentType", 0) != FloatComponent)
            throw new NotSupportedException("animation data must be 32-bit floats.");
        if (accessor.TryGetProperty("normalized", out var normalized) && normalized.GetBoolean())
            throw new NotSupportedException("normalized animation data is not read.");

        int width = Text(accessor, "type") switch
        {
            "SCALAR" => 1,
            "VEC3" => 3,
            "VEC4" => 4,
            var type => throw new NotSupportedException($"an animation accessor of type {type} is not read."),
        };

        int count = Int(accessor, "count", -1);
        int viewIndex = Int(accessor, "bufferView", -1);
        if (count < 0 || viewIndex < 0 || viewIndex >= views.Length)
            throw new InvalidDataException("an animation accessor has no usable buffer view.");

        var view = views[viewIndex];
        int bufferIndex = Int(view, "buffer", -1);
        if (bufferIndex < 0 || bufferIndex >= buffers.Length)
            throw new InvalidDataException("a buffer view names a buffer that does not exist.");

        byte[] data = buffers[bufferIndex];
        int stride = Int(view, "byteStride", width * 4);
        if (stride < width * 4) throw new InvalidDataException("a buffer view's byteStride is too short.");

        long start = (long)Int(view, "byteOffset", 0) + Int(accessor, "byteOffset", 0);
        long end = count == 0 ? start : start + (long)(count - 1) * stride + width * 4;
        if (start < 0 || end > data.Length)
            throw new InvalidDataException("an animation accessor reads past the end of its buffer.");

        var values = new float[count * width];
        for (int i = 0; i < count; i++)
            for (int c = 0; c < width; c++)
                values[i * width + c] = BinaryPrimitives.ReadSingleLittleEndian(
                    data.AsSpan((int)(start + (long)i * stride + c * 4), 4));

        return (values, count, width);
    }

    private static JsonElement[] Items(JsonElement parent, string name) =>
        parent.TryGetProperty(name, out var list) && list.ValueKind == JsonValueKind.Array
            ? list.EnumerateArray().ToArray()
            : Array.Empty<JsonElement>();

    private static IEnumerable<int> Ints(JsonElement parent, string name) =>
        parent.TryGetProperty(name, out var list) && list.ValueKind == JsonValueKind.Array
            ? list.EnumerateArray().Select(v => v.GetInt32())
            : Enumerable.Empty<int>();

    private static int Int(JsonElement parent, string name, int fallback) =>
        parent.TryGetProperty(name, out var value) && value.ValueKind == JsonValueKind.Number
            ? value.GetInt32()
            : fallback;

    private static string Text(JsonElement parent, string name) =>
        parent.TryGetProperty(name, out var value) && value.ValueKind == JsonValueKind.String
            ? value.GetString() ?? ""
            : "";

    private static Vector3 Vector3Of(JsonElement parent, string name, Vector3 fallback)
    {
        if (!parent.TryGetProperty(name, out var value)) return fallback;
        var v = value.EnumerateArray().Select(x => x.GetSingle()).ToArray();
        if (v.Length != 3) throw new InvalidDataException($"a node's {name} has {v.Length} values, not 3.");
        return new Vector3(v[0], v[1], v[2]);
    }

    private static Quaternion QuaternionOf(JsonElement parent, string name)
    {
        if (!parent.TryGetProperty(name, out var value)) return Quaternion.Identity;
        var q = value.EnumerateArray().Select(x => x.GetSingle()).ToArray();
        if (q.Length != 4) throw new InvalidDataException($"a node's {name} has {q.Length} values, not 4.");
        var quaternion = new Quaternion(q[0], q[1], q[2], q[3]);
        return quaternion.LengthSquared() > 1e-12f ? Quaternion.Normalize(quaternion) : Quaternion.Identity;
    }
}
