using System;
using System.IO;
using BehaviourStudio.Tools;
using OpenCommonwealth.Services.Hkx;
using Xunit;

namespace BehaviourStudio.Tests;

[Collection("symrm-console")]
public sealed class SymrmUnsupportedAnimationTests
{
    private static string Fixture(string relative) =>
        Path.Combine(AppContext.BaseDirectory, "fixtures/vanilla", relative.Replace('/', Path.DirectorySeparatorChar));

    private static int Run(string[] argv, out string stdout)
    {
        var writer = new StringWriter();
        TextWriter old = Console.Out;
        stdout = "";
        try
        {
            Console.SetOut(writer);
            return Program.Main(argv);
        }
        finally
        {
            Console.SetOut(old);
            stdout = writer.ToString();
        }
    }

    private static string AnimationsRoot(out string animations)
    {
        string root = Directory.CreateTempSubdirectory("bgs-animation-classes").FullName;
        animations = Path.Combine(root, "Animations");
        Directory.CreateDirectory(animations);
        return root;
    }

    private static void Remove(string root)
    {
        try { Directory.Delete(root, recursive: true); }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException) { }
    }

    [Fact]
    public void ScaleCountsAndNamesTheClassItCannotDecode()
    {
        string root = AnimationsRoot(out string animations);
        try
        {
            File.WriteAllBytes(Path.Combine(animations, "Quantized.hkx"),
                               CraftedPackfile.Animation(AnimationCompression.QuantizedClass));

            int exit = Run(new[] { "scale", root }, out string stdout);

            Assert.Equal(0, exit);
            Assert.Contains("1 in a class this reader does not decode", stdout, StringComparison.Ordinal);
            Assert.Contains(AnimationCompression.QuantizedClass, stdout, StringComparison.Ordinal);
            Assert.Contains("has not been changed", stdout, StringComparison.Ordinal);
        }
        finally
        {
            Remove(root);
        }
    }

    [Fact]
    public void ScaleStrictFlagExitsNonZeroForUnsupportedFiles()
    {
        string root = AnimationsRoot(out string animations);
        try
        {
            File.WriteAllBytes(Path.Combine(animations, "Quantized.hkx"),
                               CraftedPackfile.Animation(AnimationCompression.QuantizedClass));

            int exit = Run(new[] { "scale", root, "--strict" }, out string stdout);

            Assert.Equal(1, exit);
            Assert.Contains("1 in a class this reader does not decode", stdout, StringComparison.Ordinal);
        }
        finally
        {
            Remove(root);
        }
    }

    [Fact]
    public void FramesNamesTheClassForADirectoryAFileAndADigest()
    {
        string root = AnimationsRoot(out string animations);
        try
        {
            string file = Path.Combine(animations, "Predictive.hkx");
            File.WriteAllBytes(file, CraftedPackfile.Animation(AnimationCompression.PredictiveClass));

            Assert.Equal(0, Run(new[] { "frames", root }, out string directoryOutput));
            Assert.Contains(AnimationCompression.PredictiveClass, directoryOutput, StringComparison.Ordinal);
            Assert.Contains("has not been changed", directoryOutput, StringComparison.Ordinal);

            Assert.Equal(0, Run(new[] { "frames", file }, out string oneFileOutput));
            Assert.Contains("has not been changed", oneFileOutput, StringComparison.Ordinal);

            Assert.Equal(0, Run(new[] { "frames", file, "--digest" }, out string digestOutput));
            Assert.Contains("has not been changed", digestOutput, StringComparison.Ordinal);
        }
        finally
        {
            Remove(root);
        }
    }

    [Fact]
    public void FramesStrictFlagExitsNonZeroInEveryMode()
    {
        string root = AnimationsRoot(out string animations);
        try
        {
            string file = Path.Combine(animations, "Predictive.hkx");
            File.WriteAllBytes(file, CraftedPackfile.Animation(AnimationCompression.PredictiveClass));

            Assert.Equal(1, Run(new[] { "frames", root, "--strict" }, out _));
            Assert.Equal(1, Run(new[] { "frames", file, "--strict" }, out _));
            Assert.Equal(1, Run(new[] { "frames", file, "--digest", "--strict" }, out _));
        }
        finally
        {
            Remove(root);
        }
    }

    [Fact]
    public void ARealReadFailureStillExitsOne()
    {
        string root = AnimationsRoot(out string animations);
        try
        {
            File.WriteAllBytes(Path.Combine(animations, "Broken.hkx"), new byte[] { 1, 2, 3, 4, 5, 6, 7, 8 });

            Assert.Equal(1, Run(new[] { "scale", root }, out string scaleOutput));
            Assert.Contains("0 in a class this reader does not decode", scaleOutput, StringComparison.Ordinal);
            Assert.Contains("1 could not be read", scaleOutput, StringComparison.Ordinal);
            Assert.Equal(1, Run(new[] { "scale", root, "--strict" }, out _));

            Assert.Equal(1, Run(new[] { "frames", root }, out string framesOutput));
            Assert.Contains("0 in a class this reader does not decode", framesOutput, StringComparison.Ordinal);
            Assert.Contains("1 threw", framesOutput, StringComparison.Ordinal);
            Assert.Equal(1, Run(new[] { "frames", root, "--strict" }, out _));
        }
        finally
        {
            Remove(root);
        }
    }

    [Fact]
    public void AFileThisBuildDecodesStillExitsZero()
    {
        string root = AnimationsRoot(out string animations);
        try
        {
            File.Copy(Fixture("Meshes/Actors/RadRoach/Animations/TurnLeft90.hkx"),
                      Path.Combine(animations, "TurnLeft90.hkx"));

            Assert.Equal(0, Run(new[] { "scale", root }, out string scaleOutput));
            Assert.Contains("0 in a class this reader does not decode", scaleOutput, StringComparison.Ordinal);
            Assert.DoesNotContain("has not been changed", scaleOutput, StringComparison.Ordinal);

            Assert.Equal(0, Run(new[] { "frames", root }, out string framesOutput));
            Assert.Contains("0 in a class this reader does not decode", framesOutput, StringComparison.Ordinal);
            Assert.DoesNotContain("has not been changed", framesOutput, StringComparison.Ordinal);

            Assert.Equal(0, Run(new[] { "scale", root, "--strict" }, out _));
            Assert.Equal(0, Run(new[] { "frames", root, "--strict" }, out _));
        }
        finally
        {
            Remove(root);
        }
    }
}

[CollectionDefinition("symrm-console", DisableParallelization = true)]
public sealed class SymrmConsoleCollection
{
}
