using System;
using System.Collections.Generic;
using Avalonia;
using Avalonia.Automation;
using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Media;

namespace BehaviourStudio.App;

public partial class MainWindow : Window
{
    internal const string AnimationFindTool = "Find frame";
    internal const string AnimationEditTool = "Edit frame";
    internal const string AnimationTrimTool = "Trim";
    internal const string AnimationRetimeTool = "Retime";
    internal const string AnimationLoopTool = "Loop";

    internal const double ToolPanelWidth = 1180;
    private const double ToolGroupGap = 18;

    internal static readonly string[] AnimationToolNames =
    {
        AnimationFindTool, AnimationEditTool, AnimationTrimTool, AnimationRetimeTool, AnimationLoopTool,
    };

    private readonly Button _saveAnimationButton = Ux.Secondary("Save changes");
    private readonly ComboBox _animationToolCombo = Ux.Combo("Animation tool");
    private readonly Dictionary<string, Border> _animationToolPanels = new(StringComparer.Ordinal);
    private readonly Dictionary<string, StackPanel> _geometryAnswerSlots = new(StringComparer.Ordinal);

    private Control AnimationToolRibbon()
    {
        var first = Ux.Secondary("First");
        first.Click += (_, _) => PageFrames(int.MinValue);
        var earlier = Ux.Secondary("Earlier frames");
        earlier.Click += (_, _) => PageFrames(-FramesPerPage);
        var later = Ux.Secondary("Later frames");
        later.Click += (_, _) => PageFrames(FramesPerPage);
        var last = Ux.Secondary("Last");
        last.Click += (_, _) => PageFrames(int.MaxValue);
        ToolTip.SetTip(first, "Show the first page of frames.");
        ToolTip.SetTip(earlier, "Show the previous page of frames.");
        ToolTip.SetTip(later, "Show the next page of frames.");
        ToolTip.SetTip(last, "Show the last page of frames.");

        var aim = Ux.Secondary("Find frame");
        aim.Click += (_, _) => AimAtFraction();
        _fraction.KeyDown += (_, e) => { if (e.Key == Avalonia.Input.Key.Enter) AimAtFraction(); };
        _boneFilter.PropertyChanged += (_, e) =>
        {
            if (e.Property == TextBox.TextProperty) ShowAnimationFrames();
        };

        var apply = Ux.Secondary("Set frame");
        apply.Click += (_, _) => SetFrame();
        foreach (var box in new[] { _framePosition, _frameRotation, _frameScale })
            box.KeyDown += (_, e) => { if (e.Key == Avalonia.Input.Key.Enter) SetFrame(); };

        _saveAnimationButton.Click += (_, _) => SaveAnimation();

        var geometry = GeometryButtons();
        var trimSlot = new StackPanel();
        var retimeSlot = new StackPanel();
        var loopSlot = new StackPanel();

        Add(AnimationFindTool, Ux.Panel(Ux.ToolPanelName(AnimationFindTool), Ux.Wrap(Ux.RowTitle("Find frame"),
            Ux.Labelled(_boneFilter, "Bone or track name"), Ux.Labelled(_fraction, "Time fraction"), aim,
            _fractionAnswer)));
        Add(AnimationEditTool, Ux.Panel(Ux.ToolPanelName(AnimationEditTool), Ux.Wrap(Ux.RowTitle("Edit frame"),
            Ux.Labelled(_framePosition, "Frame position"), Ux.Labelled(_frameRotation, "Frame rotation"),
            Ux.Labelled(_frameScale, "Frame scale"), apply, _frameEditAnswer)));
        Add(AnimationTrimTool, Ux.Panel(Ux.ToolPanelName(AnimationTrimTool), Ux.Wrap(Ux.RowTitle("Trim"),
            Ux.Labelled(_trimStart, "First frame"), Ux.Labelled(_trimEnd, "Last frame"),
            geometry.PreviewTrim, geometry.Trim, trimSlot)));
        Add(AnimationRetimeTool, Ux.Panel(Ux.ToolPanelName(AnimationRetimeTool), Ux.Wrap(Ux.RowTitle("Retime"),
            Ux.Labelled(_retimeSpeed, "Speed ×"), Ux.Labelled(_retimeDuration, "New length (s)"),
            geometry.PreviewRetime, geometry.Retime, retimeSlot)));
        Add(AnimationLoopTool, Ux.Panel(Ux.ToolPanelName(AnimationLoopTool), Ux.Wrap(Ux.RowTitle("Loop"),
            Ux.Labelled(_loopBlend, "Blend frames"), geometry.PreviewLoop, geometry.Loop, loopSlot)));

        _geometryAnswerSlots[AnimationTrimTool] = trimSlot;
        _geometryAnswerSlots[AnimationRetimeTool] = retimeSlot;
        _geometryAnswerSlots[AnimationLoopTool] = loopSlot;

        var top = new WrapPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(0, 0, 0, 2) };
        top.Children.Add(ToolRow("Frame navigation", first, earlier, later, last, ToolCount(_framePage)));
        top.Children.Add(ToolRow("Tool", _animationToolCombo));
        top.Children.Add(ToolRow("", _saveAnimationButton));

        _animationToolCombo.ItemsSource = AnimationToolNames;
        string remembered = Settings.Get("animation_tool");
        int at = Array.IndexOf(AnimationToolNames, remembered);
        _animationToolCombo.SelectedIndex =
            at < 0 ? Array.IndexOf(AnimationToolNames, AnimationEditTool) : at;
        _animationToolCombo.SelectionChanged += (_, _) => ShowAnimationTool(remember: true);

        var host = new StackPanel
        {
            Orientation = Orientation.Vertical,
            Spacing = 6,
            MaxWidth = ToolPanelWidth,
            HorizontalAlignment = HorizontalAlignment.Left,
        };
        host.Children.Add(top);
        foreach (string tool in AnimationToolNames) host.Children.Add(_animationToolPanels[tool]);

        ShowAnimationTool();
        RefreshSaveAnimationState();
        return host;

        void Add(string tool, Border panel)
        {
            _animationToolPanels[tool] = panel;
        }
    }

    internal string SelectedAnimationTool => _animationToolCombo.SelectedItem as string ?? AnimationEditTool;

    internal bool AnimationToolPanelVisible(string tool) =>
        _animationToolPanels.TryGetValue(tool, out Border? panel) && panel.IsEffectivelyVisible;

    internal bool SelectAnimationTool(string tool)
    {
        int at = Array.IndexOf(AnimationToolNames, tool);
        if (at < 0) return false;
        _animationToolCombo.SelectedIndex = at;
        return true;
    }

    private void ShowAnimationTool(bool remember = false)
    {
        string tool = SelectedAnimationTool;
        foreach (var (name, panel) in _animationToolPanels) panel.IsVisible = name == tool;

        if (_geometryAnswerSlots.TryGetValue(tool, out StackPanel? slot))
        {
            if (_geometryAnswer.Parent is Panel previous) previous.Children.Remove(_geometryAnswer);
            slot.Children.Clear();
            slot.Children.Add(_geometryAnswer);
        }

        if (remember && Settings.Get("animation_tool") != tool) Settings.TrySet("animation_tool", tool, out _);
    }

    private void RefreshSaveAnimationState()
    {
        bool dirty = _animationEdited && !_readOnly && _animationData != null;
        _saveAnimationButton.IsEnabled = dirty;
        _saveAnimationButton.Classes.Set("primary", dirty);

        string why = dirty
            ? "Write the clip on the Animation tab back to " + System.IO.Path.GetFileName(_hkxPath) + "."
            : _readOnly
                ? "Not saved: " + _readOnlyWhy
                : _animationData == null
                    ? "Open an animation first; this file holds none."
                    : "No unsaved changes to write.";
        ToolTip.SetTip(_saveAnimationButton, why);
    }

    internal bool SaveAnimationButtonEnabled => _saveAnimationButton.IsEnabled;

    internal bool SaveAnimationButtonIsPrimary => _saveAnimationButton.Classes.Contains("primary");
}
