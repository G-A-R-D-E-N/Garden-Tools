using System;
using System.IO;
using System.Linq;
using BehaviourStudio.Tools;
using OpenCommonwealth.Services.Hkx;
using Xunit;
using Xunit.Abstractions;
using static BehaviourStudio.Tests.GltfTestFiles;

namespace BehaviourStudio.Tests;

public sealed class GltfCliTests
{
    private readonly ITestOutputHelper _out;

    public GltfCliTests(ITestOutputHelper output) => _out = output;

    // Regression: a --root-motion request is ignored and the clip is written without the motion the caller asked for.
    [Fact]
    public void RefusesARootMotionRequestAndWritesNothing()
    {
        using var work = new GltfWork();
        string input = work.Tiny(new TinyOptions(), binary: false);
        string outPath = work.At("out.hkx");

        var (exit, _, error) = Capture(() => CliEntry.Run(
            new[] { "gltf", input, SkeletonPath, PairedKillPath, outPath, "--root-motion" },
            TextWriter.Null, TextWriter.Null));

        Assert.Equal(1, exit);
        Assert.Contains("root motion", error, StringComparison.Ordinal);
        Assert.False(File.Exists(outPath));
    }

    // Regression: an import overwrites an existing output, destroying a file the modder still had.
    [Fact]
    public void RefusesAnExistingOutputWithoutTouchingIt()
    {
        using var work = new GltfWork();
        string input = work.Tiny(new TinyOptions(), binary: false);
        string outPath = work.Write("existing.hkx", new byte[] { 1, 2, 3 });

        var (exit, _, error) = Capture(() => CliEntry.Run(
            new[] { "gltf", input, SkeletonPath, PairedKillPath, outPath },
            TextWriter.Null, TextWriter.Null));

        Assert.Equal(1, exit);
        Assert.Contains("already exists", error, StringComparison.Ordinal);
        Assert.Equal(new byte[] { 1, 2, 3 }, File.ReadAllBytes(outPath));
    }

    // Regression: a missing argument is read as a path, so the command runs on whatever the arguments happen to name.
    [Fact]
    public void PrintsUsageForAWrongArgumentCount()
    {
        var (exit, _, error) = Capture(() => CliEntry.Run(
            new[] { "gltf", "clip.glb", "skeleton.hkx" }, TextWriter.Null, TextWriter.Null));

        Assert.Equal(1, exit);
        Assert.Contains("Usage: symrm gltf", error, StringComparison.Ordinal);
    }

    // Regression: a real template whose 94 tracks drive 94 of the skeleton's 95 bones cannot be imported through the CLI.
    [Fact]
    public void ImportsThePairedKillThroughTheCliWithItsOwnTrackMap()
    {
        var skeleton = Skeleton();
        var source = PairedKill();
        using var work = new GltfWork();
        var (json, bin) = Export(skeleton, source, embedded: false);
        string input = work.Write("paired.glb", Glb(json, bin));
        string outPath = work.At("paired-out.hkx");

        var (exit, output, error) = Capture(() => CliEntry.Run(
            new[] { "gltf", input, SkeletonPath, PairedKillPath, outPath }, TextWriter.Null, TextWriter.Null));

        Assert.True(exit == 0, error);
        Assert.Equal("", error);
        _out.WriteLine(output);
        Assert.Contains("template tracks written: 94 of 95 bones", output, StringComparison.Ordinal);
        Assert.Contains("template annotations cleared: 7", output, StringComparison.Ordinal);
        Assert.Contains("replaced by a stationary frame", output, StringComparison.Ordinal);

        var back = new HkxBinaryReader().ReadAnimation(File.ReadAllBytes(outPath));
        var paired = ComparePoses(skeleton, source, back);
        _out.WriteLine($"paired kill, read back: worst position {paired.Position:G3} units, rotation {paired.Rotation:G3} rad, scale {paired.Scale:G3}");
        Assert.Equal(source.NumFrames, back.NumFrames);
        Assert.Empty(back.Annotations);
        Assert.True(paired.Position <= 0.01f, $"position off by {paired.Position} units");
        Assert.True(paired.Rotation <= 0.001f, $"rotation off by {paired.Rotation} rad");
        Assert.True(paired.Scale <= 0.0005f, $"scale off by {paired.Scale}");
    }

    // Regression: a real template whose 38 tracks drive the first 38 bones cannot be imported through the CLI.
    [Fact]
    public void ImportsTurnLeft90ThroughTheCliWithItsOwnTrackMap()
    {
        var skeleton = Skeleton();
        var source = TurnLeft90();
        using var work = new GltfWork();
        var (json, bin) = Export(skeleton, source, embedded: false);
        string input = work.Write("turn.glb", Glb(json, bin));
        string outPath = work.At("turn-out.hkx");

        var (exit, output, error) = Capture(() => CliEntry.Run(
            new[] { "gltf", input, SkeletonPath, TurnLeft90Path, outPath }, TextWriter.Null, TextWriter.Null));

        Assert.True(exit == 0, error);
        Assert.Equal("", error);
        _out.WriteLine(output);
        Assert.Contains("template tracks written: 38 of 95 bones", output, StringComparison.Ordinal);
        Assert.Contains("template annotations cleared: 4", output, StringComparison.Ordinal);

        var back = new HkxBinaryReader().ReadAnimation(File.ReadAllBytes(outPath));
        var turn = ComparePoses(skeleton, source, back);
        _out.WriteLine($"TurnLeft90, read back: worst position {turn.Position:G3} units, rotation {turn.Rotation:G3} rad, scale {turn.Scale:G3}");
        Assert.Equal(source.NumFrames, back.NumFrames);
        Assert.Empty(back.Annotations);
        Assert.True(turn.Position <= 0.01f, $"position off by {turn.Position} units");
        Assert.True(turn.Rotation <= 0.001f, $"rotation off by {turn.Rotation} rad");
        Assert.True(turn.Scale <= 0.0005f, $"scale off by {turn.Scale}");
    }

    // Regression: malformed glTF JSON escapes the CLI as an uncaught JsonException, so the process crashes instead of printing a refusal.
    [Fact]
    public void RefusesMalformedGltfJsonThroughTheCli()
    {
        using var work = new GltfWork();
        string input = work.Write("bad.gltf", System.Text.Encoding.UTF8.GetBytes("{ \"nodes\": [ "));
        string outPath = work.At("bad-out.hkx");

        var (exit, _, error) = Capture(() => CliEntry.Run(
            new[] { "gltf", input, SkeletonPath, PairedKillPath, outPath }, TextWriter.Null, TextWriter.Null));

        Assert.Equal(1, exit);
        Assert.Contains("Not written:", error, StringComparison.Ordinal);
        Assert.Contains("malformed", error, StringComparison.Ordinal);
        Assert.False(File.Exists(outPath));
    }

    private static (int Exit, string Output, string Error) Capture(Func<int> action)
    {
        var output = new StringWriter();
        var error = new StringWriter();
        var savedOut = Console.Out;
        var savedError = Console.Error;
        try
        {
            Console.SetOut(output);
            Console.SetError(error);
            int exit = action();
            return (exit, output.ToString(), error.ToString());
        }
        finally
        {
            Console.SetOut(savedOut);
            Console.SetError(savedError);
        }
    }
}
