using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using OpenCommonwealth.Services.Archive;
using OpenCommonwealth.Services.Hkx;

namespace BehaviourStudio.Tools;

public static partial class Program
{
    private static int Motion(string[] argv)
    {
        if (argv.Length is 3 or 4 && argv[1] == "--data")
            return MotionData(Path.GetFullPath(argv[2]), argv.Length == 4 ? argv[3] : null);
        if (argv.Length != 2 || argv[1].StartsWith("--", StringComparison.Ordinal)) { Usage(); return 1; }
        string target = Path.GetFullPath(argv[1]);
        if (Directory.Exists(target))
        {
            var totals = new MotionTotals();
            foreach (string file in Directory.EnumerateFiles(target, "*.hkx", SearchOption.AllDirectories))
                totals.Read(file, () => RootMotion.Read(file));
            return totals.Report();
        }
        DumpMotion(target, RootMotion.Read(target));
        return 0;
    }

    private static void DumpMotion(string name, RootMotion.Motion motion)
    {
        Console.WriteLine($"{name}: {motion}");
        if (!motion.Any) return;
        Console.WriteLine($"up {motion.Up}, forward {motion.Forward}, raw distance {motion.Distance:R} units");
        for (int i = 0; i < motion.Samples.Count; i++)
        {
            var sample = motion.Samples[i];
            Console.WriteLine($"  sample {i,4}: {sample.Position.X:R} {sample.Position.Y:R} {sample.Position.Z:R} {sample.TurnRadians:R}");
        }
        Console.WriteLine($"final angle: {motion.Samples[^1].TurnRadians * 180 / MathF.PI:R} degrees (unwrapped)");
    }

    private static int MotionData(string folder, string? filter)
    {
        using var data = GameData.Discover(folder, pluginsPath: Path.Combine(folder, ".bgs-no-plugins"));
        var totals = new MotionTotals();
        int matched = 0, dumped = 0, failed = 0;
        foreach (string path in data.ArchivePaths)
        {
            if (!Path.GetFileName(path).Contains("Animations", StringComparison.OrdinalIgnoreCase)) continue;
            using var archive = Ba2.Open(path);
            foreach (var entry in archive.Entries.Where(e => e.Name.EndsWith(".hkx", StringComparison.OrdinalIgnoreCase)
                         && e.Name.Contains("/animations/", StringComparison.OrdinalIgnoreCase)
                         && (filter == null || e.Name.Contains(filter.Replace('\\', '/'), StringComparison.OrdinalIgnoreCase))))
            {
                string name = Path.GetFileName(path) + ":" + entry.Name;
                if (filter == null) { totals.Read(name, () => RootMotion.Read(archive.Read(entry))); continue; }

                matched++;
                try
                {
                    var bytes = archive.Read(entry);
                    DumpMotion(name, RootMotion.Read(bytes));
                    DumpMotionFrame(name, bytes);
                    dumped++;
                }
                catch (Exception e) when (e is InvalidDataException or IOException or ArgumentException or OverflowException)
                {
                    failed++;
                    Console.Error.WriteLine($"could not read {name}: {e.Message}");
                }
            }
        }
        if (filter == null) return totals.Report();

        Console.WriteLine($"{matched} entries matched '{filter}': {dumped} dumped, {failed} could not be read");
        return matched == 0 || failed > 0 ? 1 : 0;
    }

    private static void DumpMotionFrame(string name, byte[] bytes)
    {
        var image = PackfileImage.Read(bytes);
        var objects = new PackfileObjects(image);
        var animation = objects.Instances.FirstOrDefault(i => objects.FieldAt(i, "extractedMotion") != null);
        if (animation == null)
        {
            Console.WriteLine($"{name}: no object carries an extractedMotion, so there is no frame to dump");
            return;
        }
        var frame = objects.ReadRef(animation, "extractedMotion", out _);
        if (frame == null)
        {
            Console.WriteLine($"{name}: extractedMotion is null, so there is no frame to dump");
            return;
        }
        if (objects.FieldAt(frame, "referenceFrameSamples") is not { } field)
        {
            Console.WriteLine($"{name}: {frame} carries no referenceFrameSamples field");
            return;
        }

        var section = image.Section(PackfileObjects.DataSection)!;
        int target = -1;
        foreach (var local in section.Locals())
        {
            if (local.Item1 != field) continue;
            target = local.Item2;
            break;
        }
        if (target < 0)
        {
            Console.WriteLine($"{name}: array header 0x{field:x} has no local fixup, so no samples are inline");
            return;
        }

        Console.WriteLine($"frame {frame}, array header 0x{field:x}, local fixup target 0x{target:x}, stride 16 bytes");
        int count = BitConverter.ToInt32(section.Data, field + image.Layout.PointerSize);
        for (int i = 0; i < count; i++)
            Console.WriteLine($"  raw 0x{target + i * 16:x}: {System.Convert.ToHexString(section.Data.AsSpan(target + i * 16, 16))}");
    }

    private sealed class MotionTotals
    {
        private int _files, _carrying, _still, _failed, _invalid;
        private float _minAngle = float.PositiveInfinity, _maxAngle = float.NegativeInfinity;
        private readonly List<(float Distance, string Name)> _largest = new();
        private readonly SortedDictionary<string, int> _unsupported = new(StringComparer.Ordinal);

        public void Read(string name, Func<RootMotion.Motion> read)
        {
            _files++;
            try
            {
                var motion = read();
                if (motion.UnsupportedReason is { } reason)
                {
                    _unsupported.TryGetValue(reason, out int count);
                    _unsupported[reason] = count + 1;
                    return;
                }
                if (!motion.Any) { _still++; return; }
                _carrying++;
                if (motion.LooksInvalid) _invalid++;
                _largest.Add((motion.Distance, name));
                _largest.Sort((a, b) => b.Distance.CompareTo(a.Distance));
                if (_largest.Count > 10) _largest.RemoveAt(10);
                float angle = motion.Samples[^1].TurnRadians * 180 / MathF.PI;
                if (float.IsFinite(angle))
                {
                    _minAngle = Math.Min(_minAngle, angle);
                    _maxAngle = Math.Max(_maxAngle, angle);
                }
            }
            catch (Exception e) when (e is InvalidDataException or IOException or ArgumentException or OverflowException)
            {
                _failed++;
                Console.Error.WriteLine($"could not read {name}: {e.Message}");
            }
        }

        public int Report()
        {
            Console.WriteLine($"{_files} animations: {_carrying} carry root motion, {_still} without samples, " +
                              $"{_invalid} invalid (included in carrying), {_unsupported.Values.Sum()} unsupported, {_failed} could not be read");
            if (_carrying > 0)
            {
                foreach (var item in _largest) Console.WriteLine($"distance: {item.Distance:F3} units ({item.Name})");
                Console.WriteLine($"final-angle range: {_minAngle:F3} to {_maxAngle:F3} degrees (unwrapped)");
            }
            foreach (var kind in _unsupported) Console.WriteLine($"unsupported {kind.Key}: {kind.Value}");
            return _files == 0 || _failed > 0 ? 1 : 0;
        }
    }
}
