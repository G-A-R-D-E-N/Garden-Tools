using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.CompilerServices;
using BehaviourStudio.App;

namespace BehaviourStudio.UiSmoke;

internal static class SettingsRetentionSmoke
{
    [ModuleInitializer]
    internal static void Run()
    {
        string root = Directory.CreateTempSubdirectory("bgs-settings-retention").FullName;
        string settingsPath = Path.Combine(root, "settings.cfg");
        string? previousPath = Settings.SettingsPathForTest;
        Func<string, string[]> previousReader = Settings.ReadAllLinesForTest;

        try
        {
            Settings.SettingsPathForTest = settingsPath;
            Settings.ReadAllLinesForTest = File.ReadAllLines;
            Settings.Set("ordinary-preference", "survives");

            var point = new Dictionary<string, Settings.LayoutPoint>(StringComparer.Ordinal)
            {
                ["#1"] = new Settings.LayoutPoint(10, 20),
            };

            var paths = Enumerable.Range(0, Settings.MaxRememberedGraphLayouts)
                .Select(i => Path.Combine(root, $"graph-{i}.hkx"))
                .ToArray();

            foreach (string path in paths)
                Require(Settings.TrySetGraphLayout(path, point, out string failure), failure);

            Require(Settings.TrySetGraphLayout(paths[0], point, out string refreshFailure), refreshFailure);
            string newest = Path.Combine(root, "graph-newest.hkx");
            Require(Settings.TrySetGraphLayout(newest, point, out string newestFailure), newestFailure);

            string[] lines = File.ReadAllLines(settingsPath);
            int remembered = lines.Count(line =>
                line.StartsWith("graph-layout.", StringComparison.Ordinal));

            Require(remembered == Settings.MaxRememberedGraphLayouts,
                $"expected {Settings.MaxRememberedGraphLayouts} graph layouts, found {remembered}");
            Require(Settings.GetGraphLayout(paths[0]).Count == 1,
                "the refreshed oldest graph layout was pruned");
            Require(Settings.GetGraphLayout(paths[1]).Count == 0,
                "the least-recently-used graph layout was retained");
            Require(Settings.GetGraphLayout(newest).Count == 1,
                "the newest graph layout was not retained");
            Require(Settings.Get("ordinary-preference") == "survives",
                "pruning graph layouts removed an unrelated preference");

            Console.WriteLine("settings retention smoke passed");
        }
        finally
        {
            Settings.SettingsPathForTest = previousPath;
            Settings.ReadAllLinesForTest = previousReader;
            try { Directory.Delete(root, true); }
            catch (Exception e) when (e is IOException or UnauthorizedAccessException) { }
        }
    }

    internal static void WriteChecks()
    {
        SettingsWriteFailureLeavesExistingFileUntouched();
        ConcurrentSettingsWritesPreserveAllValues();
    }

    private static void SettingsWriteFailureLeavesExistingFileUntouched()
    {
        Console.WriteLine("\na failed settings write leaves the existing file untouched");
        string root = Directory.CreateTempSubdirectory("bgs-settings-failure").FullName;
        try
        {
            string final = Path.Combine(root, "settings.cfg");
            File.WriteAllText(final, "old=value\n");

            string blocked = Path.Combine(root, "blocked");
            File.WriteAllText(blocked, "not a directory");
            string temp = Path.Combine(blocked, "settings.cfg.tmp");

            bool refused = false;
            try
            {
                Settings.WriteAll(temp, final,
                    new Dictionary<string, string>(StringComparer.Ordinal) { ["new"] = "value" });
            }
            catch (IOException)
            {
                refused = true;
            }

            Smoke.CheckTrue("the settings write failure is surfaced", refused);
            Smoke.Check("the previous settings file is not replaced", "old=value\n", File.ReadAllText(final));
        }
        finally
        {
            Directory.Delete(root, true);
        }
    }

    private static void ConcurrentSettingsWritesPreserveAllValues()
    {
        Console.WriteLine("\nconcurrent settings writes preserve every preference");
        string root = Directory.CreateTempSubdirectory("bgs-settings-concurrent").FullName;
        string path = Path.Combine(root, "settings.cfg");
        string? previous = Settings.SettingsPathForTest;
        try
        {
            Settings.SettingsPathForTest = path;
            Settings.ReadAllLinesForTest = File.ReadAllLines;
            System.Threading.Tasks.Parallel.For(0, 16, i => Settings.Set("key" + i, i.ToString()));

            var lines = File.ReadAllLines(path);
            Smoke.Check("all concurrent settings writes survive", 16, lines.Length);
            Smoke.CheckTrue("every concurrent key is present",
                      Enumerable.Range(0, 16).All(i => lines.Contains($"key{i}={i}")));
            Smoke.Check("unique staging leaves no temp files", 0,
                  Directory.EnumerateFiles(root, "*.tmp").Count());
        }
        finally
        {
            Settings.SettingsPathForTest = previous;
            Settings.ReadAllLinesForTest = File.ReadAllLines;
            Directory.Delete(root, true);
        }
    }

    private static void Require(bool condition, string failure)
    {
        if (!condition) throw new InvalidOperationException("settings retention smoke failed: " + failure);
    }
}
