using System;
using System.IO;
using System.Linq;
using Avalonia.Automation;
using Avalonia.Controls;
using Avalonia.Threading;
using BehaviourStudio.App;

namespace BehaviourStudio.UiSmoke;

public static class MeshViewportSmoke
{
    public static int Run()
    {
        Console.WriteLine("\nthe Playback mesh viewport picks its renderer and falls back without OpenGL");

        int before = Smoke.FailedCount;
        string? previous = Settings.SettingsPathForTest;
        string settingsPath = Path.Combine(Path.GetTempPath(), $"bgs-mesh-{Guid.NewGuid():N}.cfg");
        Settings.SettingsPathForTest = settingsPath;
        Settings.TrySet("tour_done", "1", out _);
        Settings.TrySet("playback_mesh_mode", MainWindow.MeshModeShaded, out _);

        try
        {
            var window = new MainWindow();
            window.Show();
            try
            {
                Smoke.SelectTab(window, "Playback");
                window.SelectPlaybackTool(MainWindow.PlaybackOverlaysTool);
                Dispatcher.UIThread.RunJobs();

                Smoke.Check("the Playback viewport starts in Shaded mode",
                            MainWindow.MeshModeShaded, window.MeshModeName);
                Smoke.CheckTrue("a headless run never gets an OpenGL context", !window.MeshViewportOnGpu);
                Smoke.CheckTrue("Shaded without OpenGL is reported as the wireframe fallback",
                                window.MeshWireframeFallbackActive);
                Smoke.CheckTrue("the fallback keeps drawing the 2D wireframe", window.MeshWireframeDrawn);
                Smoke.CheckTrue("the mesh wireframe overlay toggle starts off", !window.MeshWireframeToggleChecked);
                Smoke.CheckTrue("the status line explains the fallback",
                                window.MeshViewportStatusText.Contains("no OpenGL", StringComparison.Ordinal));

                var overlays = Smoke.Find<Border>(window)
                    .Single(border => AutomationProperties.GetName(border) == Ux.ToolPanelName(MainWindow.PlaybackOverlaysTool));
                Smoke.CheckTrue("the Overlays panel carries the mesh wireframe toggle",
                                Smoke.Find<CheckBox>(overlays).Any(box => box.Content?.ToString() == "Mesh wireframe"));
                Smoke.CheckTrue("the Overlays panel carries the mesh mode picker",
                                Smoke.Find<ComboBox>(overlays).Any(combo =>
                                    AutomationProperties.GetName(combo) == "Mesh mode"));

                Smoke.CheckTrue("the viewport takes Wireframe mode", window.SelectMeshModeForTest(MainWindow.MeshModeWireframe));
                Dispatcher.UIThread.RunJobs();
                Smoke.Check("Wireframe is the chosen mode", MainWindow.MeshModeWireframe, window.MeshModeName);
                Smoke.CheckTrue("Wireframe draws the 2D edges", window.MeshWireframeDrawn);
                Smoke.CheckTrue("Wireframe is not a fallback", !window.MeshWireframeFallbackActive);

                Smoke.CheckTrue("the viewport takes Off mode", window.SelectMeshModeForTest(MainWindow.MeshModeOff));
                Dispatcher.UIThread.RunJobs();
                Smoke.CheckTrue("Off draws no mesh", !window.MeshWireframeDrawn);
                Smoke.CheckTrue("Off hides the OpenGL layer", !window.MeshViewportLayerVisible);

                Smoke.CheckTrue("the mode comes back to Shaded", window.SelectMeshModeForTest(MainWindow.MeshModeShaded));
                Dispatcher.UIThread.RunJobs();
                Smoke.CheckTrue("Shaded keeps the OpenGL layer in the layout",
                                window.MeshViewportLayerVisible);
                Smoke.CheckTrue("Shaded restores the wireframe fallback headlessly", window.MeshWireframeDrawn);
            }
            finally { Smoke.CloseForTest(window); }

            var reopened = new MainWindow();
            reopened.Show();
            try
            {
                Dispatcher.UIThread.RunJobs();
                Smoke.Check("the mesh mode is remembered", MainWindow.MeshModeShaded, reopened.MeshModeName);
            }
            finally { Smoke.CloseForTest(reopened); }
        }
        finally
        {
            Settings.SettingsPathForTest = previous;
            try { File.Delete(settingsPath); }
            catch (Exception e) when (e is IOException or UnauthorizedAccessException) { }
        }

        return Smoke.FailedCount == before ? 0 : 1;
    }
}
