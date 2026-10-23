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
        Assert.Equal(string.Empty, shape.Warning);
    }

    [Fact]
    public void DecodesSkinWeightsBonesAndBindTransforms()
    {
        var shape = Shape();
        Assert.True(shape.IsSkinned);
        Assert.Equal(new[] { 1.0f, 0.5f, 0.25f, 0.0f, 1.0f, 0.5f, 0.25f, 0.0f }, shape.Weights);
        Assert.Equal(new byte[] { 0, 0, 0, 0, 0, 0, 0, 0 }, shape.BoneIndices);

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

    [Fact]
    public void ExposesTheNodeTree()
    {
        var nif = NifFile.FromBytes(NifFixtures.NodeChainNif());
        Assert.Equal(2, nif.Nodes.Count);

        var root = nif.Nodes[0];
        Assert.Equal(0, root.Block);
        Assert.Equal("BSFadeNode", root.Type);
        Assert.Equal("Root", root.Name);
        Assert.True(root.IsRoot);
        Assert.Null(root.Parent);
        Assert.Equal(new[] { 1 }, root.ChildBlocks);
        Assert.Equal(new Vector3(1, 2, 3), root.Translation);
        Assert.Equal(2.0f, root.Scale);

        var chest = nif.Nodes[1];
        Assert.Equal("NiNode", chest.Type);
        Assert.Equal("Chest", chest.Name);
        Assert.Same(root, chest.Parent);
        Assert.Equal(0, chest.ParentIndex);
        Assert.Same(chest, Assert.Single(root.Children));
        Assert.Equal(3.0f, chest.Scale);

        Assert.Equal(new[] { 2 }, chest.ChildBlocks);
        Assert.Null(Assert.Single(chest.Children));

        var shape = Assert.Single(nif.Shapes);
        Assert.Equal(1, shape.ParentNodeIndex);
        Assert.Same(chest, shape.ParentNode);
        Assert.Same(root, shape.ParentNode!.Parent);
    }

    [Fact]
    public void ComposesTheNodeWorldMatrix()
    {
        var nif = NifFile.FromBytes(NifFixtures.NodeChainNif());
        var chest = nif.Nodes[1];
        var world = chest.WorldMatrix;
        Assert.Equal(-9.0f, Vector3.Transform(Vector3.Zero, world).X, 4);
        Assert.Equal(10.0f, Vector3.Transform(Vector3.Zero, world).Y, 4);
        Assert.Equal(15.0f, Vector3.Transform(Vector3.Zero, world).Z, 4);
        Assert.Equal(6.0f, Vector3.TransformNormal(Vector3.UnitX, world).Length(), 4);
    }

    [Fact]
    public void ComposesAVeryDeepChainWithoutRecursion()
    {
        var nif = NifFile.FromBytes(NifFixtures.DeepChainNif(10000));
        var world = nif.Nodes[^1].WorldMatrix;
        Assert.Equal(10000.0f, Vector3.Transform(Vector3.Zero, world).X, 1);
    }

    [Fact]
    public void RejectsANodeThatIsItsOwnParent()
    {
        var error = Assert.Throws<RadicleException>(() => NifFile.FromBytes(NifFixtures.SelfParentNif()));
        Assert.Contains("cycle", error.Message);
    }
    [Fact]
    public void ReportsABadShaderBlockAsAWarningAndKeepsTheGeometry()
    {
        var nif = NifFile.FromBytes(NifFixtures.BadShaderNif());
        var shape = Assert.Single(nif.Shapes);
        Assert.Equal(string.Empty, shape.Material);
        Assert.Empty(shape.Textures);
        Assert.Contains("texture set reference is out of range", shape.Warning);
        Assert.Single(shape.Positions);
        Assert.Equal(new ushort[] { 0, 0, 0 }, shape.Triangles);
        Assert.Equal(0, shape.ParentNodeIndex);
        Assert.Equal("Root", shape.ParentNode!.Name);
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
