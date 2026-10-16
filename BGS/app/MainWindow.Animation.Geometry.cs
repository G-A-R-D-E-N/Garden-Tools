using System;
using System.Globalization;
using Avalonia.Controls;
using Avalonia.Media;
using OpenCommonwealth.Services.Hkx;

namespace BehaviourStudio.App;

public partial class MainWindow : Window
{
    private readonly TextBox _trimStart = Ux.Field("first", 56);
    private readonly TextBox _trimEnd = Ux.Field("last", 56);
    private readonly TextBox _retimeSpeed = Ux.Field("x", 56);
    private readonly TextBox _retimeDuration = Ux.Field("seconds", 78);
    private readonly TextBox _loopBlend = Ux.Field("blend frames", 74);
    private readonly TextBlock _geometryAnswer = new()
    {
        Foreground = Ux.MetaBrush,
        FontSize = 12,
        TextWrapping = TextWrapping.Wrap,
    };

    private NativeAnimation.Timeline? _animationTimeline;

    private Control[] GeometryToolbarGroups()
    {
        var previewTrim = Ux.Secondary("Preview trim");
        previewTrim.Click += (_, _) => PreviewTrim();
        var trim = Ux.Secondary("Trim");
        trim.Click += (_, _) => ApplyTrim();

        var previewRetime = Ux.Secondary("Preview retime");
        previewRetime.Click += (_, _) => PreviewRetime();
        var retime = Ux.Secondary("Retime");
        retime.Click += (_, _) => ApplyRetime();

        var previewLoop = Ux.Secondary("Preview loop");
        previewLoop.Click += (_, _) => PreviewLoop();
        var loop = Ux.Secondary("Loop");
        loop.Click += (_, _) => ApplyLoop();

        foreach (var box in new[] { _trimStart, _trimEnd })
            box.KeyDown += (_, e) => { if (e.Key == Avalonia.Input.Key.Enter) ApplyTrim(); };
        foreach (var box in new[] { _retimeSpeed, _retimeDuration })
            box.KeyDown += (_, e) => { if (e.Key == Avalonia.Input.Key.Enter) ApplyRetime(); };
        _loopBlend.KeyDown += (_, e) => { if (e.Key == Avalonia.Input.Key.Enter) ApplyLoop(); };

        return new Control[]
        {
            ToolbarGroup("Trim frames", _trimStart, _trimEnd, previewTrim, trim),
            ToolbarGroup("Retime", _retimeSpeed, _retimeDuration, previewRetime, retime),
            ToolbarGroup("Loop", _loopBlend, previewLoop, loop),
        };
    }

    private void ResetGeometry()
    {
        var anim = _animationData;

        _animationTimeline = null;
        _retimeSpeed.Text = "";
        _retimeDuration.Text = "";
        _loopBlend.Text = "1";
        _geometryAnswer.Text = "";

        if (anim == null)
        {
            _trimStart.Text = "";
            _trimEnd.Text = "";
            return;
        }

        _trimStart.Text = "0";
        _trimEnd.Text = (anim.NumFrames - 1).ToString(CultureInfo.InvariantCulture);
    }

    private void Say(string text, IBrush brush)
    {
        _geometryAnswer.Text = text;
        _geometryAnswer.Foreground = brush;
    }

    private void RefuseGeometry() =>
        Say(_animationSummary.Text ?? "Open an animation first.",
            _animationSummary.Foreground ?? Ux.MutedBrush);

    private RootMotion.Motion CurrentMotion()
    {
        if (_animationTimeline != null) return _animationTimeline.Motion ?? new RootMotion.Motion();

        try { return RootMotion.Read(_hkxPath); }
        catch { return new RootMotion.Motion(); }
    }

    private bool TrimRange(HkxAnimationData anim, out int first, out int last)
    {
        first = 0;
        last = anim.NumFrames - 1;

        string start = (_trimStart.Text ?? "").Trim();
        string end = (_trimEnd.Text ?? "").Trim();

        if (start.Length > 0 &&
            !int.TryParse(start, NumberStyles.Integer, CultureInfo.InvariantCulture, out first))
        {
            Say($"\"{start}\" is not a frame number: give the first frame to keep, like 4.", Ux.BadBrush);
            return false;
        }

        if (end.Length > 0 &&
            !int.TryParse(end, NumberStyles.Integer, CultureInfo.InvariantCulture, out last))
        {
            Say($"\"{end}\" is not a frame number: give the last frame to keep, like 14.", Ux.BadBrush);
            return false;
        }

        return true;
    }

    private bool TryTrim(out AnimationEdit.Trimmed cut, out NativeAnimation.Timeline timeline)
    {
        cut = null!;
        timeline = null!;

        var anim = _animationData;
        if (anim == null) { RefuseGeometry(); return false; }
        if (!TrimRange(anim, out int first, out int last)) return false;

        try { cut = AnimationEdit.Trim(anim, CurrentMotion(), first, last); }
        catch (InvalidOperationException e) { Say(e.Message, Ux.BadBrush); return false; }

        float frame = AnimationEdit.FrameDuration(anim);
        float from = first * frame;
        float to = last * frame;
        var before = _animationTimeline;

        timeline = before == null
            ? new NativeAnimation.Timeline(cut.Animation.Duration, from, to, 1f, cut.Motion)
            : new NativeAnimation.Timeline(cut.Animation.Duration,
                                           before.FromTime + from / before.Scale,
                                           before.FromTime + to / before.Scale,
                                           before.Scale, cut.Motion);
        return true;
    }

    private void PreviewTrim()
    {
        if (!TryTrim(out var cut, out _)) return;
        Say(cut.ToString(), Ux.MetaBrush);
    }

    private void ApplyTrim()
    {
        if (!TryTrim(out var cut, out var timeline)) return;
        Adopt(cut.Animation, timeline, cut.ToString() + "   (unsaved)");
    }

    private bool TryRetime(out AnimationEdit.Retimed made, out NativeAnimation.Timeline timeline,
                           out string note)
    {
        made = null!;
        timeline = null!;
        note = "";

        var anim = _animationData;
        if (anim == null) { RefuseGeometry(); return false; }

        string speedText = (_retimeSpeed.Text ?? "").Trim();
        string durationText = (_retimeDuration.Text ?? "").Trim();

        if (speedText.Length > 0 && durationText.Length > 0)
        {
            Say("Fill either the speed factor or the new duration in seconds, not both.", Ux.BadBrush);
            return false;
        }

        if (speedText.Length == 0 && durationText.Length == 0)
        {
            Say("Type a speed factor, like 2 to make it twice as long, or a new duration in seconds.",
                Ux.BadBrush);
            return false;
        }

        float scale;

        if (speedText.Length > 0)
        {
            if (!float.TryParse(speedText, NumberStyles.Float, CultureInfo.InvariantCulture, out scale) ||
                float.IsNaN(scale) || float.IsInfinity(scale))
            {
                Say($"\"{speedText}\" is not a speed factor.", Ux.BadBrush);
                return false;
            }
        }
        else
        {
            if (!float.TryParse(durationText, NumberStyles.Float, CultureInfo.InvariantCulture,
                                out float wanted) ||
                float.IsNaN(wanted) || float.IsInfinity(wanted) || wanted <= 0)
            {
                Say($"\"{durationText}\" is not a length of time in seconds.", Ux.BadBrush);
                return false;
            }

            if (anim.Duration <= 0)
            {
                Say("This clip has no length to work a new duration out from.", Ux.BadBrush);
                return false;
            }

            scale = wanted / anim.Duration;
            note = "   (the frames stay at this clip's own rate, so the length lands on the nearest frame)";
        }

        try { made = AnimationEdit.Retime(anim, CurrentMotion(), scale); }
        catch (InvalidOperationException e) { Say(e.Message, Ux.BadBrush); return false; }

        float was = (anim.NumFrames - 1) * AnimationEdit.FrameDuration(anim);
        var before = _animationTimeline;

        timeline = before == null
            ? new NativeAnimation.Timeline(made.Animation.Duration, 0f, was, made.Scale, made.Motion)
            : new NativeAnimation.Timeline(made.Animation.Duration, before.FromTime, before.ToTime,
                                           before.Scale * made.Scale, made.Motion);
        return true;
    }

    private void PreviewRetime()
    {
        if (!TryRetime(out var made, out _, out string note)) return;
        Say(made.ToString() + note, Ux.MetaBrush);
    }

    private void ApplyRetime()
    {
        if (!TryRetime(out var made, out var timeline, out string note)) return;
        Adopt(made.Animation, timeline, made.ToString() + note + "   (unsaved)");
    }

    private bool TryLoop(out AnimationEdit.Looped looped, out NativeAnimation.Timeline timeline)
    {
        looped = null!;
        timeline = null!;

        var anim = _animationData;
        if (anim == null) { RefuseGeometry(); return false; }

        string blendText = (_loopBlend.Text ?? "").Trim();
        int blend = 1;

        if (blendText.Length > 0 &&
            !int.TryParse(blendText, NumberStyles.Integer, CultureInfo.InvariantCulture, out blend))
        {
            Say($"\"{blendText}\" is not a number of frames to blend.", Ux.BadBrush);
            return false;
        }

        try { looped = AnimationEdit.Loop(anim, CurrentMotion(), blend); }
        catch (InvalidOperationException e) { Say(e.Message, Ux.BadBrush); return false; }

        var before = _animationTimeline;

        timeline = before == null
            ? new NativeAnimation.Timeline(looped.Animation.Duration, 0f, anim.Duration, 1f, looped.Motion)
            : new NativeAnimation.Timeline(looped.Animation.Duration, before.FromTime, before.ToTime,
                                           before.Scale, looped.Motion);
        return true;
    }

    private void PreviewLoop()
    {
        if (!TryLoop(out var looped, out _)) return;
        Say(looped.ToString(), Ux.MetaBrush);
    }

    private void ApplyLoop()
    {
        if (!TryLoop(out var looped, out var timeline)) return;
        Adopt(looped.Animation, timeline, looped.ToString() + "   (unsaved)");
    }

    private void Adopt(HkxAnimationData made, NativeAnimation.Timeline timeline, string said)
    {
        _animationData = made;
        _animationTimeline = timeline;
        _animationEdited = true;
        _editTrack = _editFrame = -1;
        _aimedFrame = -1;
        _frameStart = 0;
        _trimStart.Text = "0";
        _trimEnd.Text = (made.NumFrames - 1).ToString(CultureInfo.InvariantCulture);
        _frameEditAnswer.Text = "Frame selection cleared: the clip changed shape.";
        _frameEditAnswer.Foreground = Ux.MutedBrush;
        ShowAnimationFrames();
        Say(said, Ux.CodeBrush);
    }
}
