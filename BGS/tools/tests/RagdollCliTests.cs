using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using BehaviourStudio.Tools;
using OpenCommonwealth.Services.Hkx;
using Xunit;
using Xunit.Abstractions;

namespace BehaviourStudio.Tests;

public sealed class RagdollCliTests
{
    private readonly ITestOutputHelper _out;

    public RagdollCliTests(ITestOutputHelper output) => _out = output;

    [Fact]
    public void ASingleRagdollFileNeedsNoSelector()
    {
        using var work = new Work();
        string target = work.Copy("skeleton.hkx");

        var (exit, output, error) = Capture(() => CliEntry.Run(
            new[] { "ragdoll", target, "--set", "mass:0=5" }, TextWriter.Null, TextWriter.Null));

        Assert.Equal("", error);
        Assert.Equal(0, exit);
        _out.WriteLine(output);
        Assert.Contains("set mass:0=5", output);
        Assert.Contains("1 ragdoll(s)", output);
    }

    [Fact]
    public void ASelectorOutOfRangeIsRefusedAndNothingIsWritten()
    {
        using var work = new Work();
        string target = work.Copy("skeleton.hkx");
        byte[] before = File.ReadAllBytes(target);

        var (exit, _, error) = Capture(() => CliEntry.Run(
            new[] { "ragdoll", target, "--set", "ragdoll=7", "--set", "mass:0=5" }, TextWriter.Null, TextWriter.Null));

        Assert.Equal(1, exit);
        _out.WriteLine(error);
        Assert.Contains("there is no ragdoll 7", error);
        Assert.Equal(before, File.ReadAllBytes(target));
    }

    [Fact]
    public void ASelectorNamesTheRagdollEveryFollowingEditUses()
    {
        using var work = new Work();
        string target = work.Copy("skeleton.hkx");
        string outPath = work.At("edited.hkx");

        var (exit, output, error) = Capture(() => CliEntry.Run(
            new[] { "ragdoll", target, "--set", "ragdoll=0", "--set", "mass:0=7", "--out", outPath },
            TextWriter.Null, TextWriter.Null));

        Assert.Equal("", error);
        Assert.Equal(0, exit);
        _out.WriteLine(output);
        Assert.Contains("ragdoll 0 selected", output);
        Assert.Contains("the edits came back as written", output);

        var read = RagdollDocument.Open(outPath).Ragdolls[0];
        Assert.Equal(7f, read.Motions[read.Bodies[0].MotionId].Mass, 3);
    }

    [Fact]
    public void ADynamicAndStaticMaterialEditReadsBackAtHalfPrecision()
    {
        using var work = new Work();
        string target = work.Copy("skeleton.hkx");
        string outPath = work.At("material.hkx");

        var (exit, output, error) = Capture(() => CliEntry.Run(
            new[] { "ragdoll", target, "--set", "material:0=0.3,0.7", "--out", outPath },
            TextWriter.Null, TextWriter.Null));

        Assert.Equal("", error);
        Assert.Equal(0, exit);
        _out.WriteLine(output);
        Assert.Contains("stored dynamic", output);

        var material = RagdollDocument.Open(outPath).Ragdolls[0].Materials[0];
        Assert.Equal((float)(Half)0.3f, material.DynamicFriction);
        Assert.Equal((float)(Half)0.7f, material.StaticFriction);
    }

    [Fact]
    public void ARestitutionFieldWritesOnlyRestitution()
    {
        using var work = new Work();
        string target = work.Copy("skeleton.hkx");
        string outPath = work.At("restitution.hkx");

        var (exit, output, error) = Capture(() => CliEntry.Run(
            new[] { "ragdoll", target, "--set", "restitution:0=0.5", "--out", outPath },
            TextWriter.Null, TextWriter.Null));

        Assert.Equal("", error);
        Assert.Equal(0, exit);
        _out.WriteLine(output);
        Assert.Contains("stored restitution 0.5", output);

        var material = RagdollDocument.Open(outPath).Ragdolls[0].Materials[0];
        Assert.Equal(0.5f, material.Restitution);
    }

    [Fact]
    public void ACapsuleGeometryEditIsDerivedByTheCommand()
    {
        using var work = new Work();
        string target = work.Copy("skeleton.hkx");
        string outPath = work.At("capsule.hkx");
        var document = new RagdollDocument(File.ReadAllBytes(target));

        int index = -1;
        for (int i = 0; i < document.Shapes.Count; i++)
            if (document.Shapes[i].ReferencedByBodies <= 1)
            {
                index = i;
                break;
            }

        Assert.True(index >= 0, "every fixture capsule shape is named by more than one body");
        float wanted = document.Shapes[index].Radius * 1.4f;
        var (exit, _, error) = Capture(() => CliEntry.Run(
            new[] { "ragdoll", target, "--set", $"radius:{index}={wanted}", "--out", outPath },
            TextWriter.Null, TextWriter.Null));

        _out.WriteLine(error);
        Assert.Equal(0, exit);
        Assert.True(File.Exists(outPath));
        Assert.Equal(wanted, RagdollDocument.Open(outPath).Shapes[index].Radius);
    }

    [Fact]
    public void ACapsuleGeometryEditOfASharedShapeIsRefusedByTheCommand()
    {
        using var work = new Work();
        string target = work.Copy("skeleton.hkx");
        string outPath = work.At("shared.hkx");
        var document = new RagdollDocument(File.ReadAllBytes(target));

        int index = -1;
        for (int i = 0; i < document.Shapes.Count; i++)
            if (document.Shapes[i].ReferencedByBodies > 1)
            {
                index = i;
                break;
            }

        if (index < 0)
        {
            _out.WriteLine("the fixture names no capsule shape from more than one body, so there is nothing to refuse");
            return;
        }

        var (exit, _, error) = Capture(() => CliEntry.Run(
            new[] { "ragdoll", target, "--set", $"radius:{index}=5", "--out", outPath },
            TextWriter.Null, TextWriter.Null));

        Assert.Equal(1, exit);
        _out.WriteLine(error);
        Assert.Contains("named by", error);
        Assert.False(File.Exists(outPath));
    }

    [Fact]
    public void AShapeSharedThroughItsMassPropertiesNeedsItsOwnFlag()
    {
        using var work = new Work();
        byte[] patched = RagdollSharedShapeFixture.ShareProperties(
            File.ReadAllBytes(work.Copy("skeleton.hkx")), out int source, out int repointed);
        string target = work.At("shared.hkx");
        File.WriteAllBytes(target, patched);

        var document = new RagdollDocument(patched);
        int index = RagdollSharedShapeFixture.ShapeAt(document, source);
        Assert.NotEqual(source, repointed);
        float wanted = document.Shapes[index].Radius * 1.4f;

        string refusedOut = work.At("refused.hkx");
        var (refusedExit, _, refusedError) = Capture(() => CliEntry.Run(
            new[] { "ragdoll", target, "--set", $"radius:{index}={wanted}", "--out", refusedOut },
            TextWriter.Null, TextWriter.Null));
        Assert.Equal(1, refusedExit);
        _out.WriteLine(refusedError);
        Assert.Contains("shares its properties container", refusedError);
        Assert.False(File.Exists(refusedOut));

        string materialOut = work.At("material.hkx");
        var (materialExit, _, materialError) = Capture(() => CliEntry.Run(
            new[] { "ragdoll", target, "--allow-shared-material", "--set", $"radius:{index}={wanted}",
                    "--out", materialOut },
            TextWriter.Null, TextWriter.Null));
        Assert.Equal(1, materialExit);
        Assert.Contains("shares its properties container", materialError);
        Assert.False(File.Exists(materialOut));

        string shapeOut = work.At("shape.hkx");
        var (shapeExit, _, shapeError) = Capture(() => CliEntry.Run(
            new[] { "ragdoll", target, "--allow-shared-shape", "--set", $"radius:{index}={wanted}",
                    "--out", shapeOut },
            TextWriter.Null, TextWriter.Null));
        _out.WriteLine(shapeError);
        Assert.Equal(1, shapeExit);
        Assert.DoesNotContain("shares its properties container", shapeError);
        Assert.Contains("did not read back", shapeError);
        Assert.False(File.Exists(shapeOut));
    }
    [Fact]
    public void OverwritingTheSourceWithForceKeepsTheOriginalAsABackup()
    {
        using var work = new Work();
        string target = work.Copy("skeleton.hkx");
        byte[] before = File.ReadAllBytes(target);

        var (exit, output, error) = Capture(() => CliEntry.Run(
            new[] { "ragdoll", target, "--set", "mass:0=9", "--out", target, "--force" },
            TextWriter.Null, TextWriter.Null));

        Assert.Equal("", error);
        Assert.Equal(0, exit);
        _out.WriteLine(output);
        Assert.Contains(".bak", output);
        Assert.Equal(before, File.ReadAllBytes(target + ".bak"));

        var read = RagdollDocument.Open(target).Ragdolls[0];
        Assert.Equal(9f, read.Motions[read.Bodies[0].MotionId].Mass, 3);
    }

    [Fact]
    public void OverwritingTheSourceWithoutForceIsRefused()
    {
        using var work = new Work();
        string target = work.Copy("skeleton.hkx");
        byte[] before = File.ReadAllBytes(target);

        var (exit, _, error) = Capture(() => CliEntry.Run(
            new[] { "ragdoll", target, "--set", "mass:0=9", "--out", target }, TextWriter.Null, TextWriter.Null));

        Assert.Equal(1, exit);
        Assert.Contains("is the file being read", error);
        Assert.Equal(before, File.ReadAllBytes(target));
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
            Folder = Path.Combine(Path.GetTempPath(), "bgs_ragdoll_cli_" + Guid.NewGuid().ToString("N"));
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
