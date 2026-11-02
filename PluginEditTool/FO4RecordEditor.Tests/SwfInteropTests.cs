using System;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Threading.Tasks;
using FO4RecordEditor.Core.Tests;
using FO4RecordEditor.Services;
using Xunit;

namespace FO4RecordEditor.Tests;

public sealed class SwfInteropTests
{
    [NativeFact]
    public async Task AbcRpcsReturnTheFieldsThePanelReadsAndTheRefusalShape()
    {
        // Invariant: the ABC RPC field names the web panel reads must not change; detects a renamed field or a dropped error shape.
        await WithOpenMovie(SwfFixture.Movie(withAbc: true), async swf =>
        {
            Assert.False(Parse(await swf.Open("Interface\\Test.swf")).TryGetProperty("error", out _));

            var block = Parse(await swf.AbcBlocks()).GetProperty("blocks")[0];
            Assert.Equal(0, block.GetProperty("abc").GetInt32());
            Assert.Equal(JsonValueKind.String, block.GetProperty("name").ValueKind);
            Assert.Equal(JsonValueKind.Array, block.GetProperty("classes").ValueKind);
            Assert.Equal(JsonValueKind.Array, block.GetProperty("scripts").ValueKind);
            var method = block.GetProperty("methods").EnumerateArray()
                .First(item => item.GetProperty("hasBody").GetBoolean());
            Assert.Equal(JsonValueKind.String, method.GetProperty("name").ValueKind);
            Assert.Equal(JsonValueKind.String, method.GetProperty("returnType").ValueKind);

            var listing = Parse(await swf.Disassemble(0, method.GetProperty("index").GetInt32()));
            Assert.False(string.IsNullOrWhiteSpace(listing.GetProperty("text").GetString()));

            var refusal = Parse(await swf.Disassemble(0, 9999));
            Assert.False(string.IsNullOrEmpty(refusal.GetProperty("error").GetString()));
            Assert.True(refusal.TryGetProperty("status", out _));

            Assert.True(Parse(await swf.Open("Interface\\Missing.swf")).TryGetProperty("error", out _));
            Assert.Equal("no SWF document is open", Parse(await swf.AbcBlocks()).GetProperty("error").GetString());
        });
    }

    [NativeFact]
    public async Task FrameLabelRpcsTakeATimelineAndLabelIndexAndRefuseOutOfRange()
    {
        // Constraint: detects the label RPCs reverting to tag indices, dropping the timeline, a timeline index that does not resolve to its DefineSprite character id, or reporting a refused index as a change.
        await WithOpenMovie(SwfFixture.Movie(), async swf =>
        {
            Assert.False(Parse(await swf.Open("Interface\\Test.swf")).TryGetProperty("error", out _));
            Assert.Equal(14, Parse(await swf.Timeline(1)).GetProperty("characterId").GetInt32());
            Assert.Equal(0, Parse(await swf.Timeline(0)).GetProperty("characterId").GetInt32());

            var labels = Parse(await swf.FrameLabels(0)).GetProperty("labels");
            Assert.Equal(0, labels[0].GetProperty("labelIndex").GetInt32());
            Assert.Equal("start", labels[0].GetProperty("name").GetString());

            Assert.True(Parse(await swf.RenameLabel(0, 0, "menu")).GetProperty("changed").GetBoolean());
            var refusal = Parse(await swf.RenameLabel(0, 9, "menu"));
            Assert.Contains("out of range", refusal.GetProperty("error").GetString());
            Assert.True(refusal.TryGetProperty("status", out _));

            Assert.True(Parse(await swf.RemoveLabel(0, 0)).GetProperty("changed").GetBoolean());
            Assert.Equal(0, Parse(await swf.FrameLabels(0)).GetProperty("labels").GetArrayLength());
            Assert.Equal(2, Parse(await swf.State()).GetProperty("undo").GetInt32());
        });
    }

    [NativeFact]
    public async Task PlacementMatrixRpcReportsIdentityScaleForAnAbsentScale()
    {
        // Constraint: detects the matrix RPC reporting an absent scale as 0, which the panel sends back on apply and collapses the clip.
        await WithOpenMovie(SwfFixture.Movie(), async swf =>
        {
            Assert.False(Parse(await swf.Open("Interface\\Test.swf")).TryGetProperty("error", out _));
            var tagIndex = Parse(await swf.Placements()).GetProperty("placements")[0].GetProperty("tagIndex").GetInt32();
            var matrix = Parse(await swf.PlacementMatrix(tagIndex));
            Assert.False(matrix.GetProperty("hasScale").GetBoolean());
            Assert.Equal(0x10000, matrix.GetProperty("scaleX").GetInt32());
            Assert.Equal(0x10000, matrix.GetProperty("scaleY").GetInt32());
            Assert.Equal(0, matrix.GetProperty("rotate0").GetInt32());
        });
    }

    private static async Task WithOpenMovie(byte[] movie, Func<SwfInterop, Task> body)
    {
        var root = Path.Combine(Path.GetTempPath(), "fo4re-swf-rpc-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(Path.Combine(root, "Interface"));
        File.WriteAllBytes(Path.Combine(root, "Interface", "Test.swf"), movie);
        AssetResolver.SetSessionDataRoots(new[] { root });
        try
        {
            await body(new SwfInterop());
        }
        finally
        {
            AssetResolver.SetSessionDataRoots(Array.Empty<string>());
            Directory.Delete(root, recursive: true);
        }
    }

    [NativeFact]
    public async Task ShapeSvgRpcReturnsTheSvgForAShapeAndTheRefusalForANonShape()
    {
        // Invariant: ShapeSvg returns the svg field the panel reads for a shape and the error shape for a non-shape or an out-of-range id; detects a dropped svg field, a non-shape exporting as svg, or an id wrapped into the ushort range and exporting as svg.
        var root = Path.Combine(Path.GetTempPath(), "fo4re-swf-shape-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(Path.Combine(root, "Interface"));
        File.WriteAllBytes(Path.Combine(root, "Interface", "Test.swf"), SwfFixture.Movie(withShape: true));
        AssetResolver.SetSessionDataRoots(new[] { root });
        try
        {
            var swf = new SwfInterop();
            Assert.False(Parse(await swf.Open("Interface\\Test.swf")).TryGetProperty("error", out _));

            var svg = Parse(await swf.ShapeSvg(5)).GetProperty("svg").GetString() ?? string.Empty;
            Assert.StartsWith("<svg", svg);

            var refusal = Parse(await swf.ShapeSvg(11));
            Assert.False(string.IsNullOrEmpty(refusal.GetProperty("error").GetString()));
            Assert.False(refusal.TryGetProperty("svg", out _));

            foreach (var id in new[] { 70000, -1 })
            {
                var outOfRange = Parse(await swf.ShapeSvg(id));
                Assert.Contains("0..65535", outOfRange.GetProperty("error").GetString());
                Assert.False(outOfRange.TryGetProperty("svg", out _));
            }
        }
        finally
        {
            AssetResolver.SetSessionDataRoots(Array.Empty<string>());
            Directory.Delete(root, recursive: true);
        }
    }

    [NativeFact]
    public async Task BitmapPngRpcReturnsTheBase64PngForABitmapAndTheRefusalForANonBitmap()
    {
        // Invariant: BitmapPng replies the png field as base64 of the PNG bytes for the tag index the Characters listing gives a bitmap, and the error shape for an edit-text tag; detects a dropped png field, a png that is not base64 of the PNG signature, a bitmap read by character id, or a non-bitmap exporting as png.
        var root = Path.Combine(Path.GetTempPath(), "fo4re-swf-bitmap-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(Path.Combine(root, "Interface"));
        File.WriteAllBytes(Path.Combine(root, "Interface", "Test.swf"), SwfFixture.Movie(withBitmap: true));
        AssetResolver.SetSessionDataRoots(new[] { root });
        try
        {
            var swf = new SwfInterop();
            Assert.False(Parse(await swf.Open("Interface\\Test.swf")).TryGetProperty("error", out _));
            var characters = Parse(await swf.Characters()).GetProperty("characters").EnumerateArray().ToArray();

            var bitmap = characters.First(item => item.GetProperty("kind").GetString() == "Bitmap");
            var reply = Parse(await swf.BitmapPng(bitmap.GetProperty("tagIndex").GetInt32()));
            Assert.False(reply.TryGetProperty("error", out _));
            var png = Convert.FromBase64String(reply.GetProperty("png").GetString() ?? string.Empty);
            Assert.Equal(new byte[] { 137, 80, 78, 71, 13, 10, 26, 10 }, png[..8]);

            var text = characters.First(item => item.GetProperty("kind").GetString() == "EditText");
            var refusal = Parse(await swf.BitmapPng(text.GetProperty("tagIndex").GetInt32()));
            Assert.False(string.IsNullOrEmpty(refusal.GetProperty("error").GetString()));
            Assert.False(refusal.TryGetProperty("png", out _));
        }
        finally
        {
            AssetResolver.SetSessionDataRoots(Array.Empty<string>());
            Directory.Delete(root, recursive: true);
        }
    }

    [NativeFact]
    public async Task BitmapJpegRpcReturnsTheBase64JpegForAJpegBitmapAndTheRefusalForANonBitmap()
    {
        // Invariant: BitmapJpeg replies the jpeg field as base64 of the JPEG stream for the tag index the Characters listing gives a JPEG-defined bitmap, and the error shape for an edit-text tag; detects a dropped jpeg field, a jpeg that is not base64 of the JPEG bytes, a JPEG read by character id, or a non-JPEG exporting as jpeg.
        var root = Path.Combine(Path.GetTempPath(), "fo4re-swf-jpeg-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(Path.Combine(root, "Interface"));
        File.WriteAllBytes(Path.Combine(root, "Interface", "Test.swf"), SwfFixture.Movie(withJpeg: true));
        AssetResolver.SetSessionDataRoots(new[] { root });
        try
        {
            var swf = new SwfInterop();
            Assert.False(Parse(await swf.Open("Interface\\Test.swf")).TryGetProperty("error", out _));
            var characters = Parse(await swf.Characters()).GetProperty("characters").EnumerateArray().ToArray();

            var jpegBitmap = characters.First(item => item.GetProperty("kind").GetString() == "Bitmap");
            var reply = Parse(await swf.BitmapJpeg(jpegBitmap.GetProperty("tagIndex").GetInt32()));
            Assert.False(reply.TryGetProperty("error", out _));
            var jpeg = reply.GetProperty("jpeg").GetString() ?? string.Empty;
            Assert.StartsWith("/9j/", jpeg);
            Assert.Equal(new byte[] { 0xFF, 0xD8, 0xFF, 0xD9 }, Convert.FromBase64String(jpeg));

            var text = characters.First(item => item.GetProperty("kind").GetString() == "EditText");
            var refusal = Parse(await swf.BitmapJpeg(text.GetProperty("tagIndex").GetInt32()));
            Assert.False(string.IsNullOrEmpty(refusal.GetProperty("error").GetString()));
            Assert.False(refusal.TryGetProperty("jpeg", out _));
        }
        finally
        {
            AssetResolver.SetSessionDataRoots(Array.Empty<string>());
            Directory.Delete(root, recursive: true);
        }
    }

    [NativeFact]
    public async Task FrameSvgRpcReturnsTheRootFrameAndAnEmptyStageBeyondTheTimeline()
    {
        // Invariant: FrameSvg replies the svg field for the root frame and a well-formed empty stage for a frame past the root timeline, and refuses a negative frame; detects a dropped svg field, a frame past the timeline refused as an error, or a negative frame wrapped into the uint range and exporting as svg.
        await WithOpenMovie(SwfFixture.Movie(withShape: true), async swf =>
        {
            Assert.False(Parse(await swf.Open("Interface\\Test.swf")).TryGetProperty("error", out _));

            var svg = Parse(await swf.FrameSvg(0)).GetProperty("svg").GetString() ?? string.Empty;
            Assert.StartsWith("<svg", svg);

            var beyond = Parse(await swf.FrameSvg(999));
            Assert.False(beyond.TryGetProperty("error", out _));
            Assert.StartsWith("<svg", beyond.GetProperty("svg").GetString() ?? string.Empty);

            var negative = Parse(await swf.FrameSvg(-1));
            Assert.Contains("0 or more", negative.GetProperty("error").GetString());
            Assert.False(negative.TryGetProperty("svg", out _));
        });
    }

    private static JsonElement Parse(string json) => JsonDocument.Parse(json).RootElement;
}
