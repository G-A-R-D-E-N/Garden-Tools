using System;
using System.IO;
using System.Linq;
using OpenCommonwealth.Services.Hkx;

namespace BehaviourStudio.Tools;

public static partial class Program
{
    private static int Motion(string[] argv)
    {
        if (argv.Length < 2) { Usage(); return 1; }

        string target = Path.GetFullPath(argv[1]);

        if (Directory.Exists(target))
        {
            var files = Directory.GetFiles(target, "*.hkx", SearchOption.AllDirectories);
            int carrying = 0, still = 0, failed = 0;
            float furthest = 0;
            string furthestName = "";

            foreach (string file in files)
            {
                try
                {
                    var read = RootMotion.Read(file);
                    if (!read.Any) { still++; continue; }

                    carrying++;
                    if (read.Travel.Length() > furthest)
                    {
                        furthest = read.Travel.Length();
                        furthestName = Path.GetFileName(file);
                    }
                }
                catch { failed++; }
            }

            Console.WriteLine($"{files.Length} files: {carrying} carry root motion, {still} stay on " +
                              $"the spot, {failed} could not be read");
            if (carrying > 0)
                Console.WriteLine($"furthest travelled: {furthestName} at {furthest:F1} units");
            return failed == files.Length ? 1 : 0;
        }

        var motion = RootMotion.Read(target);
        Console.WriteLine($"{Path.GetFileName(target)}: {motion}");
        if (!motion.Any) return 0;

        Console.WriteLine($"up {motion.Up.X:F0} {motion.Up.Y:F0} {motion.Up.Z:F0}, " +
                          $"forward {motion.Forward.X:F0} {motion.Forward.Y:F0} {motion.Forward.Z:F0}");

        for (int i = 0; i < motion.Samples.Count; i += Math.Max(1, motion.Samples.Count / 8))
            Console.WriteLine($"  sample {i,3}  {motion.Samples[i]}");

        Console.WriteLine($"  sample {motion.Samples.Count - 1,3}  {motion.Samples[^1]}");

        var half = RootMotion.At(motion, 0.5f);
        Console.WriteLine($"halfway through: {half}");
        return 0;
    }
}
