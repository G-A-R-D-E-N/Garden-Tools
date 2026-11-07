using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using OpenCommonwealth.Services;
using OpenCommonwealth.Services.Hkx;

namespace BehaviourStudio.Tools;

public static class GltfCli
{
    public static int Run(string[] argv)
    {
        var positional = new List<string>();
        string? animationName = null;
        for (int i = 1; i < argv.Length; i++)
        {
            string flag = argv[i];
            if (flag == "--animation")
            {
                if (i + 1 >= argv.Length) return Usage("--animation needs the name of the glTF animation to import");
                animationName = argv[++i];
                continue;
            }

            if (flag.StartsWith("-", StringComparison.Ordinal)) return Usage($"'{flag}' is not a gltf switch");
            positional.Add(flag);
        }

        if (positional.Count != 4)
            return Usage("gltf needs the glTF animation, the skeleton, the template animation and the output");

        try
        {
            string gltfPath = Path.GetFullPath(positional[0]);
            string skeletonPath = Path.GetFullPath(positional[1]);
            string templatePath = Path.GetFullPath(positional[2]);
            string outPath = Path.GetFullPath(positional[3]);

            if (new[] { gltfPath, skeletonPath, templatePath }.Any(input =>
                    string.Equals(input, outPath, StringComparison.OrdinalIgnoreCase)))
            {
                Console.Error.WriteLine($"Not written: {outPath} is one of the inputs.");
                return 1;
            }

            if (File.Exists(outPath))
            {
                Console.Error.WriteLine($"Not written: {outPath} already exists. Choose another output.");
                return 1;
            }

            var scene = GltfScene.Read(gltfPath);
            var skeleton = new HkxBinaryReader().ReadSkeleton(skeletonPath);
            var imported = GltfAnimationImport.Build(scene, skeleton, animationName);
            var written = GltfAnimationImport.Write(InputFilePolicy.ReadHkx(templatePath), imported, skeleton);
            FileSafety.Replace(outPath, written.Bytes);

            var report = imported.Report;
            Console.WriteLine($"{Path.GetFileName(gltfPath)}: {report.Frames} frame(s) at {report.FrameRate:F0} per " +
                              $"second, {report.Duration:F3} s, {imported.Animation.Tracks.Count} track(s)");
            Console.WriteLine($"  animated bones ({report.Animated.Count}): {Names(report.Animated)}");
            Console.WriteLine($"  skeleton bones with no animation, at the reference pose ({report.ReferenceOnly.Count}): " +
                              Names(report.ReferenceOnly));
            Console.WriteLine($"  glTF joints the skeleton does not name, ignored ({report.Unmapped.Count}): " +
                              Names(report.Unmapped));
            Console.WriteLine($"  animated bones whose glTF rest pose differs from the skeleton's ({report.RestMismatch.Count}): " +
                              Names(report.RestMismatch));
            Console.WriteLine($"  channels ignored: {report.WeightChannels} morph weight, " +
                              $"{report.NonBoneChannels} on non-bone nodes");
            Console.WriteLine($"  template tracks written: {written.Tracks} of {imported.Animation.Tracks.Count} bones " +
                              "(the template's track-to-bone map is kept)");
            Console.WriteLine($"  template annotations cleared: {written.AnnotationsCleared}; " +
                              $"glTF annotations written: {written.AnnotationsWritten}");
            Console.WriteLine($"  root motion from the root bone, its track kept in place: {written.Motion}");
            Console.WriteLine(written.MotionReplaced
                ? "  template root motion replaced by the glTF root motion"
                : "  template has no root motion frame, so none is written");
            Console.WriteLine($"  rotation encoded at format {written.RotationFormat}, fit tolerance " +
                              $"{written.RotationTolerance:G3} rad, to meet the error budget");
            Console.WriteLine($"  written to {outPath}: {written.Bytes.Length} bytes, worst error " +
                              $"{written.PositionError:F4} units, {written.RotationError:F5} radians, " +
                              $"{written.ScaleError:F5} scale");
            return 0;
        }
        catch (Exception error) when (error is InvalidDataException or InvalidOperationException or
                                          NotSupportedException or FormatException or IOException or
                                          UnauthorizedAccessException or ArgumentException)
        {
            Console.Error.WriteLine("Not written: " + error.Message);
            return 1;
        }
    }

    private static int Usage(string problem)
    {
        Console.Error.WriteLine(problem);
        Console.Error.WriteLine();
        Console.Error.WriteLine("Usage: symrm gltf <animation.gltf-or-glb> <skeleton.hkx> <template.hkx> <output.hkx> [--animation <name>]");
        return 1;
    }

    private static string Names(IReadOnlyList<string> names) =>
        names.Count == 0 ? "none" : string.Join(", ", names);
}
