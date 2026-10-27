using System;
using System.IO;
using System.Linq;
using Avalonia.Threading;
using BehaviourStudio.App;
using OpenCommonwealth.Services.Archive;

namespace BehaviourStudio.UiSmoke;

public static partial class Smoke
{
    private static void PackedAnimationOpensFromIndexAndArchive()
    {
        CancelledArchiveOpenKeepsTheEditedDocumentWritable();

        Console.WriteLine("\na packed animation opened from the Animations index or the archive browser is posable");

        string samples = System.IO.Path.Combine(AppContext.BaseDirectory, "samples");
        string animationSample = System.IO.Path.Combine(samples, "TurretIdleWeapReady.hkx");
        string skeletonSample = System.IO.Path.Combine(samples, "TurretStandingSkeleton.hkx");
        if (!System.IO.File.Exists(animationSample) || !System.IO.File.Exists(skeletonSample))
        {
            Console.WriteLine("        packed-open: skipped, the genuine samples are not here");
            return;
        }

        const string virtualPath = "Meshes/Actors/Turret/Animations/Mounted/Idle_WeapReady.hkx";
        const string skeletonPath = "Meshes/Actors/Turret/CharacterAssets/Skeleton.hkx";
        string root = System.IO.Path.Combine(System.IO.Path.GetTempPath(), $"bgs-packed-open-{Guid.NewGuid():N}");
        string data = System.IO.Path.Combine(root, "Data");
        string archive = System.IO.Path.Combine(data, "Fallout4 - Animations.ba2");
        System.IO.Directory.CreateDirectory(data);

        try
        {
            WriteArchive(archive, new[]
            {
                (virtualPath, System.IO.File.ReadAllBytes(animationSample)),
                (skeletonPath, System.IO.File.ReadAllBytes(skeletonSample)),
            });

            WithTemporarySettings(settingsPath =>
            {
                Settings.TrySet("gameDataFolder", data, out _);
                Settings.TrySet("gameModsFolder", "", out _);

                var window = new MainWindow();
                window.Show();
                SelectTab(window, "Animations");
                window.IndexNowForTest(System.IO.Path.Combine(root, "cache.json"));
                Dispatcher.UIThread.RunJobs();

                CheckTrue("the index lists the packed animation", window.IndexEntryCount > 0);
                using (GameData probe = GameData.Discover(data))
                {
                    Console.WriteLine($"        game data sees {probe.ArchivePaths.Count} archive(s) and the " +
                                      $"skeleton entry {probe.SkeletonEntryName(virtualPath) ?? "none"}");
                }

                CheckTrue("building the index opens nothing by itself",
                          window.PoseSourceForTest.Length == 0 && window.PoseFrameCount == 0 &&
                          window.LoadedXml.Length == 0 && window.AnimationFrameCount == 0);
                CheckTrue("and leaves the path field untouched",
                          !window.PathFieldForTest.Contains("Idle_WeapReady", StringComparison.OrdinalIgnoreCase));

                CheckTrue("the packed animation can be picked in the browser",
                          window.SelectIndexEntryForTest(virtualPath));
                window.OpenIndexSelectionForTest();
                Dispatcher.UIThread.RunJobs();

                SelectTab(window, "Playback");
                CheckTrue("opening it from the index poses the packed animation", window.PoseAnimationLoaded);
                Console.WriteLine("        " + window.PlaybackSummary);
                SelectTab(window, "Animation");
                CheckTrue("the Animation tab names the packed sibling skeleton",
                          window.AnimationSummary.Contains("sibling skeleton", StringComparison.Ordinal) &&
                          !window.AnimationSummary.Contains("no sibling skeleton", StringComparison.Ordinal));
                CheckTrue("a read only archive copy cannot be saved",
                          !window.SaveAnimationButtonEnabled && !window.AnimationEdited);
                PlayAndStep(window, "index");
                CloseForTest(window);

                var browser = new MainWindow();
                browser.Show();
                CheckTrue("the archive browser opens the same entry",
                          browser.OpenArchiveForTest(archive, virtualPath));
                Dispatcher.UIThread.RunJobs();
                SelectTab(browser, "Playback");
                CheckTrue("the From archive path poses it too", browser.PoseAnimationLoaded);
                Console.WriteLine("        " + browser.PlaybackSummary);
                SelectTab(browser, "Animation");
                CheckTrue("and the Animation tab finds the packed sibling skeleton there as well",
                          browser.AnimationSummary.Contains("sibling skeleton", StringComparison.Ordinal) &&
                          !browser.AnimationSummary.Contains("no sibling skeleton", StringComparison.Ordinal));
                CheckTrue("with the save button honest about the read only copy",
                          !browser.SaveAnimationButtonEnabled && !browser.AnimationEdited);
                PlayAndStep(browser, "archive browser");
                CloseForTest(browser);
            });
        }
        finally
        {
            try { System.IO.Directory.Delete(root, true); }
            catch (Exception e) when (e is System.IO.IOException or UnauthorizedAccessException) { }
        }
    }

    private static void CancelledArchiveOpenKeepsTheEditedDocumentWritable()
    {
        Console.WriteLine("\na cancelled archive open leaves the unsaved edit writable");
        const string entryPath = "Behaviors/CancelledOpen.hkx";
        string root = System.IO.Path.Combine(System.IO.Path.GetTempPath(), $"bgs-cancelled-open-{Guid.NewGuid():N}");
        System.IO.Directory.CreateDirectory(root);

        try
        {
            string loose = WriteOneClip("bgs-smoke-cancelled-open");
            string archive = System.IO.Path.Combine(root, "Fallout4 - Cancelled.ba2");
            WriteArchive(archive, new[] { (entryPath, System.IO.File.ReadAllBytes(loose)) });

            var window = new MainWindow();
            window.Show();
            window.Open(loose);
            Dispatcher.UIThread.RunJobs();
            window.SetXmlForTest(OpenCommonwealth.Services.Hkx.HkxTextEdit.SetParam(
                window.LoadedXml, "90", "playbackSpeed", "0.8125"));

            int asked = 0;
            window.DiscardDecision = () =>
            {
                asked++;
                return DiscardChoice.Cancel;
            };
            window.OpenArchiveForTest(archive, entryPath);
            Dispatcher.UIThread.RunJobs();
            CheckTrue("the cancelled archive open asked about the unsaved edit", asked == 1);
            CheckTrue("and the edit is still pending", window.IsDirty);

            window.SaveForTest();
            Dispatcher.UIThread.RunJobs();
            CheckTrue("Save still writes the edit to the document that stayed open",
                      OpenCommonwealth.Services.Hkx.HkxTextEdit.TextOf(loose).Contains("0.8125", StringComparison.Ordinal));
            CloseForTest(window);
        }
        finally
        {
            try { System.IO.Directory.Delete(root, true); }
            catch (Exception e) when (e is System.IO.IOException or UnauthorizedAccessException) { }
        }
    }

    private static void PlayAndStep(MainWindow window, string where)
    {
        int frames = window.PoseFrameCount;
        CheckTrue($"the {where} open reports more than one frame ({frames})", frames > 1);
        CheckTrue($"the {where} transport is enabled", window.TransportEnabledForTest);
        CheckTrue($"the {where} step button is enabled", window.StepButtonsEnabledForTest);

        window.ScrubTo(0);
        window.PlayForTest();
        CheckTrue($"the {where} Play sets IsPlaying", window.IsPlaying);
        window.PlayForTest();
        CheckTrue($"and pressing it again stops the clock", !window.IsPlaying);

        window.ScrubTo(0);
        window.StepForwardForTest();
        Check($"the {where} step moves the frame", 1, window.PoseFrame);
        CheckTrue($"and the pose follows the step", window.PoseNow != null);
    }
}
