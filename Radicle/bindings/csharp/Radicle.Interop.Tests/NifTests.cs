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

    [Fact]
    public void DecodesCollisionBlobsWithTheirBlockTypeOwnerBodyAndTarget()
    {
        var nif = NifFile.FromBytes(NifFixtures.CollisionNif());
        var blob = Assert.Single(nif.Collisions);
        Assert.Equal(0, blob.Block);
        Assert.Equal("bhkPhysicsSystem", blob.Type);
        Assert.Equal(new byte[] { 0x57, 0xE0, 0xE0, 0x57, 0x10, 0xC0, 0xC0, 0x10 }, blob.Bytes);
        Assert.Equal(1, blob.OwnerBlock);
        Assert.Equal((ushort)0x0003, blob.Flags);
        Assert.Equal(0, blob.SystemRef);
        Assert.Equal(7U, blob.BodyId);
        Assert.Equal(2, blob.TargetBlock);
        Assert.Equal(0, blob.TargetNodeIndex);
        Assert.Equal("collision", blob.TargetNode!.Name);
    }

    [Fact]
    public void ReadsEachCollisionOwnersNodeChainWithoutTheRootInEngineAxesAndMetres()
    {
        var nif = NifFile.FromBytes(NifFixtures.OwnedChainCollisionNif());
        var blob = Assert.Single(nif.Collisions);
        Assert.Equal(2, blob.Owners.Count);

        var moved = blob.Owners[0].Transform;
        Assert.Equal(1f, moved.Translation.X, 4);
        Assert.Equal(3f, moved.Translation.Y, 4);
        Assert.Equal(-2f, moved.Translation.Z, 4);
        var expected = new float[] { 0, 0, 1, 0, 1, 0, -1, 0, 0 };
        for (var i = 0; i < expected.Length; ++i) Assert.Equal(expected[i], moved.Rotation[i], 5);
        Assert.Equal(2f, moved.Scale, 5);

        var atRoot = blob.Owners[1].Transform;
        Assert.Equal(new Radicle.Interop.Vector3(0, 0, 0), atRoot.Translation);
        Assert.Equal(new float[] { 1, 0, 0, 0, 1, 0, 0, 0, 1 }, atRoot.Rotation);
        Assert.Equal(1f, atRoot.Scale);
    }

    [Fact]
    public void KeepsASystemWithoutAnObjectUnattributed()
    {
        var nif = NifFile.FromBytes(NifFixtures.UnownedCollisionNif());
        var blob = Assert.Single(nif.Collisions);
        Assert.Equal(-1, blob.OwnerBlock);
        Assert.Equal((ushort)0, blob.Flags);
        Assert.Equal(-1, blob.SystemRef);
        Assert.Equal(0U, blob.BodyId);
        Assert.Equal(-1, blob.TargetBlock);
        Assert.Equal(-1, blob.TargetNodeIndex);
        Assert.Null(blob.TargetNode);
    }

    [Fact]
    public void ReadsTheSegmentTableWithSubSegmentsStartsUserDataCutOffsetsAndSsfPath()
    {
        var nif = NifFile.FromBytes(NifFixtures.SegmentedNif());
        Assert.Equal(new[] { 1, 2 }, new[] { nif.Shapes[0].Block, nif.Shapes[1].Block });

        var table = nif.ReadSegments(nif.Shapes[0].Block)!;
        Assert.Equal(8U, table.Primitives);
        Assert.Equal(6U, table.TotalSegments);
        Assert.Equal(2, table.Segments.Length);
        Assert.Equal(new uint[] { 0, 4, 9 }, table.SegmentStarts);
        Assert.Equal("Data\\Fx.ssf", table.SsfFile);

        var first = table.Segments[0];
        Assert.Equal((0U, 4U, 0U), (first.StartIndex, first.Primitives, first.ParentArrayIndex));
        Assert.Equal(2, first.SubSegments.Length);
        Assert.Equal((0U, 1U, 0U, 0xDEADBEEFU),
            (first.SubSegments[0].StartIndex, first.SubSegments[0].Primitives, first.SubSegments[0].ParentArrayIndex,
                first.SubSegments[0].Unused));
        Assert.Equal((1U, 3U, 2U, 0U),
            (first.SubSegments[1].StartIndex, first.SubSegments[1].Primitives, first.SubSegments[1].ParentArrayIndex,
                first.SubSegments[1].Unused));
        var second = table.Segments[1];
        Assert.Equal((4U, 2U, 1U), (second.StartIndex, second.Primitives, second.ParentArrayIndex));
        Assert.Equal(4U, Assert.Single(second.SubSegments).StartIndex);

        Assert.Equal(5, table.UserData.Length);
        Assert.Equal((7U, 9U), (table.UserData[0].UserIndex, table.UserData[0].BoneId));
        Assert.Equal(new[] { 0.5f, 1.5f }, table.UserData[0].CutOffsets);
        Assert.Empty(table.UserData[1].CutOffsets);
        Assert.Equal((11U, 12U), (table.UserData[4].UserIndex, table.UserData[4].BoneId));
        Assert.Equal(new[] { 2.5f }, table.UserData[4].CutOffsets);
    }

    [Fact]
    public void AnswersNullForAShapeWithoutSegmentsAndRejectsBadBlocks()
    {
        var nif = NifFile.FromBytes(NifFixtures.SegmentedNif());
        Assert.Null(nif.ReadSegments(0));
        Assert.Null(nif.ReadSegments(2));
        var error = Assert.Throws<RadicleException>(() => nif.ReadSegments(3));
        Assert.Equal(Native.Status.InvalidArgument, error.Status);
        Assert.Throws<ArgumentOutOfRangeException>(() => nif.ReadSegments(-1));
    }

    [Fact]
    public void ReadsEveryLightingShaderFieldAndLeavesOtherTypeGroupsNull()
    {
        var nif = NifFile.FromBytes(NifFixtures.DistinctLightingShaderNif());
        var shader = nif.ReadLightingShader(0);
        Assert.Equal(0, shader.Block);
        Assert.Equal("fx", shader.Name);
        Assert.Equal(5U, shader.ShaderType);
        Assert.Equal((0x11U, 0x22U), (shader.Flags1, shader.Flags2));
        Assert.Equal(new Vector2(0.25f, 0.5f), shader.UvOffset);
        Assert.Equal(new Vector2(2, 3), shader.UvScale);
        Assert.Equal(-1, shader.TextureSetRef);
        Assert.Equal(0, shader.TextureCount);
        Assert.Equal(new Vector3(0.5f, 0.25f, 0.125f), shader.EmissiveColor);
        Assert.Equal(4.0f, shader.EmissiveMultiple);
        Assert.True(shader.HasRootMaterial);
        Assert.Equal("materials\\root.bgsm", shader.RootMaterial);
        Assert.Equal(3U, shader.TextureClamp);
        Assert.Equal((0.5f, 0.25f, 0.75f), (shader.Alpha, shader.RefractionStrength, shader.Smoothness));
        Assert.Equal(new Vector3(0.1f, 0.2f, 0.3f), shader.SpecularColor);
        Assert.Equal((0.875f, 0.125f), (shader.SpecularStrength, shader.SubsurfaceRolloff));
        Assert.Equal(float.MaxValue, shader.RimlightPower);
        Assert.Equal(2.5f, shader.Backlight);
        Assert.Equal(0.625f, shader.GrayscaleToPaletteScale);
        Assert.Equal(7.5f, shader.FresnelPower);
        Assert.Equal(new[] { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f }, shader.Wetness);
        Assert.Equal(new[] { 0.1f, 0.2f, 0.3f, 0.4f }, shader.SkinTint);
        Assert.Equal(16, shader.TailBytes);
        Assert.Null(shader.HairTint);
        Assert.Null(shader.Snow);
        Assert.Null(shader.ParallaxOcc);
        Assert.Null(shader.MultiLayerParallax);
        Assert.False(shader.HasEnvMap);
        Assert.False(shader.HasEye);
    }

    [Fact]
    public void ReadsTheLightingShaderAShapeReferencesAndRejectsOtherBlockTypes()
    {
        var nif = NifFile.FromBytes(NifFixtures.FullNif());
        var shape = Assert.Single(nif.Shapes);
        var shader = nif.ReadLightingShader(shape.ShaderReference);
        Assert.Equal(shape.ShaderReference, shader.Block);
        Assert.Equal("materials\\body.bgsm", shader.Name);
        Assert.Equal(shape.Material, shader.Name);
        Assert.Equal(2, shader.TextureCount);
        Assert.True(shader.HasEnvMap);
        Assert.Equal(1.0f, shader.EnvMapScale);
        Assert.Equal(0.0f, shader.Backlight);
        var error = Assert.Throws<RadicleException>(() => nif.ReadLightingShader(0));
        Assert.Equal(Native.Status.InvalidArgument, error.Status);
    }

    [Fact]
    public void ShaderFlagNamesHoldTheNativeTableInBitOrder()
    {
        var names = NifFile.ShaderFlagNames;
        Assert.Equal(64, names.Count);
        Assert.Equal("Specular", names[0]);
        Assert.Equal("Refraction_Writes_Depth", names[63]);
    }

    [Fact]
    public void ReadsLightDimmerColoursAndPointAttenuationInFileOrder()
    {
        var nif = NifFile.FromBytes(NifFixtures.LightNif());
        Assert.Equal(2, nif.Lights.Count);
        var point = nif.Lights[0];
        Assert.Equal("NiPointLight", point.Type);
        Assert.Equal(4.5f, point.Dimmer);
        Assert.Equal(new Vector3(0.1f, 0.2f, 0.3f), point.AmbientColor);
        Assert.Equal(new Vector3(0.4f, 0.5f, 0.6f), point.DiffuseColor);
        Assert.Equal(new Vector3(70, 80, 90), point.SpecularOrRadius);
        Assert.Equal(new Vector3(0.25f, 1.5f, 2.5f), point.Attenuation);
        var sun = nif.Lights[1];
        Assert.Equal(0.75f, sun.Dimmer);
        Assert.Equal(new Vector3(7, 8, 9), sun.SpecularOrRadius);
        Assert.Null(sun.Attenuation);
    }

    [Fact]
    public void ReadsTheAlphaPropertyBitFieldsAndTheNativeShapeCounts()
    {
        var nif = NifFile.FromBytes(NifFixtures.MeshMaterialNif(0x20D, 77));
        var shape = Assert.Single(nif.Shapes);
        Assert.Equal(3, shape.NativeVertexCount);
        Assert.Equal(shape.VertexCount, shape.NativeVertexCount);
        Assert.Empty(nif.Lights);
        var alpha = nif.ReadAlphaProperty(shape.AlphaReference);
        Assert.Equal(shape.AlphaReference, alpha.Block);
        Assert.Equal((ushort)0x20D, alpha.Flags);
        Assert.Equal((byte)77, alpha.Threshold);
        Assert.True(alpha.BlendEnabled);
        Assert.Equal((byte)6, alpha.SourceBlend);
        Assert.Equal((byte)0, alpha.DestinationBlend);
        Assert.True(alpha.TestEnabled);
        Assert.Equal((byte)0, alpha.TestFunction);
        Assert.Equal(Native.Status.InvalidArgument, Assert.Throws<RadicleException>(() => nif.ReadAlphaProperty(0)).Status);
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
