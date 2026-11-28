using System;
using System.IO;
using System.Linq;
using BehaviourStudio.Tools;
using Xunit;
using Xunit.Abstractions;

namespace BehaviourStudio.Tests;

public sealed class MapperCliTests
{
    private readonly ITestOutputHelper _out;

    public MapperCliTests(ITestOutputHelper output) => _out = output;

    [Fact]
    public void RefusesAnExistingOutWithoutForce()
    {
        using var work = new Work();
        string target = work.Copy("skeleton.hkx");
        string outPath = work.At("existing.hkx");
        File.WriteAllBytes(outPath, new byte[] { 1, 2, 3 });

        var (exit, _, error) = Capture(() => CliEntry.Run(
            new[] { "mapper", target, "--out", outPath }, TextWriter.Null, TextWriter.Null));

        Assert.Equal(1, exit);
        Assert.Contains("already exists", error);
        Assert.Equal(new byte[] { 1, 2, 3 }, File.ReadAllBytes(outPath));
    }

    [Fact]
    public void RefusesOutEqualToTheSourceWithoutForce()
    {
        using var work = new Work();
        string target = work.Copy("skeleton.hkx");
        byte[] before = File.ReadAllBytes(target);

        var (exit, _, error) = Capture(() => CliEntry.Run(
            new[] { "mapper", target, "--out", target }, TextWriter.Null, TextWriter.Null));

        Assert.Equal(1, exit);
        Assert.Contains("is the file being read", error);
        Assert.Equal(before, File.ReadAllBytes(target));
    }

    [Fact]
    public void ForceWritesAnExistingOutAndMovesTheResult()
    {
        using var work = new Work();
        string target = work.Copy("skeleton.hkx");
        string outPath = work.At("existing.hkx");
        File.WriteAllBytes(outPath, new byte[] { 1, 2, 3 });

        var (exit, output, error) = Capture(() => CliEntry.Run(
            new[] { "mapper", target, "--out", outPath, "--force" }, TextWriter.Null, TextWriter.Null));

        Assert.Equal(0, exit);
        Assert.Contains("written to", output);
        Assert.Equal("", error);
        byte[] produced = File.ReadAllBytes(outPath);
        Assert.Equal(File.ReadAllBytes(target).Length, produced.Length);
        Assert.False(Directory.GetFiles(work.Folder, "*.writing").Any());
    }

    [Fact]
    public void OverwritingTheSourceWithForceKeepsTheOriginalAsABackup()
    {
        using var work = new Work();
        string target = work.Copy("skeleton.hkx");
        byte[] before = File.ReadAllBytes(target);
        string elsewhere = work.At("rebuilt.hkx");
        Capture(() => CliEntry.Run(
            new[] { "mapper", target, "--out", elsewhere }, TextWriter.Null, TextWriter.Null));
        byte[] rebuilt = File.ReadAllBytes(elsewhere);

        var (exit, output, error) = Capture(() => CliEntry.Run(
            new[] { "mapper", target, "--out", target, "--force" }, TextWriter.Null, TextWriter.Null));

        Assert.Equal(0, exit);
        Assert.Equal("", error);
        Assert.Contains(".bak", output);
        Assert.Equal(before, File.ReadAllBytes(target + ".bak"));
        Assert.Equal(rebuilt, File.ReadAllBytes(target));
    }

    [Fact]
    public void UnknownSwitchesAndMissingValuesExitOneWithUsage()
    {
        using var work = new Work();
        string target = work.Copy("skeleton.hkx");

        var unknown = Capture(() => CliEntry.Run(
            new[] { "mapper", target, "--nonsense" }, TextWriter.Null, TextWriter.Null));
        Assert.Equal(1, unknown.Exit);
        Assert.Contains("is not a mapper switch", unknown.Error);
        Assert.Contains("Usage:", unknown.Error);

        var missingOut = Capture(() => CliEntry.Run(
            new[] { "mapper", target, "--out" }, TextWriter.Null, TextWriter.Null));
        Assert.Equal(1, missingOut.Exit);
        Assert.Contains("--out needs", missingOut.Error);
        Assert.Contains("Usage:", missingOut.Error);

        var missingData = Capture(() => CliEntry.Run(
            new[] { "mapper", target, "--data" }, TextWriter.Null, TextWriter.Null));
        Assert.Equal(1, missingData.Exit);
        Assert.Contains("--data needs", missingData.Error);

        var nothing = Capture(() => CliEntry.Run(new[] { "mapper" }, TextWriter.Null, TextWriter.Null));
        Assert.Equal(1, nothing.Exit);
        Assert.Contains("needs a file", nothing.Error);
    }

    [Fact]
    public void RecomputeWithoutOutReportsAndWritesNothing()
    {
        using var work = new Work();
        string target = work.Copy("skeleton.hkx");
        byte[] before = File.ReadAllBytes(target);

        var (exit, output, error) = Capture(() => CliEntry.Run(
            new[] { "mapper", target, "--recompute" }, TextWriter.Null, TextWriter.Null));

        Assert.Equal(0, exit);
        Assert.Contains("nothing was written", output);
        Assert.Equal("", error);
        Assert.Equal(before, File.ReadAllBytes(target));
        Assert.Single(Directory.GetFiles(work.Folder));
    }

    [Fact]
    public void TheSweepRefusesOut()
    {
        using var work = new Work();
        string folder = work.At("no-such-data");

        var (exit, _, error) = Capture(() => CliEntry.Run(
            new[] { "mapper", "--data", folder, "--out", work.At("x.hkx") }, TextWriter.Null, TextWriter.Null));

        Assert.Equal(1, exit);
        _out.WriteLine(error);
        Assert.Contains("--out is refused in sweep mode", error);
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

    private sealed class Work : IDisposable
    {
        public Work()
        {
            Folder = Path.Combine(Path.GetTempPath(), "bgs_mapper_cli_" + Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(Folder);
        }

        public string Folder { get; }

        public string At(string name) => Path.Combine(Folder, name);

        public string Copy(string name)
        {
            string target = At(name);
            File.Copy(Source, target);
            return target;
        }

        public void Dispose()
        {
            try { Directory.Delete(Folder, recursive: true); }
            catch (IOException) { }
        }

        private static string Source => Path.Combine(
            AppContext.BaseDirectory, "fixtures", "vanilla",
            "Meshes", "Actors", "Character", "CharacterAssets", "skeleton.hkx");
    }
}
