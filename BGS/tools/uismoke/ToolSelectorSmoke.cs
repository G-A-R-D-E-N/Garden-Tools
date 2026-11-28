using System;
using System.IO;
using System.Linq;
using System.Collections.Generic;
using Avalonia.Automation;
using Avalonia.Controls;
using Avalonia.Threading;
using BehaviourStudio.App;

namespace BehaviourStudio.UiSmoke;

public static class ToolSelectorSmoke
{
    public static int Run()
    {
        Console.WriteLine("\nthe Animation and Playback tabs select a tool and show only its panel");

        int before = Smoke.FailedCount;
        string? previous = Settings.SettingsPathForTest;
        string settingsPath = Path.Combine(Path.GetTempPath(), $"bgs-tools-{Guid.NewGuid():N}.cfg");
        string root = Directory.CreateTempSubdirectory("bgs-tools-").FullName;
        Settings.SettingsPathForTest = settingsPath;
        Settings.TrySet("tour_done", "1", out _);

        try
        {
            string? fixture = Path.Combine(AppContext.BaseDirectory, "fixtures", "RadRoachTurnLeft90.hkx");
            if (!File.Exists(fixture)) fixture = null;

            var window = new MainWindow();
            window.Show();
            try
            {
                if (fixture != null)
                {
                    string copy = Path.Combine(root, "tool-selector.hkx");
                    File.Copy(fixture, copy, true);
                    window.Open(copy);
                    Dispatcher.UIThread.RunJobs();
                }

                Smoke.SelectTab(window, "Animation");
                AnimationTools(window);
                SaveStateFollowsTheEdits(window, fixture != null);

                Smoke.SelectTab(window, "Playback");
                PlaybackTools(window);

                ToolChoicesAreRemembered(window);

                var wide = new MainWindow();
                wide.Width = 1280;
                wide.Height = 860;
                wide.Show();
                Dispatcher.UIThread.RunJobs();
                try { NoHorizontalScrollbar(wide); }
                finally { Smoke.CloseForTest(wide); }
            }
            finally { Smoke.CloseForTest(window); }
        }
        finally
        {
            Settings.SettingsPathForTest = previous;
            TryDelete(settingsPath);
            try { Directory.Delete(root, true); }
            catch (Exception e) when (e is IOException or UnauthorizedAccessException) { }
        }

        return Smoke.FailedCount == before ? 0 : 1;
    }

    private static void AnimationTools(MainWindow window)
    {
        (string Tool, string Field)[] tools =
        {
            (MainWindow.AnimationFindTool, "Bone or track name"),
            (MainWindow.AnimationEditTool, "Frame position"),
            (MainWindow.AnimationTrimTool, "First frame"),
            (MainWindow.AnimationRetimeTool, "Speed ×"),
            (MainWindow.AnimationLoopTool, "Blend frames"),
        };

        foreach (var (tool, field) in tools)
        {
            Smoke.CheckTrue($"the Animation tab offers {tool}", window.SelectAnimationTool(tool));
            Dispatcher.UIThread.RunJobs();
            Smoke.CheckTrue($"{tool} shows its own panel", window.AnimationToolPanelVisible(tool));
            foreach (var other in tools.Where(candidate => candidate.Tool != tool))
                Smoke.CheckTrue($"{tool} hides the {other.Tool} panel",
                                !window.AnimationToolPanelVisible(other.Tool));
            Smoke.CheckTrue($"{tool} shows the {field} box", BoxNamed(window, field));
            Smoke.CheckTrue($"{tool} captions its panel {tool.ToUpperInvariant()}", CaptionVisible(window, tool));
            Smoke.CheckTrue($"{tool} panel carries its own automation name", PanelNamed(window, tool));
        }

        window.SelectAnimationTool(MainWindow.AnimationTrimTool);
        Dispatcher.UIThread.RunJobs();
        Smoke.CheckTrue("a trim field carries a visible label",
                        LabelVisible(window, "First frame") && LabelVisible(window, "Last frame"));

        window.SelectAnimationTool(MainWindow.AnimationLoopTool);
        Dispatcher.UIThread.RunJobs();
        Smoke.CheckTrue("the loop field carries its visible label", LabelVisible(window, "Blend frames"));

        window.SelectAnimationTool(MainWindow.AnimationRetimeTool);
        Dispatcher.UIThread.RunJobs();
        Smoke.CheckTrue("the retime fields carry their visible labels",
                        LabelVisible(window, "Speed ×") && LabelVisible(window, "New length (s)"));
    }

    private static void PlaybackTools(MainWindow window)
    {
        (string Tool, string Control)[] tools =
        {
            (MainWindow.PlaybackOverlaysTool, "Reference pose"),
            (MainWindow.PlaybackSceneTool, "Drop"),
            (MainWindow.PlaybackDiagnosticsTool, "Copy diagnostics"),
        };

        foreach (var (tool, control) in tools)
        {
            Smoke.CheckTrue($"the Playback tab offers {tool}", window.SelectPlaybackTool(tool));
            Dispatcher.UIThread.RunJobs();
            Smoke.CheckTrue($"{tool} shows its own panel", window.PlaybackToolPanelVisible(tool));
            foreach (var other in tools.Where(candidate => candidate.Tool != tool))
                Smoke.CheckTrue($"{tool} hides the {other.Tool} panel",
                                !window.PlaybackToolPanelVisible(other.Tool));
            Smoke.CheckTrue($"{tool} shows {control}", ControlVisible(window, control));
            Smoke.CheckTrue($"{tool} captions its panel {tool.ToUpperInvariant()}", CaptionVisible(window, tool));
            Smoke.CheckTrue($"{tool} panel carries its own automation name", PanelNamed(window, tool));
        }

        Smoke.CheckTrue("the transport stays outside the tool panels",
                        ControlVisible(window, "Play") && ControlVisible(window, "Fit"));
    }

    private static void SaveStateFollowsTheEdits(MainWindow window, bool loaded)
    {
        if (!loaded)
        {
            Smoke.CheckTrue("with nothing loaded the save button is off", !window.SaveAnimationButtonEnabled);
            return;
        }

        Smoke.CheckTrue("a clean animation keeps Save changes out of reach",
                        !window.SaveAnimationButtonEnabled && !window.SaveAnimationButtonIsPrimary);
        Smoke.CheckTrue("nothing is dirty before an edit", !window.AnimationEdited);

        if (!window.PickFrame(0, 0))
        {
            Smoke.CheckTrue("the fixture has a frame to edit", false);
            return;
        }

        window.TypeFramePosition("7.25, -8.5, 9.75");
        Dispatcher.UIThread.RunJobs();
        Smoke.CheckTrue("an edited animation turns Save changes on", window.SaveAnimationButtonEnabled);
        Smoke.CheckTrue("and makes it the primary action", window.SaveAnimationButtonIsPrimary);

        Smoke.CheckTrue("the guarded save writes the clip", window.SaveAnimationForTest());
        Dispatcher.UIThread.RunJobs();
        Smoke.CheckTrue("and the button goes quiet again",
                        !window.SaveAnimationButtonEnabled && !window.AnimationEdited);
    }

    private static void ToolChoicesAreRemembered(MainWindow window)
    {
        window.SelectAnimationTool(MainWindow.AnimationLoopTool);
        window.SelectPlaybackTool(MainWindow.PlaybackDiagnosticsTool);
        Dispatcher.UIThread.RunJobs();

        var reopened = new MainWindow();
        reopened.Show();
        Dispatcher.UIThread.RunJobs();
        try
        {
            Smoke.Check("the Animation tab reopens on the chosen tool",
                        MainWindow.AnimationLoopTool, reopened.SelectedAnimationTool);
            Smoke.Check("the Playback tab reopens on the chosen tool",
                        MainWindow.PlaybackDiagnosticsTool, reopened.SelectedPlaybackTool);
        }
        finally { Smoke.CloseForTest(reopened); }
    }

    private static void NoHorizontalScrollbar(MainWindow window)
    {
        var tabs = Smoke.Find<TabControl>(window).First();
        (string Tool, string Tab)[] tools =
        {
            (MainWindow.AnimationEditTool, "Animation"),
            (MainWindow.AnimationTrimTool, "Animation"),
            (MainWindow.AnimationLoopTool, "Animation"),
            (MainWindow.PlaybackOverlaysTool, "Playback"),
            (MainWindow.PlaybackSceneTool, "Playback"),
            (MainWindow.PlaybackDiagnosticsTool, "Playback"),
        };

        foreach (var (tool, tab) in tools)
        {
            Smoke.SelectTab(window, tab);
            if (tab == "Animation") window.SelectAnimationTool(tool);
            else window.SelectPlaybackTool(tool);
            Dispatcher.UIThread.RunJobs();

            var toolbar = Smoke.Find<Border>(window).Single(border => border.Name == "WorkspaceToolbar");
            var scroll = (ScrollViewer)toolbar.Child!;
            Console.WriteLine($"        {tab} {tool}: extent {scroll.Extent.Width:F0}, " +
                              $"viewport {scroll.Viewport.Width:F0}, toolbar {toolbar.Bounds.Height:F0} tall, " +
                              $"editor {tabs.Bounds.Height:F0} tall");

            Smoke.CheckTrue($"{tab} {tool} needs no horizontal scrollbar at 1280 px",
                            scroll.Extent.Width <= scroll.Viewport.Width + 0.5);
            Smoke.CheckTrue($"{tab} {tool} leaves most of the window to the editor",
                            tabs.Bounds.Height > 400);
        }
    }

    private static bool CaptionVisible(MainWindow window, string tool) =>
        Smoke.Find<TextBlock>(window).Any(block => block.IsEffectivelyVisible &&
            block.Text == tool.ToUpperInvariant());

    private static bool PanelNamed(MainWindow window, string tool) =>
        Smoke.Find<Border>(window).Any(border => AutomationProperties.GetName(border) == Ux.ToolPanelName(tool));

    private static bool BoxNamed(MainWindow window, string name) =>
        Smoke.Find<TextBox>(window).Any(box => box.IsEffectivelyVisible &&
            AutomationProperties.GetName(box) == name);

    private static bool LabelVisible(MainWindow window, string text) =>
        Smoke.Find<TextBlock>(window).Any(block => block.IsEffectivelyVisible && block.Text == text);

    private static bool ControlVisible(MainWindow window, string label) =>
        BoxNamed(window, label) ||
        Smoke.Find<Button>(window).Any(button => button.IsEffectivelyVisible &&
            button.Content?.ToString() == label) ||
        Smoke.Find<CheckBox>(window).Any(box => box.IsEffectivelyVisible &&
            box.Content?.ToString() == label);

    private static void TryDelete(string path)
    {
        try { File.Delete(path); }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException) { }
    }
}
