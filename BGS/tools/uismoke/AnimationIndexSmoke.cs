using System;
using System.IO;
using BehaviourStudio.App;

namespace BehaviourStudio.UiSmoke;

public static class AnimationIndexSmoke
{
    public static int Run()
    {
        int before = Smoke.FailedCount;
        string settingsPath = Path.Combine(Path.GetTempPath(), $"bgs-index-ui-{Guid.NewGuid():N}.cfg");
        string? previous = Settings.SettingsPathForTest;
        string root = Directory.CreateTempSubdirectory("bgs-index-ui-").FullName;
        try
        {
            Settings.SettingsPathForTest = settingsPath;
            File.WriteAllText(settingsPath, "");
            Settings.TrySet("tour_done", "1", out _);

            Console.WriteLine("\nthe Animations tab indexes loose files, archives and mod folders, then filters them");
            string data = Path.Combine(root, "Data");
            string instance = Path.Combine(root, "MO2");
            Write(data, "Meshes/Actors/Character/Animations/Standalone.hkx", new byte[] { 1, 2, 3 });
            Write(data, "Meshes/Actors/Character/Behaviors/LooseBehaviour.hkx", new byte[] { 4, 5, 6 });
            Write(Path.Combine(instance, "mods", "AttackMod"), "Meshes/Actors/Character/Animations/ModAttack.hkx",
                  new byte[] { 7, 7 });
            File.WriteAllText(Path.Combine(instance, "modlist.txt"), "+AttackMod\n");

            bool fixture = CopyFixture("Meshes/Actors/Character/Behaviors/RaiderRootBehavior.hkx",
                Path.Combine(data, "Meshes", "Actors", "Character", "Behaviors", "RaiderRootBehavior.hkx"));
            int expected = fixture ? 6 : 5;
            int behaviours = fixture ? 2 : 1;

            BehaviourStudio.Tests.TestArchives.WriteGeneralBa2(Path.Combine(data, "Fallout4 - Animations.ba2"), new[]
            {
                ("Meshes/Actors/Character/Animations/Packed.hkx", new byte[] { 8, 8 }),
                ("Meshes/Actors/RadRoach/Characters/RadRoachCharacter.hkx", new byte[] { 9, 9, 9 }),
                ("Meshes/Textures/Noise.dds", new byte[] { 0 }),
            });

            Settings.TrySet("gameDataFolder", data, out _);
            Settings.TrySet("gameModsFolder", instance, out _);

            var window = new MainWindow();
            window.Show();
            try
            {
                Smoke.SelectTab(window, "Animations");
                Smoke.Check("the Animations tab starts with no index", 0, window.IndexEntryCount);
                Smoke.Check("and says so before a build", "no index built yet", window.IndexSummary);

                window.IndexNowForTest(Path.Combine(root, "cache.json"));
                Avalonia.Threading.Dispatcher.UIThread.RunJobs();

                Smoke.Check("the index counts every loose, mod and archive entry", expected, window.IndexEntryCount);
                Smoke.Check("the summary reports the whole index", $"{expected} entries", window.IndexSummary);
                Smoke.Check("the load order is named", "Data + 1 mod root(s)", window.IndexSourceSummary);
                Smoke.Check("the grid groups the entries under one heading per kind", 3,
                            window.IndexGrid.BodyTree.ItemCount);
                Smoke.Check("every entry hangs under a heading", expected + 3, window.IndexGrid.RowCount);
                Smoke.CheckTrue("the build report names what was scanned",
                    window.IndexProgressText.Contains("scanned", StringComparison.Ordinal) &&
                    window.IndexProgressText.Contains("entries", StringComparison.Ordinal));

                window.IndexSearchField.Text = "packed";
                Avalonia.Threading.Dispatcher.UIThread.RunJobs();
                Smoke.Check("search narrows the list", 1, window.IndexShownCount);
                Smoke.Check("and the summary counts what is shown", $"1 of {expected} entries", window.IndexSummary);

                window.IndexSearchField.Text = "radroach character";
                Avalonia.Threading.Dispatcher.UIThread.RunJobs();
                Smoke.Check("every word must match, in any order", 1, window.IndexShownCount);

                window.IndexSearchField.Text = "nothing-like-this";
                Avalonia.Threading.Dispatcher.UIThread.RunJobs();
                Smoke.Check("nonsense matches nothing", 0, window.IndexShownCount);
                Smoke.Check("and every entry is still indexed", expected, window.IndexEntryCount);

                window.IndexSearchField.Text = "";
                Avalonia.Threading.Dispatcher.UIThread.RunJobs();
                Smoke.Check("clearing the search shows every entry again", expected, window.IndexShownCount);

                Smoke.CheckTrue("the kind filter names the kinds in the index",
                    window.SelectIndexKindForTest("behaviour"));
                Avalonia.Threading.Dispatcher.UIThread.RunJobs();
                Smoke.Check("the kind filter narrows to behaviours", behaviours, window.IndexShownCount);
                Smoke.CheckTrue("and releases the list again", window.SelectIndexKindForTest("all kinds"));
                Avalonia.Threading.Dispatcher.UIThread.RunJobs();
                Smoke.Check("with the filter cleared every entry is back", expected, window.IndexShownCount);

                Smoke.CheckTrue("a loose entry can be picked from the browser",
                    window.SelectIndexEntryForTest("Meshes/Actors/Character/Animations/Standalone.hkx"));
                window.OpenIndexSelectionForTest();
                Avalonia.Threading.Dispatcher.UIThread.RunJobs();
                Smoke.CheckTrue("opening a loose entry feeds the existing open path",
                    window.PathFieldForTest.EndsWith("Standalone.hkx", StringComparison.OrdinalIgnoreCase));

                Smoke.CheckTrue("an archive entry can be picked from the browser",
                    window.SelectIndexEntryForTest("Meshes/Actors/Character/Animations/Packed.hkx"));
                window.OpenIndexSelectionForTest();
                Avalonia.Threading.Dispatcher.UIThread.RunJobs();
                Smoke.CheckTrue("opening an archive entry extracts it for the existing open path",
                    window.PathFieldForTest.EndsWith("Meshes_Actors_Character_Animations_Packed.hkx",
                                                      StringComparison.OrdinalIgnoreCase) &&
                    File.Exists(window.PathFieldForTest));
            }
            finally
            {
                Smoke.CloseForTest(window);
            }
        }
        finally
        {
            Settings.SettingsPathForTest = previous;
            try { File.Delete(settingsPath); } catch (IOException) { } catch (UnauthorizedAccessException) { }
            try { Directory.Delete(root, true); } catch (IOException) { } catch (UnauthorizedAccessException) { }
        }

        return Smoke.FailedCount == before ? 0 : 1;
    }

    private static void Write(string root, string relative, byte[] bytes)
    {
        string path = Path.Combine(root, relative.Replace('/', Path.DirectorySeparatorChar));
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        File.WriteAllBytes(path, bytes);
    }

    private static bool CopyFixture(string relative, string target)
    {
        string[] candidates =
        {
            Path.Combine("tools", "tests", "fixtures", "vanilla", relative.Replace('/', Path.DirectorySeparatorChar)),
            Path.Combine(AppContext.BaseDirectory, "fixtures", "vanilla",
                         relative.Replace('/', Path.DirectorySeparatorChar)),
        };

        foreach (string candidate in candidates)
        {
            if (!File.Exists(candidate)) continue;
            Directory.CreateDirectory(Path.GetDirectoryName(target)!);
            File.Copy(candidate, target, overwrite: true);
            return true;
        }
        Console.WriteLine($"        animation index: the {relative} fixture is not here, so that entry is skipped");
        return false;
    }
}
