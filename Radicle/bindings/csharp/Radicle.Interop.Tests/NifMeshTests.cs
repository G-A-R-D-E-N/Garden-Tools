using System;
using System.Numerics;
using System.Runtime.InteropServices;
using Radicle.Interop.Native;
using Xunit;

namespace Radicle.Interop.Tests;

using Vector3 = System.Numerics.Vector3;

public class NifMeshTests
{
    private const float Units = 10f;
    private const ushort AdditiveBlendWithTest = 0x20D;

    private static string? LastError() => Marshal.PtrToStringUTF8(RadicleNative.LastError());

    [Fact]
    public void EmptyBytesAreRefusedBeforeCallingNative()
    {
        Assert.Throws<ArgumentException>(() => NifMesh.Read(ReadOnlySpan<byte>.Empty));
    }

    [Fact]
    public void BytesThatAreNotANifSurfaceAsAParseError()
    {
        Assert.Equal(Status.ParseError, Assert.Throws<RadicleException>(() => NifMesh.Read(new byte[64])).Status);
    }

    [Theory]
    [InlineData(-1f)]
    [InlineData(float.NaN)]
    [InlineData(float.PositiveInfinity)]
    public void AUnitScaleThatIsNotZeroOrFinitePositiveIsAnInvalidArgument(float units)
    {
        var error = Assert.Throws<RadicleException>(() => NifMesh.Read(NifFixtures.MeshMaterialNif(AdditiveBlendWithTest, 77), units));
        Assert.Equal(Status.InvalidArgument, error.Status);
    }

    [Fact]
    public unsafe void NullHandlesAndMissingOutputsAreInvalidArguments()
    {
        Mesh* mesh = (Mesh*)new IntPtr(1);
        Assert.Equal(Status.InvalidArgument, RadicleNative.NifMeshRead(IntPtr.Zero, null, &mesh));
        Assert.True(mesh == null);
        Assert.Equal(Status.InvalidArgument, RadicleNative.NifMeshRead(IntPtr.Zero, null, null));
        Assert.Contains("required", LastError());
        RadicleNative.NifMeshFree(null);
    }

    [Fact]
    public void AuditIsOffByDefaultAndReportsAFileWithTwoRootsWhenAsked()
    {
        var twoRoots = NifFixtures.MeshMaterialNif(AdditiveBlendWithTest, 77, secondRoot: true);
        Assert.Empty(NifMesh.Read(twoRoots, Units).AuditFindings);
        var audited = NifMesh.Read(twoRoots, Units, audit: true);
        var finding = Assert.Single(audited.AuditFindings, f => f.Rule == "nif_root_count");
        Assert.Equal("error", finding.Severity);
        Assert.Equal(-1, finding.Block);
        Assert.Single(audited.Parts);
    }

    [Fact]
    public void AFailedAuditIsOneSyntheticErrorFindingNotAnEmptyList()
    {
        var finding = Assert.Single(NifMesh.ReadAudit(IntPtr.Zero));
        Assert.Equal("error", finding.Severity);
        Assert.Equal("nif_audit_failed", finding.Rule);
        Assert.Equal(-1, finding.Block);
        Assert.Contains("required", finding.Message);
    }

    [Fact]
    public void ARigidShapeReadsInEngineAxesAndMetresWithEveryAttributeCopied()
    {
        var mesh = NifMesh.Read(NifFixtures.MeshMaterialNif(AdditiveBlendWithTest, 77), Units);
        var part = Assert.Single(mesh.Parts);
        Assert.Equal("fx", part.Name);
        Assert.Equal(3, part.VertexCount);
        Assert.Equal(new[] { 0.1f, 0.3f, -0.2f, 0.4f, 0.6f, -0.5f, 0.7f, 0.9f, -0.8f }, part.Positions, new FloatComparer());
        Assert.Equal(new uint[] { 0, 1, 2 }, part.Indices);
        Assert.Equal(9, part.Normals!.Length);
        Assert.Equal(12, part.Tangents!.Length);
        Assert.Equal(6, part.Uv0!.Length);
        Assert.Equal(12, part.Colors!.Length);
        Assert.Null(part.Uv1);
        Assert.Null(part.Skin);
        Assert.Equal(0, part.Material);
        Assert.True(Vector3.Distance(new Vector3(0.1f, 0.3f, -0.8f), part.BoundsMin) < 1e-5f);
        Assert.True(Vector3.Distance(new Vector3(0.7f, 0.9f, -0.2f), part.BoundsMax) < 1e-5f);
        Assert.Equal(string.Empty, part.Warning);
    }

    [Fact]
    public void ASegmentedShapeWhoseSubSegmentSlotsAreAllGoreCapSlotsIsFlaggedAsADismemberCap()
    {
        var part = Assert.Single(NifMesh.Read(NifFixtures.SegmentedMeshNif(new[] { 0, 2 }, new uint[] { 0, 0, 101, 100 })).Parts);
        Assert.True(part.DismemberCap);
    }

    [Fact]
    public void ASegmentedShapeWithABodySlotOrAMissingSlotRecordOrNoSegmentsIsNotACap()
    {
        Assert.False(Assert.Single(NifMesh.Read(NifFixtures.SegmentedMeshNif(new[] { 2 }, new uint[] { 0, 32, 33 })).Parts).DismemberCap);
        Assert.False(Assert.Single(NifMesh.Read(NifFixtures.SegmentedMeshNif(new[] { 2 }, new uint[] { 0, 100 })).Parts).DismemberCap);
        Assert.False(Assert.Single(NifMesh.Read(NifFixtures.SegmentedMeshNif(new[] { 0 }, new uint[] { 100 })).Parts).DismemberCap);
        Assert.False(Assert.Single(NifMesh.Read(NifFixtures.MeshMaterialNif(AdditiveBlendWithTest, 77), Units).Parts).DismemberCap);
    }

    [Theory]
    [InlineData(new[] { 1 }, new uint[] { 0, 99 }, false)]
    [InlineData(new[] { 1 }, new uint[] { 0, 102 }, true)]
    [InlineData(new[] { 1 }, new uint[] { 0, 103 }, false)]
    [InlineData(new[] { 1, 1 }, new uint[] { 0, 30, 0, 100 }, false)]
    [InlineData(new[] { 0, 0 }, new uint[] { 0, 0 }, false)]
    public void TheCapRangeIsTheThreeSlotsFromOneHundredAndEverySubSegmentMustBeInIt(int[] subCounts, uint[] slots, bool expected)
    {
        Assert.Equal(expected, NifDismemberCap.IsCap(subCounts, slots));
    }

    [Fact]
    public void TheWindingFlagReversesEveryTriangle()
    {
        var mesh = NifMesh.Read(NifFixtures.MeshMaterialNif(AdditiveBlendWithTest, 77), Units, flipWinding: true);
        Assert.Equal(new uint[] { 0, 2, 1 }, Assert.Single(mesh.Parts).Indices);
    }

    [Fact]
    public void ALightingMaterialCarriesTheRawShaderBlockValues()
    {
        var mesh = NifMesh.Read(NifFixtures.MeshMaterialNif(AdditiveBlendWithTest, 77), Units);
        var material = Assert.Single(mesh.Materials);
        Assert.Equal(NifMeshMaterialKind.Lighting, material.Kind);
        Assert.Equal("fx", material.Key);
        Assert.Equal("materials\\root.bgsm", material.RootMaterial);
        Assert.Equal(5U, material.ShaderType);
        Assert.Equal((0x11U, 0x22U), (material.ShaderFlags1, material.ShaderFlags2));
        Assert.Equal(3U, material.TextureClamp);
        Assert.True(material.Flags.Specular);
        Assert.True(material.Flags.PaletteColor);
        Assert.False(material.Flags.Hair);
        Assert.Equal(new Vector3(0.5f, 0.25f, 0.125f), material.EmissiveColor);
        Assert.Equal(4.0f, material.EmissiveMultiple);
        Assert.False(material.HasEnvMap);
        Assert.Equal(new Vector3(0.1f, 0.2f, 0.3f), material.SkinTint);
        Assert.Equal(new Vector2(0.25f, 0.5f), material.UvOffset);
        Assert.Equal(new Vector2(2, 3), material.UvScale);

        var lighting = material.Lighting;
        Assert.Equal(0.75f, lighting.Glossiness);
        Assert.Equal(new Vector3(0.1f, 0.2f, 0.3f), lighting.SpecularColor);
        Assert.Equal(0.875f, lighting.SpecularStrength);
        Assert.Equal(0.5f, lighting.Alpha);
        Assert.Equal(7.5f, lighting.FresnelPower);
        Assert.Equal(0.125f, lighting.SubsurfaceRolloff);
        Assert.Equal(float.MaxValue, lighting.RimlightPower);
        Assert.True(lighting.HasBacklight);
        Assert.Equal(2.5f, lighting.BacklightPower);
        Assert.True(lighting.HasGrayscaleToPaletteScale);
        Assert.Equal(0.625f, lighting.GrayscaleToPaletteScale);
        Assert.Equal(default(NifMeshEffectValues), material.Effect);
    }

    [Fact]
    public void TheAlphaPropertyKeepsItsBlendFactorsAndThresholdForABlendedShape()
    {
        var mesh = NifMesh.Read(NifFixtures.MeshMaterialNif(AdditiveBlendWithTest, 77), Units);
        var material = Assert.Single(mesh.Materials);
        Assert.Equal(NifMeshAlphaMode.Blend, material.AlphaMode);
        Assert.Equal(new NifMeshAlphaProperty(true, 6, 0, 77), material.AlphaProperty);
    }

    [Fact]
    public void ATestOnlyAlphaPropertyIsAMaskWithTheCutoffOverTwoFiftyFive()
    {
        var mesh = NifMesh.Read(NifFixtures.MeshMaterialNif(0x200, 128), Units);
        var material = Assert.Single(mesh.Materials);
        Assert.Equal(NifMeshAlphaMode.Mask, material.AlphaMode);
        Assert.Equal(128f / 255f, material.AlphaCutoff, 1e-5f);
        Assert.Equal(new NifMeshAlphaProperty(true, 0, 0, 128), material.AlphaProperty);
    }

    [Fact]
    public void ANifWithoutShadersHasNoMaterialRows()
    {
        var mesh = NifMesh.Read(NifFixtures.NodeChainNif(), Units);
        Assert.Empty(mesh.Materials);
    }

    private sealed class FloatComparer : System.Collections.Generic.IEqualityComparer<float>
    {
        public bool Equals(float x, float y) => Math.Abs(x - y) < 1e-5f;

        public int GetHashCode(float value) => 0;
    }
}
