using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using OpenCommonwealth.Services.Archive;
using OpenCommonwealth.Services.Hkx;

namespace BehaviourStudio.App;

/// <summary>
/// Headless load-order scanners for behaviour/animation defects.
/// </summary>
public static class ModlistScan
{
    public static int Extract(string[] args)
    {
        int i = Array.IndexOf(args, "--extract");
        if (i < 0 || i + 3 >= args.Length)
        {
            Console.Error.WriteLine("usage: --extract <archive.ba2> <substring> <outDir>");
            return 2;
        }

        string archive = args[i + 1];
        string substring = args[i + 2];
        string outDir = args[i + 3];
        if (!File.Exists(archive))
        {
            Console.Error.WriteLine($"no such archive: {archive}");
            return 2;
        }

        try
        {
            Directory.CreateDirectory(outDir);
            int n = Ba2.ExtractMatching(archive, substring, outDir, "", Console.WriteLine, keepFolders: true);
            Console.WriteLine($"extracted {n} entry(ies) matching '{substring}' to {outDir}");
            return n > 0 ? 0 : 1;
        }
        catch (Exception e) when (e is IOException or InvalidDataException or UnauthorizedAccessException or ArgumentException)
        {
            Console.Error.WriteLine("extract failed: " + First(e.Message));
            return 2;
        }
    }

    public static int ScanModlist(string[] args)
    {
        string? instance = After(args, "--scan-modlist");
        if (instance == null)
        {
            Console.Error.WriteLine("--scan-modlist needs an MO2 instance directory");
            return 2;
        }

        if (!Mo2ScanData.TryOpen(instance, out var scan, out string error))
        {
            Console.Error.WriteLine(error);
            return 2;
        }

        using (scan!)
        {
            PrintLayout(scan);
            var characters = scan.ModHkxPaths("characters");
            PrintArchiveWarnings(scan);

            string temp = TempDirectory("bgs-modlist");
            int parsed = 0, broken = 0, missing = 0, unreadable = 0, serial = 0;
            try
            {
                foreach (string dataRel in characters)
                {
                    var read = scan.ReadDataRelative(dataRel);
                    if (read == null)
                    {
                        unreadable++;
                        Console.WriteLine($"UNREADABLE CHARACTER  {dataRel}  (winning copy could not be read)");
                        continue;
                    }

                    int at = FolderIndex(dataRel, "characters");
                    if (at < 0) continue;
                    string projectRel = dataRel[..at];

                    string tmp = Path.Combine(temp, $"{serial++}_{Ba2.FlatFileName(dataRel)}.hkx");
                    try { File.WriteAllBytes(tmp, read.Bytes); }
                    catch (Exception e) when (e is IOException or UnauthorizedAccessException)
                    {
                        unreadable++;
                        Console.WriteLine($"UNREADABLE CHARACTER  {dataRel}: {First(e.Message)}");
                        continue;
                    }

                    List<string> declared;
                    try
                    {
                        string xml = HkxTextEdit.TextOf(tmp);
                        if (xml.Length == 0)
                        {
                            unreadable++;
                            Console.WriteLine($"UNREADABLE CHARACTER  {dataRel}: unsupported HKX class layout");
                            continue;
                        }

                        var model = BehaviourGraphModel.Parse(xml);
                        var csd = model.Objects.FirstOrDefault(o => o.Class == "hkbCharacterStringData");
                        if (csd == null) continue;
                        declared = ProjectChain.DeclaredAnimations(csd);
                    }
                    catch (Exception e)
                    {
                        unreadable++;
                        Console.WriteLine($"UNREADABLE CHARACTER  {dataRel}: {First(e.Message)}");
                        continue;
                    }
                    finally
                    {
                        try { File.Delete(tmp); } catch { }
                    }

                    if (declared.Count == 0) continue;
                    parsed++;

                    var gone = declared
                        .Where(a => !scan.ExistsDataRelative(CombineData(projectRel, a)))
                        .ToList();

                    if (gone.Count == 0) continue;
                    broken++;
                    missing += gone.Count;

                    Console.WriteLine(
                        $"MISSING ANIMATIONS  {dataRel}  [{scan.DescribeSource(dataRel, read)}]  " +
                        $"({gone.Count} of {declared.Count})");
                    foreach (string animation in gone.Take(40)) Console.WriteLine("    " + animation);
                    if (gone.Count > 40) Console.WriteLine($"    ... and {gone.Count - 40} more");
                }
            }
            finally
            {
                TryDeleteDirectory(temp);
            }

            Console.WriteLine(
                $"done  {parsed} winning character file(s) checked, {broken} with missing animations, " +
                $"{missing} missing reference(s), {unreadable} unreadable");
            return missing == 0 && unreadable == 0 ? 0 : 1;
        }
    }

    public static int ScanClips(string[] args)
    {
        string? instance = After(args, "--scan-clips");
        if (instance == null)
        {
            Console.Error.WriteLine("--scan-clips needs an MO2 instance directory");
            return 2;
        }

        if (!Mo2ScanData.TryOpen(instance, out var scan, out string error))
        {
            Console.Error.WriteLine(error);
            return 2;
        }

        using (scan!)
        {
            PrintLayout(scan);
            var behaviours = scan.ModHkxPaths("behaviors");
            PrintArchiveWarnings(scan);

            bool Resolves(string projectRel, string animation)
            {
                string flat = animation.Replace('\\', '/');
                if (scan.ExistsDataRelative(CombineData(projectRel, flat))) return true;

                var parts = flat.Split('/', StringSplitOptions.RemoveEmptyEntries);
                if (parts.Length != 2 ||
                    !parts[0].Equals("Animations", StringComparison.OrdinalIgnoreCase))
                    return false;

                string leaf = parts[1];

                // Prefer the engine-derived weapon search prefixes already maintained by GameData.
                foreach (var set in scan.Data.WeaponTypeSets)
                    foreach (string prefix in set.Prefixes)
                        if (scan.ExistsDataRelative(CombineData(projectRel, prefix + "\\" + leaf)))
                            return true;

                // If Fallout4.esm is unavailable, preserve the static fallback using every
                // weapon folder visible in the merged loose/archive data view.
                foreach (string type in scan.WeaponSubfolders(projectRel))
                    if (scan.ExistsDataRelative(
                        CombineData(projectRel, $"Animations/Weapon/{type}/{leaf}")))
                        return true;

                return false;
            }

            string temp = TempDirectory("bgs-clips");
            int checkedBehaviours = 0, broken = 0, missingClips = 0, unreadable = 0, serial = 0;
            try
            {
                foreach (string dataRel in behaviours)
                {
                    var read = scan.ReadDataRelative(dataRel);
                    if (read == null)
                    {
                        unreadable++;
                        Console.WriteLine($"UNREADABLE BEHAVIOUR  {dataRel}  (winning copy could not be read)");
                        continue;
                    }

                    int at = FolderIndex(dataRel, "behaviors");
                    if (at < 0) continue;
                    string projectRel = dataRel[..at];

                    string tmp = Path.Combine(temp, $"{serial++}_{Ba2.FlatFileName(dataRel)}.hkx");
                    try { File.WriteAllBytes(tmp, read.Bytes); }
                    catch (Exception e) when (e is IOException or UnauthorizedAccessException)
                    {
                        unreadable++;
                        Console.WriteLine($"UNREADABLE BEHAVIOUR  {dataRel}: {First(e.Message)}");
                        continue;
                    }

                    List<(string Clip, string Anim)> clips;
                    try
                    {
                        string xml = HkxTextEdit.TextOf(tmp);
                        if (xml.Length == 0)
                        {
                            unreadable++;
                            Console.WriteLine($"UNREADABLE BEHAVIOUR  {dataRel}: unsupported HKX class layout");
                            continue;
                        }

                        var model = BehaviourGraphModel.Parse(xml);
                        clips = model.Objects
                            .Where(o => o.Class == "hkbClipGenerator")
                            .Select(o => (o.Str("name"), o.Str("animationName")))
                            .Where(c => !string.IsNullOrWhiteSpace(c.Item2))
                            .ToList();
                    }
                    catch (Exception e)
                    {
                        unreadable++;
                        Console.WriteLine($"UNREADABLE BEHAVIOUR  {dataRel}: {First(e.Message)}");
                        continue;
                    }
                    finally
                    {
                        try { File.Delete(tmp); } catch { }
                    }

                    if (clips.Count == 0) continue;
                    checkedBehaviours++;

                    var gone = clips.Where(c => !Resolves(projectRel, c.Anim)).ToList();
                    if (gone.Count == 0) continue;

                    broken++;
                    missingClips += gone.Count;
                    Console.WriteLine(
                        $"MISSING CLIP ANIMATIONS  {dataRel}  [{scan.DescribeSource(dataRel, read)}]  " +
                        $"({gone.Count} clip(s))");
                    foreach (var clip in gone.Take(20))
                        Console.WriteLine($"    clip '{clip.Clip}' plays '{clip.Anim}' - resolves nowhere");
                    if (gone.Count > 20) Console.WriteLine($"    ... and {gone.Count - 20} more");
                }
            }
            finally
            {
                TryDeleteDirectory(temp);
            }

            Console.WriteLine(
                $"done  {checkedBehaviours} winning behaviour file(s) checked, " +
                $"{broken} with unresolved clip animations, {missingClips} clip(s), {unreadable} unreadable");
            return missingClips == 0 && unreadable == 0 ? 0 : 1;
        }
    }

    public static int Run(string[] args)
    {
        string? root = After(args, "--scan-archives");
        if (root == null || !Directory.Exists(root))
        {
            Console.Error.WriteLine("--scan-archives needs an existing directory");
            return 2;
        }

        bool errorsOnly = args.Contains("--errors-only");
        string temp = TempDirectory("bgs-scan");
        int archives = 0, scanned = 0, corrupt = 0, serial = 0;

        try
        {
            var ba2s = Mo2ScanData.EnumerateFilesSafe(root, "*.ba2", recursive: true)
                .OrderBy(p => p, StringComparer.OrdinalIgnoreCase)
                .ToList();
            Console.WriteLine($"scanning {ba2s.Count} .ba2 archive(s) under {root}");

            foreach (string ba2path in ba2s)
            {
                Ba2 archive;
                try { archive = Ba2.Open(ba2path); }
                catch (Exception e) when (e is IOException or InvalidDataException or UnauthorizedAccessException)
                {
                    if (!errorsOnly) Console.WriteLine($"skip     {Rel(root, ba2path)}: {First(e.Message)}");
                    continue;
                }

                using (archive)
                {
                    var entries = archive.Entries.Where(IsBehaviourEntry).ToList();
                    if (entries.Count == 0) continue;
                    archives++;

                    foreach (var entry in entries)
                    {
                        scanned++;
                        string tmp = Path.Combine(temp, $"{serial++}_{Ba2.FlatFileName(entry.Name)}");
                        try { File.WriteAllBytes(tmp, archive.Read(entry)); }
                        catch (Exception e)
                        {
                            corrupt++;
                            Console.WriteLine(
                                $"UNREADABLE  {Rel(root, ba2path)} :: {entry.Name}: {First(e.Message)}");
                            continue;
                        }

                        var errors = CheckBehaviour(tmp);
                        try { File.Delete(tmp); } catch { }
                        if (errors.Count == 0) continue;

                        corrupt++;
                        Console.WriteLine($"CORRUPT  {Rel(root, ba2path)} :: {entry.Name}");
                        foreach (string finding in errors) Console.WriteLine("    " + finding);
                    }
                }
            }

            var loose = Mo2ScanData.EnumerateFilesSafe(root, "*.hkx", recursive: true)
                .Where(p => IsBehaviourPath(p.Replace('\\', '/')))
                .OrderBy(p => p, StringComparer.OrdinalIgnoreCase)
                .ToList();
            Console.WriteLine($"scanning {loose.Count} loose behaviour/character .hkx");

            foreach (string path in loose)
            {
                scanned++;
                var errors = CheckBehaviour(path);
                if (errors.Count == 0) continue;
                corrupt++;
                Console.WriteLine($"CORRUPT  {Rel(root, path)}  (loose)");
                foreach (string finding in errors) Console.WriteLine("    " + finding);
            }
        }
        finally
        {
            TryDeleteDirectory(temp);
        }

        Console.WriteLine(
            $"done  {scanned} behaviour/character hkx scanned across {archives} archive(s) + loose, " +
            $"{corrupt} with errors");
        return corrupt == 0 ? 0 : 1;
    }

    private static void PrintLayout(Mo2ScanData scan)
    {
        string profile = scan.Info.Profile ?? "(root modlist)";
        Console.WriteLine(
            $"profile {profile}; {scan.ModRootsHighToLow.Count} active mod root(s); " +
            $"Data {scan.Info.GameDataFolder}");
        if (Directory.Exists(scan.Info.OverwriteFolder))
            Console.WriteLine($"overwrite {scan.Info.OverwriteFolder}");
    }

    private static void PrintArchiveWarnings(Mo2ScanData scan)
    {
        foreach (string warning in scan.DrainArchiveWarnings())
            Console.WriteLine("ARCHIVE WARNING  " + warning);
    }

    private static string CombineData(string projectRel, string declared)
    {
        string left = projectRel.Replace('\\', '/').Trim('/');
        string right = declared.Replace('\\', '/').TrimStart('/');
        return left.Length == 0 ? right : left + "/" + right;
    }

    private static int FolderIndex(string dataRelative, string folder)
    {
        string flat = dataRelative.Replace('\\', '/');
        string needle = "/" + folder.Trim('/', '\\') + "/";
        int at = flat.IndexOf(needle, StringComparison.OrdinalIgnoreCase);
        if (at >= 0) return at;

        string prefix = folder.Trim('/', '\\') + "/";
        return flat.StartsWith(prefix, StringComparison.OrdinalIgnoreCase) ? 0 : -1;
    }

    private static bool IsBehaviourEntry(Ba2.Entry entry) =>
        entry.Name.EndsWith(".hkx", StringComparison.OrdinalIgnoreCase) &&
        IsBehaviourPath(entry.Name);

    private static bool IsBehaviourPath(string name)
    {
        string flat = "/" + name.Replace('\\', '/').Trim('/') + "/";
        return flat.Contains("/behaviors/", StringComparison.OrdinalIgnoreCase) ||
               flat.Contains("/characters/", StringComparison.OrdinalIgnoreCase) ||
               flat.Contains("/characterassets/", StringComparison.OrdinalIgnoreCase);
    }

    private static List<string> CheckBehaviour(string path)
    {
        var errors = new List<string>();
        try
        {
            if (!HkxBinaryReader.IsFo4Hkx(path)) return errors;
            string xml = HkxTextEdit.TextOf(path);
            if (xml.Length == 0) return errors;
            foreach (var finding in GraphValidator.Check(xml))
                if (finding.Level == GraphValidator.Level.Error)
                    errors.Add(finding.ToString());
        }
        catch (Exception e)
        {
            errors.Add("parse threw: " + First(e.Message));
        }
        return errors;
    }

    private static string TempDirectory(string prefix)
    {
        string path = Path.Combine(Path.GetTempPath(), prefix + "-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(path);
        return path;
    }

    private static void TryDeleteDirectory(string path)
    {
        try { Directory.Delete(path, recursive: true); }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException) { }
    }

    private static string? After(string[] args, string flag)
    {
        int i = Array.IndexOf(args, flag);
        return i >= 0 && i + 1 < args.Length ? args[i + 1] : null;
    }

    private static string Rel(string root, string path)
    {
        try { return Path.GetRelativePath(root, path); }
        catch { return path; }
    }

    private static string First(string message) =>
        message.Split('\n', StringSplitOptions.RemoveEmptyEntries).FirstOrDefault()?.Trim() ?? message.Trim();
}
