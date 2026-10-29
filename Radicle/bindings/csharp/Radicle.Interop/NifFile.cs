using System;
using System.Collections.Generic;
using System.Text;
using Radicle.Interop.Native;

namespace Radicle.Interop;

using Numerics = System.Numerics;

public sealed class NifBone
{
    public required string Name { get; init; }

    public required Numerics.Vector3 BoundsCenter { get; init; }

    public required float BoundsRadius { get; init; }

    public required float[] Rotation { get; init; }

    public required Numerics.Vector3 Translation { get; init; }

    public required float Scale { get; init; }

    public Numerics.Matrix4x4 BindMatrix => new(
        Rotation[0] * Scale, Rotation[1] * Scale, Rotation[2] * Scale, 0,
        Rotation[3] * Scale, Rotation[4] * Scale, Rotation[5] * Scale, 0,
        Rotation[6] * Scale, Rotation[7] * Scale, Rotation[8] * Scale, 0,
        Translation.X, Translation.Y, Translation.Z, 1);
}

public sealed class NifNode
{
    public required int Block { get; init; }

    public required string Type { get; init; }

    public required string Name { get; init; }

    public required int ParentIndex { get; init; }

    public required int[] ChildBlocks { get; init; }

    public required NifNode?[] Children { get; init; }

    public required Numerics.Vector3 Translation { get; init; }

    public required float[] Rotation { get; init; }

    public required float Scale { get; init; }

    public NifNode? Parent { get; internal set; }

    public bool IsRoot => ParentIndex < 0;

    public Numerics.Matrix4x4 LocalMatrix => new(
        Rotation[0] * Scale, Rotation[3] * Scale, Rotation[6] * Scale, 0,
        Rotation[1] * Scale, Rotation[4] * Scale, Rotation[7] * Scale, 0,
        Rotation[2] * Scale, Rotation[5] * Scale, Rotation[8] * Scale, 0,
        Translation.X, Translation.Y, Translation.Z, 1);

    internal int NodeCount { get; set; } = 1;

    public Numerics.Matrix4x4 WorldMatrix
    {
        get
        {
            var world = LocalMatrix;
            var current = Parent;
            for (int steps = 0; current != null; steps++)
            {
                if (steps >= NodeCount) throw new InvalidOperationException("the node's parent chain contains a cycle");
                world *= current.LocalMatrix;
                current = current.Parent;
            }
            return world;
        }
    }
}

public sealed class NifShape
{
    public required string Name { get; init; }

    public required Numerics.Vector3[] Positions { get; init; }

    public required Numerics.Vector3[] Normals { get; init; }

    public required Numerics.Vector3[] Tangents { get; init; }

    public required Numerics.Vector3[] Bitangents { get; init; }

    public required Numerics.Vector2[] Uvs { get; init; }

    public required Numerics.Vector4[] Colors { get; init; }

    public required ushort[] Triangles { get; init; }

    public required string Material { get; init; }

    public required string[] Textures { get; init; }

    public required float[] Weights { get; init; }

    public required byte[] BoneIndices { get; init; }

    public required NifBone[] Bones { get; init; }

    public required int ParentNodeIndex { get; init; }

    public NifNode? ParentNode { get; internal set; }

    public required string Warning { get; init; }

    public required int SkinReference { get; init; }

    public required int ShaderReference { get; init; }

    public required int AlphaReference { get; init; }

    public required Numerics.Vector3 GlobalTranslation { get; init; }

    public required float[] GlobalRotation { get; init; }

    public required float GlobalScale { get; init; }

    public int VertexCount => Positions.Length;

    public int TriangleCount => Triangles.Length / 3;

    public bool IsSkinned => Weights.Length == Positions.Length * 4 && Bones.Length > 0;
}

public sealed class NifFile
{
    private NifFile(NifNode[] nodes, NifShape[] shapes)
    {
        Nodes = nodes;
        Shapes = shapes;
    }

    public IReadOnlyList<NifNode> Nodes { get; }

    public IReadOnlyList<NifShape> Shapes { get; }

    public static unsafe NifFile FromBytes(ReadOnlySpan<byte> bytes)
    {
        if (bytes.IsEmpty) throw new ArgumentException("a NIF needs at least a header", nameof(bytes));
        IntPtr handle = IntPtr.Zero;
        fixed (byte* pointer = bytes)
        {
            Call.Check(RadicleNative.NifRead(pointer, (nuint)bytes.Length, &handle));
        }
        try
        {
            var nodes = ReadNodes(handle);
            var shapes = ReadShapes(handle, nodes);
            return new NifFile(nodes, shapes);
        }
        finally
        {
            RadicleNative.NifDestroy(handle);
        }
    }

    private static unsafe NifNode[] ReadNodes(IntPtr handle)
    {
        nuint nodeCount = 0;
        Call.Check(RadicleNative.NifNodeCount(handle, &nodeCount));
        var nodes = new NifNode[checked((int)nodeCount)];
        var childBlocks = new int[nodes.Length][];
        var translation = stackalloc float[3];
        var rotation = stackalloc float[9];
        for (var index = 0; index < nodes.Length; ++index)
        {
            uint block = 0;
            Call.Check(RadicleNative.NifNodeBlock(handle, (nuint)index, &block));
            int parent = -1;
            Call.Check(RadicleNative.NifNodeParent(handle, (nuint)index, &parent));

            uint* refs = null;
            nuint refCount = 0;
            Call.Check(RadicleNative.NifNodeChildren(handle, (nuint)index, &refs, &refCount));
            var blocks = Call.TakeNodeRefs(refs, refCount);

            float scale = 0;
            Call.Check(RadicleNative.NifNodeTransform(handle, (nuint)index, translation, rotation, &scale));

            childBlocks[index] = Array.ConvertAll(blocks, value => checked((int)value));
            nodes[index] = new NifNode {
                Block = checked((int)block),
                Type = ReadText(handle, (nuint)index, RadicleNative.NifNodeType),
                Name = ReadText(handle, (nuint)index, RadicleNative.NifNodeName),
                ParentIndex = parent,
                ChildBlocks = childBlocks[index],
                Children = new NifNode?[childBlocks[index].Length],
                Translation = new Numerics.Vector3(translation[0], translation[1], translation[2]),
                Rotation = Call.Copy<float>((IntPtr)rotation, 9),
                Scale = scale,
            };
        }

        for (var index = 0; index < nodes.Length; ++index)
        {
            nodes[index].NodeCount = nodes.Length;
            if (nodes[index].ParentIndex >= 0) nodes[index].Parent = nodes[nodes[index].ParentIndex];
            for (var slot = 0; slot < nodes[index].ChildBlocks.Length; ++slot)
            {
                int child = -1;
                Call.Check(RadicleNative.NifNodeForBlock(handle, (uint)nodes[index].ChildBlocks[slot], &child));
                if (child >= 0) nodes[index].Children[slot] = nodes[child];
            }
        }
        return nodes;
    }

    private static unsafe NifShape[] ReadShapes(IntPtr handle, NifNode[] nodes)
    {
        nuint shapeCount = 0;
        Call.Check(RadicleNative.NifShapeCount(handle, &shapeCount));
        var shapes = new NifShape[checked((int)shapeCount)];
        for (var index = 0; index < shapes.Length; ++index) shapes[index] = Snapshot(handle, nodes, (nuint)index);
        return shapes;
    }

    private unsafe delegate Status FloatReader(IntPtr nif, nuint index, float** values, nuint* count);

    private unsafe delegate Status UshortReader(IntPtr nif, nuint index, ushort** values, nuint* count);

    private unsafe delegate Status ByteReader(IntPtr nif, nuint index, byte** values, nuint* count);

    private static unsafe float[] ReadFloats(IntPtr handle, nuint index, FloatReader reader)
    {
        float* values = null;
        nuint count = 0;
        Call.Check(reader(handle, index, &values, &count));
        return Call.TakeFloats(values, count);
    }

    private static unsafe ushort[] ReadUshorts(IntPtr handle, nuint index, UshortReader reader)
    {
        ushort* values = null;
        nuint count = 0;
        Call.Check(reader(handle, index, &values, &count));
        return Call.TakeTriangles(values, count);
    }

    private static unsafe byte[] ReadBytes(IntPtr handle, nuint index, ByteReader reader)
    {
        byte* values = null;
        nuint count = 0;
        Call.Check(reader(handle, index, &values, &count));
        return Call.TakeBytes(values, count);
    }

    private static string ReadText(IntPtr handle, nuint index, ByteReader reader) =>
        Encoding.Latin1.GetString(ReadBytes(handle, index, reader));

    private static Numerics.Vector3[] ToVectors(float[] values)
    {
        var result = new Numerics.Vector3[values.Length / 3];
        for (var i = 0; i < result.Length; ++i) {
            result[i] = new Numerics.Vector3(values[i * 3], values[i * 3 + 1], values[i * 3 + 2]);
        }
        return result;
    }

    private static Numerics.Vector2[] ToTexCoords(float[] values)
    {
        var result = new Numerics.Vector2[values.Length / 2];
        for (var i = 0; i < result.Length; ++i) result[i] = new Numerics.Vector2(values[i * 2], values[i * 2 + 1]);
        return result;
    }

    private static Numerics.Vector4[] ToColors(float[] values)
    {
        var result = new Numerics.Vector4[values.Length / 4];
        for (var i = 0; i < result.Length; ++i) {
            result[i] = new Numerics.Vector4(values[i * 4], values[i * 4 + 1], values[i * 4 + 2], values[i * 4 + 3]);
        }
        return result;
    }

    private static unsafe NifBone[] ReadBones(IntPtr handle, nuint index)
    {
        nuint boneCount = 0;
        Call.Check(RadicleNative.NifShapeBoneCount(handle, index, &boneCount));
        var bones = new NifBone[checked((int)boneCount)];
        if (bones.Length == 0) return bones;

        var center = stackalloc float[3];
        var rotation = stackalloc float[9];
        var translation = stackalloc float[3];
        for (var bone = 0; bone < bones.Length; ++bone) {
            byte* name = null;
            nuint nameSize = 0;
            Call.Check(RadicleNative.NifShapeBoneName(handle, index, (nuint)bone, &name, &nameSize));
            var boneName = Encoding.Latin1.GetString(Call.TakeBytes(name, nameSize));

            float radius = 0;
            float scale = 0;
            Call.Check(RadicleNative.NifShapeBoneTransform(handle, index, (nuint)bone, center, &radius, rotation,
                translation, &scale));
            bones[bone] = new NifBone {
                Name = boneName,
                BoundsCenter = new Numerics.Vector3(center[0], center[1], center[2]),
                BoundsRadius = radius,
                Rotation = Call.Copy<float>((IntPtr)rotation, 9),
                Translation = new Numerics.Vector3(translation[0], translation[1], translation[2]),
                Scale = scale,
            };
        }
        return bones;
    }

    private static unsafe string[] ReadTextures(IntPtr handle, nuint index)
    {
        nuint textureCount = 0;
        Call.Check(RadicleNative.NifShapeTextureCount(handle, index, &textureCount));
        var textures = new string[checked((int)textureCount)];
        for (var slot = 0; slot < textures.Length; ++slot) {
            byte* text = null;
            nuint size = 0;
            Call.Check(RadicleNative.NifShapeTexture(handle, index, (nuint)slot, &text, &size));
            textures[slot] = Encoding.Latin1.GetString(Call.TakeBytes(text, size));
        }
        return textures;
    }

    private static unsafe NifShape Snapshot(IntPtr handle, NifNode[] nodes, nuint index)
    {
        int skin = 0;
        int shader = 0;
        int alpha = 0;
        Call.Check(RadicleNative.NifShapeRefs(handle, index, &skin, &shader, &alpha));

        int parentNode = -1;
        Call.Check(RadicleNative.NifShapeParentNode(handle, index, &parentNode));

        var translation = stackalloc float[3];
        var rotation = stackalloc float[9];
        float scale = 0;
        Call.Check(RadicleNative.NifShapeTransform(handle, index, translation, rotation, &scale));

        var shape = new NifShape {
            Name = ReadText(handle, index, RadicleNative.NifShapeName),
            Positions = ToVectors(ReadFloats(handle, index, RadicleNative.NifShapePositions)),
            Normals = ToVectors(ReadFloats(handle, index, RadicleNative.NifShapeNormals)),
            Tangents = ToVectors(ReadFloats(handle, index, RadicleNative.NifShapeTangents)),
            Bitangents = ToVectors(ReadFloats(handle, index, RadicleNative.NifShapeBitangents)),
            Uvs = ToTexCoords(ReadFloats(handle, index, RadicleNative.NifShapeUvs)),
            Colors = ToColors(ReadFloats(handle, index, RadicleNative.NifShapeColors)),
            Triangles = ReadUshorts(handle, index, RadicleNative.NifShapeTriangles),
            Material = ReadText(handle, index, RadicleNative.NifShapeMaterial),
            Textures = ReadTextures(handle, index),
            Weights = ReadFloats(handle, index, RadicleNative.NifShapeWeights),
            BoneIndices = ReadBytes(handle, index, RadicleNative.NifShapeBoneIndices),
            Bones = ReadBones(handle, index),
            ParentNodeIndex = parentNode,
            Warning = ReadText(handle, index, RadicleNative.NifShapeWarning),
            SkinReference = skin,
            ShaderReference = shader,
            AlphaReference = alpha,
            GlobalTranslation = new Numerics.Vector3(translation[0], translation[1], translation[2]),
            GlobalRotation = Call.Copy<float>((IntPtr)rotation, 9),
            GlobalScale = scale,
        };
        if (parentNode >= 0) shape.ParentNode = nodes[parentNode];
        return shape;
    }
}
