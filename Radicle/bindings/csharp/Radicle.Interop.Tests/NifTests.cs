using System;
using System.Numerics;
using Radicle.Interop;
using Xunit;

namespace Radicle.Interop.Tests;

using Vector3 = System.Numerics.Vector3;

public class NifTests
{
    private static NifShape Shape()
    {
        var nif = NifFile.FromBytes(NifFixtures.FullNif());
        Assert.Single(nif.Shapes);
        return nif.Shapes[0];
    }

    [Fact]
    public void DecodesGeometryIntoManagedArrays()
    {
        var shape = Shape();
        Assert.Equal("shape", shape.Name);
        Assert.Equal(new[] { new Vector3(1, 2, 3), new Vector3(4, 5, 6) }, shape.Positions);
        Assert.Equal(new[] { new Vector2(0.5f, 0.25f), new Vector2(0.5f, 0.25f) }, shape.Uvs);
        Assert.Equal(new ushort[] { 0, 1, 1 }, shape.Triangles);
        Assert.Equal(2, shape.VertexCount);
        Assert.Equal(1, shape.TriangleCount);
        Assert.Equal(2, shape.SkinReference);
        Assert.Equal(4, shape.ShaderReference);
        Assert.Equal(-1, shape.AlphaReference);
    }

    [Fact]
    public void DecodesNormalsTangentsBitangentsAndColours()
    {
        var shape = Shape();
        var mid = 128.0f / 255.0f * 2.0f - 1.0f;
        Assert.Equal(2, shape.Normals.Length);
        Assert.Equal(new Vector3(1, -1, mid), shape.Normals[0]);
        Assert.Equal(new Vector3(-1, 1, mid), shape.Tangents[0]);
        Assert.Equal(new Vector3(0.5f, 0x11 / 255.0f * 2.0f - 1.0f, 0x22 / 255.0f * 2.0f - 1.0f), shape.Bitangents[0]);
        Assert.Equal(new Vector4(1, 0, 51.0f / 255.0f, 102.0f / 255.0f), shape.Colors[0]);
    }

    [Fact]
    public void DecodesMaterialPathAndTextureSlots()
    {
        var shape = Shape();
        Assert.Equal("materials\\body.bgsm", shape.Material);
        Assert.Equal(new[] { "textures\\body_d.dds", "textures\\body_n.dds" }, shape.Textures);
    }

    [Fact]
    public void DecodesSkinWeightsBonesAndBindTransforms()
    {
        var shape = Shape();
        Assert.True(shape.IsSkinned);
        Assert.Equal(new[] { 1.0f, 0.5f, 0.25f, 0.0f, 1.0f, 0.5f, 0.25f, 0.0f }, shape.Weights);
        Assert.Equal(new byte[] { 0, 1, 0xFE, 0xFF, 0, 1, 0xFE, 0xFF }, shape.BoneIndices);

        var bone = Assert.Single(shape.Bones);
        Assert.Equal("RootBone", bone.Name);
        Assert.Equal(new Vector3(1, 2, 3), bone.BoundsCenter);
        Assert.Equal(4.0f, bone.BoundsRadius);
        Assert.Equal(new float[] { 1, 2, 3, 4, 5, 6, 7, 8, 9 }, bone.Rotation);
        Assert.Equal(new Vector3(5, 6, 7), bone.Translation);
        Assert.Equal(2.0f, bone.Scale);

        var expected = new Matrix4x4(
            2, 4, 6, 0,
            8, 10, 12, 0,
            14, 16, 18, 0,
            5, 6, 7, 1);
        Assert.Equal(expected, bone.BindMatrix);
    }

    [Fact]
    public void DecodesTheComposedGlobalTransform()
    {
        var shape = Shape();
        Assert.Equal(new Vector3(7, 8, 9), shape.GlobalTranslation);
        Assert.Equal(new float[] { 1, 0, 0, 0, 1, 0, 0, 0, 1 }, shape.GlobalRotation);
        Assert.Equal(1.0f, shape.GlobalScale);
    }

    private static void ReadEmpty() => NifFile.FromBytes(ReadOnlySpan<byte>.Empty);

    [Fact]
    public void RejectsEmptyAndMalformedInput()
    {
        Assert.Throws<ArgumentException>(ReadEmpty);
        var junk = new byte[64];
        var error = Assert.Throws<RadicleException>(() => NifFile.FromBytes(junk));
        Assert.Equal(Native.Status.ParseError, error.Status);
    }
}
