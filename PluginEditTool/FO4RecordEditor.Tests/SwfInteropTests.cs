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
    [Fact]
    public async Task AbcRpcsReturnTheFieldsThePanelReadsAndTheRefusalShape()
    {
        // Invariant: the ABC RPC field names the web panel reads must not change; detects a renamed field or a dropped error shape.
        RequireNativeLibrary();
        var root = Path.Combine(Path.GetTempPath(), "fo4re-swf-rpc-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(Path.Combine(root, "Interface"));
        File.WriteAllBytes(Path.Combine(root, "Interface", "Test.swf"), SwfFixture.Movie(withAbc: true));
        AssetResolver.SetSessionDataRoots(new[] { root });
        try
        {
            var swf = new SwfInterop();
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
        }
        finally
        {
            AssetResolver.SetSessionDataRoots(Array.Empty<string>());
            Directory.Delete(root, recursive: true);
        }
    }

    private static JsonElement Parse(string json) => JsonDocument.Parse(json).RootElement;

    private static void RequireNativeLibrary()
    {
        if (string.IsNullOrWhiteSpace(Environment.GetEnvironmentVariable("RADICLE_NATIVE_PATH"))
            && RadicleNative.FindFirstExisting(RadicleNative.CandidatePaths(AppContext.BaseDirectory)) is null)
            throw Xunit.Sdk.SkipException.ForSkip("The native Radicle library is not deployed next to the test host.");
    }
}
