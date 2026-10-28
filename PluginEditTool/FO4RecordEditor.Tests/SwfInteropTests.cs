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
        // Invariant: ShapeSvg returns the svg field the panel reads for a shape and the error shape for a non-shape; detects a dropped svg field or a non-shape exporting as svg.
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
        }
        finally
        {
            AssetResolver.SetSessionDataRoots(Array.Empty<string>());
            Directory.Delete(root, recursive: true);
        }
    }

    private static JsonElement Parse(string json) => JsonDocument.Parse(json).RootElement;
}
