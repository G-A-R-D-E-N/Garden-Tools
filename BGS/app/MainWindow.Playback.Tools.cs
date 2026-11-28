using System;
using System.Collections.Generic;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Media;

namespace BehaviourStudio.App;

public partial class MainWindow : Window
{
    internal const string PlaybackOverlaysTool = "Overlays";
    internal const string PlaybackSceneTool = "Scene tools";
    internal const string PlaybackDiagnosticsTool = "Diagnostics";

    internal static readonly string[] PlaybackToolNames =
    {
        PlaybackOverlaysTool, PlaybackSceneTool, PlaybackDiagnosticsTool,
    };

    private readonly Button _firstFrameButton = Ux.Secondary("|<");
    private readonly Button _backFrameButton = Ux.Secondary("<");
    private readonly Button _forwardFrameButton = Ux.Secondary(">");
    private readonly Button _lastFrameButton = Ux.Secondary(">|");
    private readonly Button _fitButton = Ux.Secondary("Fit");
    private readonly Button _reloadFromNodeButton = Ux.Secondary("From selected node");
    private readonly Button _meshButton = Ux.Secondary("Mesh...");
    private readonly Button _noMeshButton = Ux.Secondary("No mesh");
    private readonly CheckBox _referenceToggle = Ux.Named(new CheckBox
    {
        Content = "Reference pose",
        Foreground = Ux.MetaBrush,
        FontSize = 12,
        VerticalAlignment = VerticalAlignment.Center,
    }, "Reference pose");
    private readonly CheckBox _travelToggle = Ux.Named(new CheckBox
    {
        Content = "Follow travel",
        Foreground = Ux.MetaBrush,
        FontSize = 12,
        VerticalAlignment = VerticalAlignment.Center,
    }, "Follow travel");
    private readonly ComboBox _playbackToolCombo = Ux.Combo("Playback tool");
    private readonly ComboBox _meshModeCombo = Ux.Combo("Mesh mode", 110);
    private readonly CheckBox _meshWireframeToggle = Ux.Named(new CheckBox
    {
        Content = "Mesh wireframe",
        Foreground = Ux.MetaBrush,
        FontSize = 12,
        VerticalAlignment = VerticalAlignment.Center,
    }, "Mesh wireframe");
    private readonly TextBlock _meshViewportStatus = new()
    {
        Foreground = Ux.MetaBrush, FontSize = 11, TextWrapping = TextWrapping.Wrap, MaxWidth = 360,
    };
    private readonly TextBlock _diagnosticsText = new()
    {
        Foreground = Ux.MetaBrush, FontSize = 12, TextWrapping = TextWrapping.Wrap,
        VerticalAlignment = VerticalAlignment.Center,
    };
    private readonly Dictionary<string, Border> _playbackToolPanels = new(StringComparer.Ordinal);

    private Control PlaybackToolRibbon()
    {
        _firstFrameButton.Click += (_, _) => ShowFrame(0, stop: true);
        _backFrameButton.Click += (_, _) => ShowFrame(_playback.Frame - 1, stop: true);
        _forwardFrameButton.Click += (_, _) => ShowFrame(_playback.Frame + 1, stop: true);
        _lastFrameButton.Click += (_, _) => ShowFrame(int.MaxValue, stop: true);
        _fitButton.Click += (_, _) => _skeleton.Frame();
        _scrub.MinWidth = 180;
        _scrub.Height = 24;
        foreach (var step in new[] { _firstFrameButton, _backFrameButton, _forwardFrameButton, _lastFrameButton })
            step.MinWidth = 48;
        _fitButton.MinWidth = 60;
        Ux.Named(_scrub, "Timeline frame");
        _frameLabel.VerticalAlignment = VerticalAlignment.Center;

        var timeline = new List<Control> { _scrub, Ux.Pill(_frameLabel) };
        if (_scalePill != null) timeline.Add(_scalePill);
        if (_frameDistancePill != null) timeline.Add(_frameDistancePill);

        foreach (var toggle in new[]
                 {
                     _referenceToggle, _travelToggle, _bodiesToggle, _constraintsToggle, _bindingsToggle,
                     _physicsSkeletonToggle, _framesToggle, _engineerToggle, _meshWireframeToggle,
                 })
        {
            toggle.Padding = new Thickness(0, 0, 4, 0);
            toggle.MinHeight = 18;
            toggle.FontSize = 11;
        }

        _meshModeCombo.ItemsSource = MeshModes;
        string savedMeshMode = Settings.Get("playback_mesh_mode");
        int savedMeshAt = Array.IndexOf(MeshModes, savedMeshMode);
        _meshMode = savedMeshAt < 0 ? MeshModeShaded : MeshModes[savedMeshAt];
        _meshModeCombo.SelectedIndex = savedMeshAt < 0 ? 0 : savedMeshAt;
        _meshModeCombo.SelectionChanged += (_, _) =>
            SetMeshMode(_meshModeCombo.SelectedItem as string ?? MeshModeShaded);
        _meshWireframeToggle.IsChecked = Settings.Get("playback_mesh_wireframe") == "1";
        _meshWireframeToggle.IsCheckedChanged += (_, _) =>
        {
            Settings.TrySet("playback_mesh_wireframe",
                            _meshWireframeToggle.IsChecked == true ? "1" : "0", out _);
            SyncViewport();
        };

        Add(PlaybackOverlaysTool, Ux.Panel(Ux.ToolPanelName(PlaybackOverlaysTool), Ux.Wrap(Ux.RowTitle("Overlays"),
            _referenceToggle, _travelToggle, _bodiesToggle, _constraintsToggle, _bindingsToggle,
            _physicsSkeletonToggle, _framesToggle, _engineerToggle, _meshModeCombo, _meshWireframeToggle)));
        Add(PlaybackSceneTool, Ux.Panel(Ux.ToolPanelName(PlaybackSceneTool), Ux.Wrap(Ux.RowTitle("Scene tools"),
            _dropButton, _reloadFromNodeButton, _meshButton, _noMeshButton, _clearFrameDistanceButton)));
        Add(PlaybackDiagnosticsTool, Ux.Panel(Ux.ToolPanelName(PlaybackDiagnosticsTool),
            Ux.Wrap(Ux.RowTitle("Diagnostics"),
            _copyDiagnosticsButton, _diagnosticsText),
            Ux.Wrap(_meshViewportStatus)));

        var top = new WrapPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(0, 0, 0, 2) };
        top.Children.Add(ToolRow("Transport", _playButton, _firstFrameButton, _backFrameButton,
            _forwardFrameButton, _lastFrameButton, _fitButton));
        top.Children.Add(ToolRow("Timeline", timeline.ToArray()));
        top.Children.Add(ToolRow("Tool", _playbackToolCombo));

        _playbackToolCombo.ItemsSource = PlaybackToolNames;
        string remembered = Settings.Get("playback_tool");
        int at = Array.IndexOf(PlaybackToolNames, remembered);
        _playbackToolCombo.SelectedIndex =
            at < 0 ? Array.IndexOf(PlaybackToolNames, PlaybackOverlaysTool) : at;
        _playbackToolCombo.SelectionChanged += (_, _) => ShowPlaybackTool(remember: true);

        var host = new StackPanel
        {
            Orientation = Orientation.Vertical,
            Spacing = 6,
            MaxWidth = ToolPanelWidth,
            HorizontalAlignment = HorizontalAlignment.Left,
        };
        host.Children.Add(top);
        foreach (string tool in PlaybackToolNames) host.Children.Add(_playbackToolPanels[tool]);

        ShowPlaybackTool();
        RefreshDiagnosticsPanel();
        UpdateTransportState();
        return host;

        void Add(string tool, Border panel)
        {
            _playbackToolPanels[tool] = panel;
        }
    }

    internal string SelectedPlaybackTool => _playbackToolCombo.SelectedItem as string ?? PlaybackOverlaysTool;

    internal bool PlaybackToolPanelVisible(string tool) =>
        _playbackToolPanels.TryGetValue(tool, out Border? panel) && panel.IsEffectivelyVisible;

    internal bool SelectPlaybackTool(string tool)
    {
        int at = Array.IndexOf(PlaybackToolNames, tool);
        if (at < 0) return false;
        _playbackToolCombo.SelectedIndex = at;
        return true;
    }

    private void ShowPlaybackTool(bool remember = false)
    {
        string tool = SelectedPlaybackTool;
        foreach (var (name, panel) in _playbackToolPanels) panel.IsVisible = name == tool;
        if (remember && Settings.Get("playback_tool") != tool) Settings.TrySet("playback_tool", tool, out _);
    }

    private void RefreshDiagnosticsPanel()
    {
        _diagnosticsText.Text = _physicsReportNote is { Length: > 0 } note
            ? note
            : "No physics report yet: open a file whose skeleton carries a ragdoll.";
    }

    private void UpdateTransportState()
    {
        bool posable = _poseAnimation != null;
        bool playable = posable && _playback.CanPlay;

        _playButton.IsEnabled = playable;
        _firstFrameButton.IsEnabled = posable;
        _backFrameButton.IsEnabled = posable;
        _forwardFrameButton.IsEnabled = posable;
        _lastFrameButton.IsEnabled = posable;

        string why = playable
            ? "Play or pause this clip."
            : _poseRefusal.Length > 0
                ? _poseRefusal
                : posable
                    ? "This clip holds a single frame, so there is nothing to play; the step buttons " +
                      "still move it."
                    : "Nothing is posable yet. Select a clip, or press From selected node.";
        ToolTip.SetTip(_playButton, why);
        foreach (var button in new[] { _firstFrameButton, _backFrameButton, _forwardFrameButton, _lastFrameButton })
            ToolTip.SetTip(button, posable ? "Step one frame." : why);
    }
}
