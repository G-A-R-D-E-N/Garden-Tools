using System;
using System.Collections.Generic;
using Flash.Events;
using Flash.Geom;

namespace Flash.Display;

public sealed class GraphicsCommand
{
    public GraphicsCommand(string name, double[] args)
    {
        Name = name;
        Args = args;
    }

    public string Name { get; }

    public double[] Args { get; }
}

public class Graphics
{
    private readonly List<GraphicsCommand> _commands = new List<GraphicsCommand>();

    public IReadOnlyList<GraphicsCommand> Commands => _commands;

    private void Record(string name, params double[] args) => _commands.Add(new GraphicsCommand(name, args));

    public void Clear() => _commands.Clear();

    public void BeginFill(double color, double alpha = 1) => Record("beginFill", color, alpha);

    public void EndFill() => Record("endFill");

    public void LineStyle(double thickness = double.NaN, double color = 0, double alpha = 1) =>
        Record("lineStyle", thickness, color, alpha);

    public void MoveTo(double x, double y) => Record("moveTo", x, y);

    public void LineTo(double x, double y) => Record("lineTo", x, y);

    public void CurveTo(double controlX, double controlY, double anchorX, double anchorY) =>
        Record("curveTo", controlX, controlY, anchorX, anchorY);

    public void DrawRect(double x, double y, double width, double height) => Record("drawRect", x, y, width, height);

    public void DrawCircle(double x, double y, double radius) => Record("drawCircle", x, y, radius);

    public void DrawEllipse(double x, double y, double width, double height) =>
        Record("drawEllipse", x, y, width, height);

    public void DrawRoundRect(double x, double y, double width, double height, double ellipseWidth,
                              double ellipseHeight = double.NaN) =>
        Record("drawRoundRect", x, y, width, height, ellipseWidth, ellipseHeight);
}

public class Shape : DisplayObject
{
    public Graphics Graphics { get; } = new Graphics();
}

public class Sprite : DisplayObjectContainer
{
    public bool ButtonMode { get; set; }

    public bool UseHandCursor { get; set; } = true;

    public Graphics Graphics { get; } = new Graphics();

    public Sprite? HitArea { get; set; }

    public bool IsDragging { get; private set; }

    public bool DragLockCenter { get; private set; }

    public Rectangle? DragBounds { get; private set; }

    public void StartDrag(bool lockCenter = false, Rectangle? bounds = null)
    {
        IsDragging = true;
        DragLockCenter = lockCenter;
        DragBounds = bounds;
    }

    public void StopDrag()
    {
        IsDragging = false;
        DragLockCenter = false;
        DragBounds = null;
    }
}

public class FrameLabel
{
    public FrameLabel(string name, int frame)
    {
        Name = name;
        Frame = frame;
    }

    public string Name { get; }

    public int Frame { get; }
}

public class Scene
{
    public Scene(string name, int numFrames, AsArray? labels = null)
    {
        Name = name;
        NumFrames = numFrames;
        Labels = labels ?? new AsArray();
    }

    public string Name { get; }

    public int NumFrames { get; }

    public AsArray Labels { get; }
}

public class MovieClip : Sprite
{
    private int _currentFrame = 1;
    private int _totalFrames = 1;

    public int CurrentFrame => _currentFrame;

    public int TotalFrames
    {
        get => _totalFrames;
        set
        {
            _totalFrames = Math.Max(1, value);
            _currentFrame = Math.Min(_currentFrame, _totalFrames);
        }
    }

    public int FramesLoaded => _totalFrames;

    public AsArray FrameLabels { get; set; } = new AsArray();

    public bool IsPlaying { get; private set; } = true;

    public string? CurrentLabel
    {
        get
        {
            FrameLabel? best = null;
            foreach (object? item in FrameLabels)
            {
                if (item is FrameLabel label && label.Frame <= _currentFrame && (best == null || label.Frame >= best.Frame))
                {
                    best = label;
                }
            }
            return best?.Name;
        }
    }

    public void Play() => IsPlaying = true;

    public void Stop() => IsPlaying = false;

    public void GotoAndPlay(object? frame, string? scene = null)
    {
        _currentFrame = ResolveFrame(frame);
        IsPlaying = true;
    }

    public void GotoAndStop(object? frame, string? scene = null)
    {
        _currentFrame = ResolveFrame(frame);
        IsPlaying = false;
    }

    public void NextFrame()
    {
        IsPlaying = false;
        _currentFrame = Math.Min(_totalFrames, _currentFrame + 1);
    }

    public void PrevFrame()
    {
        IsPlaying = false;
        _currentFrame = Math.Max(1, _currentFrame - 1);
    }

    public void AdvanceFrame()
    {
        if (IsPlaying)
        {
            _currentFrame = _currentFrame < _totalFrames ? _currentFrame + 1 : 1;
        }
        DispatchEvent(new Event(Event.ENTER_FRAME));
    }

    private int ResolveFrame(object? frame)
    {
        if (!Ops.IsNumeric(frame))
        {
            string name = Ops.ToString(frame);
            foreach (object? item in FrameLabels)
            {
                if (item is FrameLabel label && label.Name == name)
                {
                    return label.Frame;
                }
            }
            throw new ArgumentException("Frame label not found: " + name);
        }
        double number = Ops.ToNumber(frame);
        if (double.IsNaN(number))
        {
            throw new ArgumentException("Frame number is NaN.");
        }
        return (int)Math.Max(1, Math.Min(_totalFrames, Math.Truncate(number)));
    }
}

public class Stage : DisplayObjectContainer
{
    public double StageWidth { get; set; }

    public double StageHeight { get; set; }

    public string Align { get; set; } = StageAlign.NONE;

    public string ScaleMode { get; set; } = StageScaleMode.SHOW_ALL;

    public string Quality { get; set; } = StageQuality.HIGH;

    public string DisplayState { get; set; } = StageDisplayState.NORMAL;

    public double FrameRate { get; set; } = 12;

    public InteractiveObject? Focus { get; set; }

    public double MouseGlobalX { get; private set; }

    public double MouseGlobalY { get; private set; }

    internal override bool IsStage => true;

    public void SetMouse(double x, double y)
    {
        MouseGlobalX = x;
        MouseGlobalY = y;
    }
}

public class Bitmap : DisplayObject
{
    public BitmapData? BitmapData { get; set; }

    public string PixelSnapping { get; set; } = "auto";

    public bool Smoothing { get; set; }
}

public class BitmapData
{
    public BitmapData(int width, int height, bool transparent = true, uint fillColor = 0xFFFFFFFF)
    {
        Width = width;
        Height = height;
        Transparent = transparent;
        FillColor = fillColor;
    }

    public int Width { get; }

    public int Height { get; }

    public bool Transparent { get; }

    public uint FillColor { get; }
}

public class SimpleButton : InteractiveObject
{
    public DisplayObject? UpState { get; set; }

    public DisplayObject? OverState { get; set; }

    public DisplayObject? DownState { get; set; }

    public DisplayObject? HitTestState { get; set; }

    public bool UseHandCursor { get; set; } = true;

    public bool TrackAsMenu { get; set; }
}

public class LoaderInfo : EventDispatcher
{
    public string Url { get; set; } = string.Empty;

    public DisplayObject? Content { get; set; }

    public Loader? Loader { get; set; }

    public uint BytesLoaded { get; set; }

    public uint BytesTotal { get; set; }

    public AsObject Parameters { get; set; } = new AsObject();
}

public class Loader : DisplayObjectContainer
{
    public DisplayObject? Content { get; set; }

    public LoaderInfo ContentLoaderInfo { get; } = new LoaderInfo();
}
