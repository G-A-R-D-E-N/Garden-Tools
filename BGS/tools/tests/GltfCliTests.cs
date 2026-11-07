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

    // Detects: a switch the import does not take, such as the removed --root-motion, is ignored and the clip is written anyway.
    [Fact]
    public void RefusesAnUnknownSwitchAndWritesNothing()
    {
        using var work = new GltfWork();
        string input = work.Tiny(new TinyOptions(), binary: false);
        string outPath = work.At("out.hkx");

        var (exit, _, error) = Capture(() => CliEntry.Run(
            new[] { "gltf", input, SkeletonPath, PairedKillPath, outPath, "--root-motion" },
            TextWriter.Null, TextWriter.Null));

        Assert.Equal(1, exit);
        Assert.Contains("'--root-motion' is not a gltf switch", error, StringComparison.Ordinal);
        Assert.False(File.Exists(outPath));
    }

    // Detects: a glTF with several animations is imported as its first clip without a name, or the --animation name is not honoured.
    [Fact]
    public void NamesTheAnimationsWhenTheFileHoldsSeveral()
    {
        using var work = new GltfWork();
        string input = work.Tiny(new TinyOptions(TwoAnimations: true), binary: false);

        var (refused, _, refusal) = Capture(() => CliEntry.Run(
            new[] { "gltf", input, SkeletonPath, PairedKillPath, work.At("refused.hkx") }, TextWriter.Null, TextWriter.Null));
        Assert.Equal(1, refused);
        Assert.Contains("2 animations (tiny, again)", refusal, StringComparison.Ordinal);

        var (exit, _, error) = Capture(() => CliEntry.Run(
            new[] { "gltf", input, SkeletonPath, PairedKillPath, work.At("again.hkx"), "--animation", "again" },
            TextWriter.Null, TextWriter.Null));
        Assert.True(exit == 0, error);
        Assert.True(File.Exists(work.At("again.hkx")));
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

    // Detects: the CLI drops the paired kill's root travel or writes a motion that differs from it, or places the written clip off the source; placed poses compound the encoder's per-bone error along each bone chain, so their bound is looser than the per-bone budget.
    [Fact]
    public void ImportsThePairedKillThroughTheCliWithItsOwnTrackMap()
    {
        var skeleton = Skeleton();
        var source = PairedKill();
        var motion = RootMotion.Read(PairedKillPath);
        using var work = new GltfWork();
        var (json, bin) = Export(skeleton, source, embedded: false, motion);
        string input = work.Write("paired.glb", Glb(json, bin));
        string outPath = work.At("paired-out.hkx");

        var (exit, output, error) = Capture(() => CliEntry.Run(
            new[] { "gltf", input, SkeletonPath, PairedKillPath, outPath }, TextWriter.Null, TextWriter.Null));

        Assert.True(exit == 0, error);
        Assert.Equal("", error);
        _out.WriteLine(output);
        Assert.Contains("template tracks written: 94 of 95 bones", output, StringComparison.Ordinal);
        Assert.Contains("template annotations cleared: 7", output, StringComparison.Ordinal);
        Assert.Contains("template root motion replaced by the glTF root motion", output, StringComparison.Ordinal);

        var back = new HkxBinaryReader().ReadAnimation(File.ReadAllBytes(outPath));
        var written = RootMotion.Read(outPath);
        var (travel, turn) = CompareMotion(motion, written);
        Assert.True(travel <= 0.001f, $"root travel off by {travel} units");
        Assert.True(turn <= 0.001f, $"root turn off by {turn} rad");
        var paired = ComparePlaced(skeleton, source, motion, back, written);
        _out.WriteLine($"paired kill, read back: worst position {paired.Position:G3} units, rotation {paired.Rotation:G3} rad, scale {paired.Scale:G3}");
        Assert.Equal(source.NumFrames, back.NumFrames);
        Assert.Empty(back.Annotations);
        Assert.True(paired.Position <= 0.2f, $"position off by {paired.Position} units");
        Assert.True(paired.Rotation <= 0.01f, $"rotation off by {paired.Rotation} rad");
        Assert.True(paired.Scale <= 0.0005f, $"scale off by {paired.Scale}");
    }

    // Detects: the CLI drops the TurnLeft90 root turn from the written motion, or places the written clip off the source; placed poses compound the encoder's per-bone error along each bone chain, so their bound is looser than the per-bone budget.
    [Fact]
    public void ImportsTurnLeft90ThroughTheCliWithItsOwnTrackMap()
    {
        var skeleton = Skeleton();
        var source = TurnLeft90();
        var motion = RootMotion.Read(TurnLeft90Path);
        using var work = new GltfWork();
        var (json, bin) = Export(skeleton, source, embedded: false, motion);
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
        var written = RootMotion.Read(outPath);
        var (travel, rootTurn) = CompareMotion(motion, written);
        Assert.True(travel <= 0.001f, $"root travel off by {travel} units");
        Assert.True(rootTurn <= 0.001f, $"root turn off by {rootTurn} rad");
        var turn = ComparePlaced(skeleton, source, motion, back, written);
        _out.WriteLine($"TurnLeft90, read back: worst position {turn.Position:G3} units, rotation {turn.Rotation:G3} rad, scale {turn.Scale:G3}");
        Assert.Equal(source.NumFrames, back.NumFrames);
        Assert.Empty(back.Annotations);
        Assert.True(turn.Position <= 0.2f, $"position off by {turn.Position} units");
        Assert.True(turn.Rotation <= 0.01f, $"rotation off by {turn.Rotation} rad");
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
