using System;
using System.IO;
using System.Linq;
using Avalonia.Threading;
using BehaviourStudio.App;
using OpenCommonwealth.Services.Hkx;

namespace BehaviourStudio.UiSmoke;

public static class AnimationGeometrySmoke
{
    public static int Run()
    {
        Console.WriteLine("\nthe Animation tab trims, retimes and loops a clip through the verified save path");

        string fixture = Path.Combine(AppContext.BaseDirectory, "fixtures", "RadRoachTurnLeft90.hkx");
        if (!File.Exists(fixture))
        {
            Console.WriteLine($"        animation geometry: skipped, {fixture} is not here");
            return 0;
        }

        string root = Directory.CreateTempSubdirectory("bgs-animation-geometry").FullName;
        string? settings = Settings.SettingsPathForTest;
        string settingsPath = Path.Combine(Path.GetTempPath(), $"bgs-geometry-{Guid.NewGuid():N}.cfg");
        Settings.SettingsPathForTest = settingsPath;
        Settings.TrySet("tour_done", "1", out _);

        int failed = 0;
        try
        {
            TrimAndSave(fixture, root, ref failed);
            RetimeAndSave(fixture, root, ref failed);
            LoopAndSave(fixture, root, ref failed);
            UnsupportedKeepsItsMessage(root, ref failed);
        }
        finally
        {
            Settings.SettingsPathForTest = settings;
            File.Delete(settingsPath);
            try { Directory.Delete(root, true); }
            catch (Exception e) when (e is IOException or UnauthorizedAccessException) { }
        }

        Console.WriteLine($"        animation geometry: {failed} failure(s)");
        return failed;
    }

    private static void TrimAndSave(string fixture, string root, ref int failed)
    {
        string path = Fresh(fixture, root, "trim.hkx");
        var window = new MainWindow();
        window.Show();
        window.Open(path);
        Dispatcher.UIThread.RunJobs();
        Smoke.SelectTab(window, "Animation");

        Check(ref failed, "the clip opens with nineteen frames", window.AnimationFrameCount == 19);
        Check(ref failed, "and its four annotations", window.AnimationAnnotationCount == 4);
        Check(ref failed, "with nothing edited yet", !window.AnimationEdited);

        window.TypeTrimStart("4");
        window.TypeTrimEnd("14");
        window.PreviewTrimForTest();
        Console.WriteLine("        " + window.GeometryAnswer);
        Check(ref failed, "a preview counts the annotation a trim would drop",
              window.GeometryAnswer.Contains("1 annotation(s) dropped", StringComparison.Ordinal));
        Check(ref failed, "and leaves the clip alone",
              window.AnimationFrameCount == 19 && !window.AnimationEdited);

        window.ApplyTrimForTest();
        Console.WriteLine("        " + window.GeometryAnswer);
        Check(ref failed, "the trim keeps eleven frames", window.AnimationFrameCount == 11);
        Check(ref failed, "of 0.333s", Math.Abs(window.AnimationDuration - 1f / 3f) < 1e-3);
        Check(ref failed, "and three annotations", window.AnimationAnnotationCount == 3);
        Check(ref failed, "leaving the clip dirty with a timeline to write", window.AnimationEdited && window.AnimationHasTimeline);
        Check(ref failed, "and the answer warns there is no undo and the .bak is the way back",
              window.GeometryAnswer.Contains("No undo", StringComparison.Ordinal) &&
              window.GeometryAnswer.Contains(".bak", StringComparison.Ordinal));

        bool saved = window.SaveAnimationForTest();
        Dispatcher.UIThread.RunJobs();
        Console.WriteLine("        " + window.FrameEditAnswer);
        Check(ref failed, "the trim saves and nothing is left unsaved", saved && !window.AnimationEdited);

        var written = new HkxBinaryReader().ReadAnimation(path);
        Check(ref failed, "the file on disk holds eleven frames", written.NumFrames == 11);
        Check(ref failed, "at the trimmed length", Math.Abs(written.Duration - 1f / 3f) < 1e-3);
        Check(ref failed, "with three annotations", written.Annotations.Count == 3);
        Check(ref failed, "keeping the text of the ones that lived inside the cut",
              written.Annotations.Any(a => a.Text == "FootRight") &&
              written.Annotations.Count(a => a.Text == "FootLeft") == 2);
        Check(ref failed, "and every one of them inside the clip",
              written.Annotations.All(a => a.Time >= 0f && a.Time <= written.Duration + 1f / 60f + 1e-4f));
        Check(ref failed, "the clip comes back spline compressed",
              written.AnimationClass == NativeAnimation.SplineClass);
        Check(ref failed, "and the original is kept beside it", File.Exists(path + ".bak"));

        Smoke.CloseForTest(window);
    }

    private static void RetimeAndSave(string fixture, string root, ref int failed)
    {
        string path = Fresh(fixture, root, "retime.hkx");
        var window = new MainWindow();
        window.Show();
        window.Open(path);
        Dispatcher.UIThread.RunJobs();
        Smoke.SelectTab(window, "Animation");

        window.TypeRetimeSpeed("2");
        window.PreviewRetimeForTest();
        Console.WriteLine("        " + window.GeometryAnswer);
        Check(ref failed, "a retime preview says the frame count it would reach",
              window.GeometryAnswer.Contains("37 frame(s)", StringComparison.Ordinal));
        Check(ref failed, "and leaves the clip alone",
              window.AnimationFrameCount == 19 && !window.AnimationEdited);

        window.ApplyRetimeForTest();
        Console.WriteLine("        " + window.GeometryAnswer);
        Check(ref failed, "twice as long is thirty-seven frames", window.AnimationFrameCount == 37);
        Check(ref failed, "over 1.2s", Math.Abs(window.AnimationDuration - 1.2) < 1e-3);
        Check(ref failed, "and the annotations scale with it", window.AnimationAnnotationCount == 4);
        Check(ref failed, "with the retime and loop boxes cleared so Enter cannot compound it",
              window.RetimeSpeedText.Length == 0 && window.RetimeDurationText.Length == 0 &&
              window.LoopBlendText.Length == 0);

        bool saved = window.SaveAnimationForTest();
        Dispatcher.UIThread.RunJobs();
        Console.WriteLine("        " + window.FrameEditAnswer);
        Check(ref failed, "the retimed clip saves", saved && !window.AnimationEdited);

        var written = new HkxBinaryReader().ReadAnimation(path);
        Check(ref failed, "the file on disk holds thirty-seven frames", written.NumFrames == 37);
        Check(ref failed, "over 1.2s", Math.Abs(written.Duration - 1.2) < 1e-3);
        Check(ref failed, "with the annotations moved with it", written.Annotations.Count == 4);
        Check(ref failed, "and none of them past the new end",
              written.Annotations.All(a => a.Time >= 0f && a.Time <= written.Duration + 1f / 60f + 1e-4f));

        Smoke.CloseForTest(window);
    }

    private static void LoopAndSave(string fixture, string root, ref int failed)
    {
        string path = Fresh(fixture, root, "loop.hkx");
        var window = new MainWindow();
        window.Show();
        window.Open(path);
        Dispatcher.UIThread.RunJobs();
        Smoke.SelectTab(window, "Animation");

        var travel = RootMotion.Read(path);

        window.TypeLoopBlend("1");
        window.PreviewLoopForTest();
        Console.WriteLine("        " + window.GeometryAnswer);
        Check(ref failed, "a loop preview says the last frame would match the first",
              window.GeometryAnswer.Contains("matches the first", StringComparison.Ordinal));
        Check(ref failed, "and says the seam holds that pose for one frame",
              window.GeometryAnswer.Contains("holds that pose for one frame", StringComparison.Ordinal));
        Check(ref failed, "and leaves the clip alone", !window.AnimationEdited);

        window.ApplyLoopForTest();
        Check(ref failed, "the loop leaves the frame count alone", window.AnimationFrameCount == 19);

        float drift = 0;
        for (int t = 0; t < window.AnimationTrackCount; t++)
            drift = Math.Max(drift, (window.FramePosition(t, 18) - window.FramePosition(t, 0)).Length());
        Check(ref failed, $"and moves the last frame onto the first ({drift:F5})", drift < 1e-3f);

        bool saved = window.SaveAnimationForTest();
        Dispatcher.UIThread.RunJobs();
        Console.WriteLine("        " + window.FrameEditAnswer);
        Check(ref failed, "the looped clip saves", saved && !window.AnimationEdited);

        var written = new HkxBinaryReader().ReadAnimation(path);
        Check(ref failed, "the file on disk keeps nineteen frames", written.NumFrames == 19);
        Check(ref failed, "and its 0.6s length", Math.Abs(written.Duration - 0.6) < 1e-3);

        float landed = 0;
        for (int t = 0; t < written.Tracks.Count; t++)
            landed = Math.Max(landed,
                (written.Tracks[t].Translations[^1] - written.Tracks[t].Translations[0]).Length());
        Check(ref failed, $"and the last frame comes back on the first ({landed:F5})", landed < 0.05f);

        var kept = RootMotion.Read(path);
        Check(ref failed, "with the travel it came in with",
              kept.Samples.Count == travel.Samples.Count &&
              Math.Abs(kept.Turn - travel.Turn) < 1e-4f &&
              (kept.Travel - travel.Travel).Length() < 1e-4f);

        Smoke.CloseForTest(window);
    }

    private static void UnsupportedKeepsItsMessage(string root, ref int failed)
    {
        string path = Path.Combine(root, "no-animation.hkx");
        File.WriteAllBytes(path, Smoke.OneMachineBytes());

        var window = new MainWindow();
        window.Show();
        window.Open(path);
        Dispatcher.UIThread.RunJobs();
        Smoke.SelectTab(window, "Animation");

        string summary = window.AnimationSummary;
        Check(ref failed, "a file with no frames says so on the Animation tab", summary.Length > 0);

        window.ApplyTrimForTest();
        Console.WriteLine("        " + window.GeometryAnswer);
        Check(ref failed, "and trimming it keeps that same message", window.GeometryAnswer == summary);
        Check(ref failed, "without making anything editable",
              !window.AnimationEdited && window.AnimationFrameCount == 0);

        Smoke.CloseForTest(window);
    }

    private static string Fresh(string fixture, string root, string name)
    {
        string path = Path.Combine(root, name);
        File.Copy(fixture, path, true);
        File.Delete(path + ".bak");
        return path;
    }

    private static void Check(ref int failed, string what, bool ok)
    {
        if (!ok) failed++;
        Console.WriteLine($"  {(ok ? "ok  " : "FAIL")}  {what}");
    }
}
