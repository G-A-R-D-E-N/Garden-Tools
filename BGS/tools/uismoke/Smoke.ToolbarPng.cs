using System;
using Avalonia;
using Avalonia.Headless;
using BehaviourStudio.App;

namespace BehaviourStudio.UiSmoke;

public static partial class Smoke
{
    internal static int ToolbarPngForTest(string[] args)
    {
        AppBuilder.Configure<HeadlessApp>()
            .UseSkia()
            .UseHeadless(new AvaloniaHeadlessPlatformOptions { UseHeadlessDrawing = false })
            .SetupWithoutStarting();

        string output = args.Length > 1 && !args[1].StartsWith("--")
            ? args[1] : System.IO.Path.Combine(System.IO.Path.GetTempPath(), "bgs-toolbars");
        System.IO.Directory.CreateDirectory(output);

        Settings.SettingsPathForTest =
            System.IO.Path.Combine(System.IO.Path.GetTempPath(), $"bgs-shot-{Guid.NewGuid():N}.cfg");
        Settings.TrySet("tour_done", "1", out _);

        var window = new MainWindow();
        window.Width = 1280;
        window.Height = 860;
        window.Show();
        Avalonia.Threading.Dispatcher.UIThread.RunJobs();

        string fixture = System.IO.Path.Combine(AppContext.BaseDirectory, "fixtures", "RadRoachTurnLeft90.hkx");
        if (System.IO.File.Exists(fixture)) window.Open(fixture);
        Avalonia.Threading.Dispatcher.UIThread.RunJobs();

        string animation = System.IO.Path.Combine(output, "ui-animation-toolbar.png");
        string playback = System.IO.Path.Combine(output, "ui-playback-toolbar.png");

        SelectTab(window, "Animation");
        window.SelectAnimationTool(MainWindow.AnimationTrimTool);
        Avalonia.Threading.Dispatcher.UIThread.RunJobs();
        Shoot(window, animation);

        SelectTab(window, "Playback");
        window.SelectPlaybackTool(MainWindow.PlaybackOverlaysTool);
        Avalonia.Threading.Dispatcher.UIThread.RunJobs();
        Shoot(window, playback);

        string posed = PlaybackWithAPosableClip(output);
        CloseForTest(window);
        System.IO.File.Delete(Settings.SettingsPathForTest);
        Settings.SettingsPathForTest = null;

        Console.WriteLine(animation);
        Console.WriteLine(playback);
        Console.WriteLine(posed);
        return 0;
    }

    private static string PlaybackWithAPosableClip(string output)
    {
        string samples = System.IO.Path.Combine(AppContext.BaseDirectory, "samples");
        string clip = System.IO.Path.Combine(samples, "TurretIdleWeapReady.hkx");
        string skeleton = System.IO.Path.Combine(samples, "TurretStandingSkeleton.hkx");
        string target = System.IO.Path.Combine(output, "ui-playback-enabled.png");
        if (!System.IO.File.Exists(clip) || !System.IO.File.Exists(skeleton)) return target + " (skipped)";

        const string virtualPath = "Meshes/Actors/Turret/Animations/Mounted/Idle_WeapReady.hkx";
        string root = System.IO.Directory.CreateTempSubdirectory("bgs-shot-pack-").FullName;
        try
        {
            string data = System.IO.Path.Combine(root, "Data");
            System.IO.Directory.CreateDirectory(data);
            string archive = System.IO.Path.Combine(data, "Fallout4 - Animations.ba2");
            WriteArchive(archive, new[]
            {
                (virtualPath, System.IO.File.ReadAllBytes(clip)),
                ("Meshes/Actors/Turret/CharacterAssets/Skeleton.hkx", System.IO.File.ReadAllBytes(skeleton)),
            });
            Settings.TrySet("gameDataFolder", data, out _);

            var window = new MainWindow();
            window.Width = 1280;
            window.Height = 860;
            window.Show();
            Avalonia.Threading.Dispatcher.UIThread.RunJobs();
            window.OpenArchiveForTest(archive, virtualPath);
            Avalonia.Threading.Dispatcher.UIThread.RunJobs();

            SelectTab(window, "Playback");
            window.SelectPlaybackTool(MainWindow.PlaybackOverlaysTool);
            Avalonia.Threading.Dispatcher.UIThread.RunJobs();
            Shoot(window, target);
            CloseForTest(window);
        }
        finally
        {
            try { System.IO.Directory.Delete(root, true); }
            catch (Exception e) when (e is System.IO.IOException or UnauthorizedAccessException) { }
        }
        return target;
    }

    private static void Shoot(MainWindow window, string path)
    {
        var size = new Size(window.Width, window.Height);
        window.Measure(size);
        window.Arrange(new Rect(size));
        Avalonia.Threading.Dispatcher.UIThread.RunJobs();

        using var bitmap = new Avalonia.Media.Imaging.RenderTargetBitmap(
            new PixelSize((int)size.Width, (int)size.Height), new Vector(96, 96));
        bitmap.Render(window);
        bitmap.Save(path);
        Console.WriteLine($"        {path}: {bitmap.PixelSize.Width}x{bitmap.PixelSize.Height}");
    }
}
