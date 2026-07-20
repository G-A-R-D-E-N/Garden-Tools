using System.IO;
using FluentAssertions;
using FO4RecordEditor.Services;
using FO4RecordEditor.Services.Materials;
using Xunit;

namespace FO4RecordEditor.Tests;

public class MaterialServiceTests
{
    private static string WriteMinimalBgsm()
    {
        var d = new BgsmData
        {
            Header = new MaterialHeader { Signature = BgsmCodec.Signature, Version = 2, UScale = 1, VScale = 1, Alpha = 1 },
            DiffuseTexture = "tex\\d.dds\0", NormalTexture = "tex\\n.dds\0",
            SmoothSpecTexture = "\0", GreyscaleTexture = "\0",
            EnvmapTexture = "\0", GlowTexture = "\0", InnerLayerTexture = "\0", WrinklesTexture = "\0", DisplacementTexture = "\0",
            RimLighting = false, RimPower = 0, BackLightPower = 0, SubsurfaceLighting = false, SubsurfaceLightingRolloff = 0,
            SpecularEnabled = true, SpecularColor = new[] { 1f, 1f, 1f }, SpecularMult = 1, Smoothness = 0.5f, FresnelPower = 1,
            WetnessControlSpecScale = 1, WetnessControlSpecPowerScale = 1, WetnessControlSpecMinvar = 0,
            WetnessControlEnvMapScale = 1, WetnessControlFresnelPower = 1, WetnessControlMetalness = 0,
            RootMaterialPath = "\0", HairTintColor = new[] { 1f, 1f, 1f },
            DisplacementTextureBias = 0, DisplacementTextureScale = 0, TessellationPnScale = 0,
            TessellationBaseFactor = 0, TessellationFadeDistance = 0, SkewSpecularAlpha = false,
            BackLighting = false, EnvironmentMappingWindow = false, EnvironmentMappingEye = false,
            ReceiveShadows = true, CastShadows = true,
        };
        var path = Path.Combine(Path.GetTempPath(), $"MatTest_{Guid.NewGuid():N}.bgsm");
        File.WriteAllBytes(path, BgsmCodec.Write(d));
        return path;
    }

    [Fact]
    public void Inspect_ListsFieldsAndOmitsUnsetOptionalOnes()
    {
        var path = WriteMinimalBgsm();
        try
        {
            var result = MaterialService.Inspect(path);
            result.Should().Contain("Smoothness = 0.5");
            result.Should().Contain("DiffuseTexture = tex\\d.dds");   // trailing \0 trimmed for display
            result.Should().NotContain("Pbr =", "version 2 doesn't carry a PBR field, it should be omitted not shown as false");
        }
        finally { try { File.Delete(path); } catch { } }
    }

    [Fact]
    public void SetField_UpdatesFloatField_AndPersists()
    {
        var path = WriteMinimalBgsm();
        try
        {
            var result = MaterialService.SetField(path, "Smoothness", "0.9", null);
            result.Should().Contain("Set Smoothness = 0.9");

            var reparsed = BgsmCodec.Parse(File.ReadAllBytes(path));
            reparsed.Smoothness.Should().Be(0.9f);
        }
        finally { try { File.Delete(path); } catch { } }
    }

    [Fact]
    public void SetField_UpdatesColorField_FromCommaSeparatedString()
    {
        var path = WriteMinimalBgsm();
        try
        {
            MaterialService.SetField(path, "SpecularColor", "0.1, 0.2, 0.3", null);
            var reparsed = BgsmCodec.Parse(File.ReadAllBytes(path));
            reparsed.SpecularColor.Should().Equal(0.1f, 0.2f, 0.3f);
        }
        finally { try { File.Delete(path); } catch { } }
    }

    [Fact]
    public void SetField_UnknownField_ReturnsHelpfulError()
    {
        var path = WriteMinimalBgsm();
        try
        {
            MaterialService.SetField(path, "NotARealField", "1", null)
                .Should().Contain("Unknown BGSM field");
        }
        finally { try { File.Delete(path); } catch { } }
    }

    [Fact]
    public void SetField_ToSeparateOutPath_LeavesOriginalUntouched()
    {
        var path = WriteMinimalBgsm();
        var outPath = Path.Combine(Path.GetTempPath(), $"MatTestOut_{Guid.NewGuid():N}.bgsm");
        try
        {
            MaterialService.SetField(path, "Smoothness", "0.1", outPath);
            BgsmCodec.Parse(File.ReadAllBytes(path)).Smoothness.Should().Be(0.5f, "original must be untouched when out_path is given");
            BgsmCodec.Parse(File.ReadAllBytes(outPath)).Smoothness.Should().Be(0.1f);
        }
        finally
        {
            try { File.Delete(path); } catch { }
            try { File.Delete(outPath); } catch { }
        }
    }

    [Fact]
    public void FileNotFound_ReturnsFriendlyError()
    {
        MaterialService.Inspect(@"C:\does\not\exist.bgsm").Should().Contain("not found");
    }
}
