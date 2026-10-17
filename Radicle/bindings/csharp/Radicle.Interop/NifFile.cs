using System;
using System.Collections.Generic;
using System.Text;
using Radicle.Interop.Native;

namespace Radicle.Interop;

// Radicle.Interop declares its own Vector3 record for record payloads, so the geometry
// types are reached through a namespace alias. They are System.Numerics types, which
// is what the BGS NifShape uses.
using Numerics = System.Numerics;

/// <summary>A bone of a skinned shape: its name and its bind transform.</summary>
public sealed class NifBone
{
    /// <summary>The referenced node's name, empty when the reference does not resolve.</summary>
    public required string Name { get; init; }

    public required Numerics.Vector3 BoundsCenter { get; init; }

    public required float BoundsRadius { get; init; }

    /// <summary>The nine rotation floats in the order the file stores them.</summary>
    public required float[] Rotation { get; init; }

    public required Numerics.Vector3 Translation { get; init; }

    /// <summary>Multiplies the rotation, not the translation.</summary>
    public required float Scale { get; init; }

    /// <summary>
    /// The skin-to-bone matrix: the stored rotation rows scaled by <see cref="Scale"/>,
    /// with <see cref="Translation"/> in the fourth row. It is the matrix BGS's
    /// NifGeometry.ReadSkin builds from the same fields.
    /// </summary>
    public Numerics.Matrix4x4 BindMatrix => new(
        Rotation[0] * Scale, Rotation[1] * Scale, Rotation[2] * Scale, 0,
        Rotation[3] * Scale, Rotation[4] * Scale, Rotation[5] * Scale, 0,
        Rotation[6] * Scale, Rotation[7] * Scale, Rotation[8] * Scale, 0,
        Translation.X, Translation.Y, Translation.Z, 1);
}

/// <summary>
/// One decoded shape. Positions, normals, tangents, bitangents and colours are
/// per-vertex; UVs are two floats per vertex; weights are four floats per vertex
/// and bone indices four bytes per vertex.
/// </summary>
public sealed class NifShape
{
    public required string Name { get; init; }

    public required Numerics.Vector3[] Positions { get; init; }

    public required Numerics.Vector3[] Normals { get; init; }

    public required Numerics.Vector3[] Tangents { get; init; }

    /// <summary>Empty unless the shape stores both normals and tangents.</summary>
    public required Numerics.Vector3[] Bitangents { get; init; }

    public required Numerics.Vector2[] Uvs { get; init; }

    public required Numerics.Vector4[] Colors { get; init; }

    public required ushort[] Triangles { get; init; }

    /// <summary>The shader's material file path: a BGSM or BGEM, or empty.</summary>
    public required string Material { get; init; }

    /// <summary>Texture slots in BSShaderTextureSet order: 0 diffuse, 1 normal, 2 glow and so on.</summary>
    public required string[] Textures { get; init; }

    /// <summary>Four floats per vertex; empty when the shape is not skinned.</summary>
    public required float[] Weights { get; init; }

    /// <summary>Four bone indices per vertex, indexing <see cref="Bones"/>.</summary>
    public required byte[] BoneIndices { get; init; }

    /// <summary>The bones of the skin instance, in the order the file stores them.</summary>
    public required NifBone[] Bones { get; init; }

    public required int SkinReference { get; init; }

    public required int ShaderReference { get; init; }

    public required int AlphaReference { get; init; }

    /// <summary>The composed shape-to-global translation, with every ancestor applied.</summary>
    public required Numerics.Vector3 GlobalTranslation { get; init; }

    /// <summary>The composed shape-to-global rotation, nine floats in the order the file stores them.</summary>
    public required float[] GlobalRotation { get; init; }

    public required float GlobalScale { get; init; }

    public int VertexCount => Positions.Length;

    public int TriangleCount => Triangles.Length / 3;

    public bool IsSkinned => Weights.Length == Positions.Length * 4 && Bones.Length > 0;
}

/// <summary>
/// A read-only NIF. Every shape is decoded into managed arrays while the native
/// handle is open, so the handle is released before <see cref="FromBytes"/> returns.
/// </summary>
public sealed class NifFile
{
    private NifFile(NifShape[] shapes) => Shapes = shapes;

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
            nuint count = 0;
            Call.Check(RadicleNative.NifShapeCount(handle, &count));
            var shapes = new NifShape[checked((int)count)];
            for (var index = 0; index < shapes.Length; ++index) shapes[index] = Snapshot(handle, (nuint)index);
            return new NifFile(shapes);
        }
        finally
        {
            RadicleNative.NifDestroy(handle);
        }
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

    private static unsafe NifShape Snapshot(IntPtr handle, nuint index)
    {
        int skin = 0;
        int shader = 0;
        int alpha = 0;
        Call.Check(RadicleNative.NifShapeRefs(handle, index, &skin, &shader, &alpha));

        var translation = stackalloc float[3];
        var rotation = stackalloc float[9];
        float scale = 0;
        Call.Check(RadicleNative.NifShapeTransform(handle, index, translation, rotation, &scale));

        return new NifShape {
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
            SkinReference = skin,
            ShaderReference = shader,
            AlphaReference = alpha,
            GlobalTranslation = new Numerics.Vector3(translation[0], translation[1], translation[2]),
            GlobalRotation = Call.Copy<float>((IntPtr)rotation, 9),
            GlobalScale = scale,
        };
    }
}
