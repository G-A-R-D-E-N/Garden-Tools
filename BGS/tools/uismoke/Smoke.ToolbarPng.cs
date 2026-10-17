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

        CloseForTest(window);
        System.IO.File.Delete(Settings.SettingsPathForTest);
        Settings.SettingsPathForTest = null;

        Console.WriteLine(animation);
        Console.WriteLine(playback);
        return 0;
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
