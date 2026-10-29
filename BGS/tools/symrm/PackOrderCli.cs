using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using OpenCommonwealth.Services.Archive;
using OpenCommonwealth.Services.Hkx;

namespace BehaviourStudio.Tools;

public static partial class Program
{
    private static int Walk(string[] argv)
    {
        string? dataFolder = null;
        bool serialize = false;

        for (int i = 1; i < argv.Length; i++)
        {
            if (argv[i] == "--serialize") { serialize = true; continue; }
            if (argv[i] != "--data") continue;
            if (i + 1 >= argv.Length) return PackOrderUsage("--data needs the Data folder to sweep");
            if (FullPath(argv[++i]) is not string folder) return PackOrderUsage("--data is not a usable path");
            dataFolder = folder;
        }

        if (dataFolder != null)
        {
            if (argv.Length > 1 && !argv[1].StartsWith("-", StringComparison.Ordinal))
                return PackOrderUsage("--data sweeps a whole Data folder, so it takes no file");
            return PackOrderSweep(dataFolder, serialize);
        }

        if (argv.Length < 2) { Usage(); return 1; }

        return PackOrderFiles(Path.GetFullPath(argv[1]));
    }

    private sealed record Order(bool Global, bool Local, bool Virtual, int Entries, int Objects,
                                string? Problem)
    {
        public bool Clean => Problem == null && Global && Local && Virtual;
    }

    private static Order? OrderOf(PackfileImage image)
    {
        var data = image.Section("__data__");
        if (data == null) return null;

        var objects = new PackfileObjects(image);
        var types = HavokClassTypes.Shipped;
        if (objects.Instances.Any(i => !types.Knows(i.ClassName))) return null;

        var offsets = data.Virtuals().Select(v => v.Source).ToList();
        bool virtualOk = true;
        string? problem = null;
        for (int at = 1; at < offsets.Count; at++)
        {
            if (offsets[at] > offsets[at - 1]) continue;
            virtualOk = false;
            problem = $"the virtual table is not in file order, entry {at} at 0x{offsets[at]:x} " +
                      $"follows 0x{offsets[at - 1]:x}";
            break;
        }

        int entries = 0;
        bool globalOk = true, localOk = true;
        foreach (bool global in new[] { true, false })
        {
            var actual = global ? data.Globals().Select(g => g.Source).ToList()
                                : data.Locals().Select(l => l.Source).ToList();
            var predicted = FixupOrder.Sources(objects, types, data, global);
            entries += actual.Count;

            if (predicted.SequenceEqual(actual)) continue;

            int at = predicted.Zip(actual).TakeWhile(p => p.First == p.Second).Count();
            if (global) globalOk = false; else localOk = false;
            problem ??= $"the {(global ? "global" : "local")} table is not in that order, " +
                        $"{predicted.Count} predicted against {actual.Count}, first differing at {at}";
        }

        return new Order(globalOk, localOk, virtualOk, entries, offsets.Count, problem);
    }

    private static int PackOrderFiles(string target)
    {
        var files = Directory.Exists(target)
            ? Directory.GetFiles(target, "*.hkx", SearchOption.AllDirectories)
                       .OrderBy(f => f, StringComparer.Ordinal).ToList()
            : new List<string> { target };

        int clean = 0, bad = 0, entries = 0, virtuals = 0;

        foreach (string file in files)
        {
            PackfileImage image;
            try { image = PackfileImage.Read(file); }
            catch (Exception e)
            {
                Console.WriteLine($"  {Path.GetFileName(file)}: skipped, {e.Message.Split('\n')[0]}");
                continue;
            }

            var order = OrderOf(image);
            if (order == null) continue;

            entries += order.Entries;
            virtuals += order.Objects;

            if (order.Clean) { clean++; continue; }

            bad++;
            Console.WriteLine($"{Path.GetFileName(file)}: {order.Problem}");
        }

        Console.WriteLine($"\n{clean} file(s) with both tables in the predicted order, {bad} not, " +
                          $"{entries} entr(ies) checked, {virtuals} object(s) whose virtual entries " +
                          "run in file order");
        return bad == 0 ? 0 : 1;
    }

    private static bool IsPackfileCandidate(string virtualPath) =>
        virtualPath.EndsWith(".hkx", StringComparison.OrdinalIgnoreCase);

    private static int PackOrderSweep(string dataFolder, bool serialize)
    {
        if (!Directory.Exists(dataFolder))
        {
            Console.Error.WriteLine($"--data folder not found: {dataFolder}");
            return 1;
        }

        using var data = Mo2ScanData.OpenScanData(dataFolder);

        var candidates = data.EnumerateWinners("", IsPackfileCandidate)
                             .OrderBy(asset => asset.ArchivePath, StringComparer.Ordinal)
                             .ThenBy(asset => asset.VirtualPath, StringComparer.Ordinal)
                             .ToList();

        var archiveSet = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var asset in candidates)
        {
            if (asset.ArchivePath != null) archiveSet.Add(asset.ArchivePath);
            foreach (var loser in asset.Shadowed)
                if (loser.ArchivePath != null) archiveSet.Add(loser.ArchivePath);
        }

        int total = 0, files = 0, skipped = 0, unreadable = 0;
        int clean = 0, orderGlobal = 0, orderLocal = 0, orderVirtual = 0, bad = 0;
        int unchanged = 0, changed = 0, identical = 0, canonicallyIdentical = 0, canonicalBad = 0;
        var failures = new List<string>();

        foreach (var asset in candidates)
        {
            total++;

            if (!data.TryRead(asset, out byte[] bytes, out string? warning))
            {
                unreadable++;
                if (failures.Count < 20) failures.Add($"{asset.VirtualPath}: {warning ?? "unreadable"}");
                continue;
            }

            PackfileImage image;
            Order? order;
            byte[] unedited;
            try
            {
                image = PackfileImage.Read(bytes);
                order = OrderOf(image);
                if (order == null) { skipped++; continue; }
                unedited = image.Rebuild();
            }
            catch (Exception error)
            {
                unreadable++;
                if (failures.Count < 20) failures.Add($"{asset.VirtualPath}: {error.Message}");
                continue;
            }

            files++;
            if (order.Global) orderGlobal++;
            if (order.Local) orderLocal++;
            if (order.Virtual) orderVirtual++;
            if (unedited.AsSpan().SequenceEqual(bytes)) identical++;

            string? problem = order.Problem;
            try { FixupOrder.Reorder(image); }
            catch (Exception error) { problem ??= error.Message; }

            if (problem == null)
            {
                if (image.Rebuild().AsSpan().SequenceEqual(unedited)) unchanged++;
                else
                {
                    changed++;
                    problem = "reordering changed the rebuilt file";
                }
            }

            if (serialize)
            {
                try
                {
                    byte[]? canonical = CanonicalPackfileWriter.Write(PackfileImage.Read(bytes));
                    if (canonical != null && canonical.AsSpan().SequenceEqual(bytes)) canonicallyIdentical++;
                    else { canonicalBad++; problem ??= "canonical re-serialization changed the file"; }
                }
                catch (Exception error) { canonicalBad++; problem ??= error.Message; }
            }

            if (problem == null) { clean++; continue; }

            bad++;
            if (failures.Count < 20) failures.Add($"{asset.VirtualPath}: {problem}");
        }

        foreach (string failure in failures) Console.WriteLine("  " + failure);

        Console.WriteLine($"\n{archiveSet.Count} archive(s): {total} .hkx file(s), {files} checked, " +
                          $"{skipped} skipped for classes this build has no schema for, " +
                          $"{unreadable} unreadable");
        Console.WriteLine($"table order: {orderGlobal} global, {orderLocal} local, {orderVirtual} virtual " +
                          $"of {files} match the walk");
        Console.WriteLine($"reordering left the rebuilt file byte for byte the same in {unchanged} file(s), " +
                          $"{changed} changed, {clean} clean, {bad} with a problem");
        Console.WriteLine($"an unedited rebuild reproduced the original file bytes in {identical} " +
                          $"of {files} file(s)");
        if (serialize)
            Console.WriteLine($"canonical re-serialization reproduced the original file bytes in " +
                              $"{canonicallyIdentical} file(s), {canonicalBad} not");

        return (bad == 0 && unreadable == 0 && canonicalBad == 0) ? 0 : 1;
    }

    private static int PackOrderUsage(string message)
    {
        Console.Error.WriteLine(message);
        Console.Error.WriteLine("usage: symrm walk <hkx-file-or-dir> [--data <Data folder>]");
        return 1;
    }
}
