using Flash.Display;

namespace Flash.Events;

public static class EventPhase
{
    public const uint CAPTURING_PHASE = 1;
    public const uint AT_TARGET = 2;
    public const uint BUBBLING_PHASE = 3;
}

public class Event
{
    public const string ACTIVATE = "activate";
    public const string ADDED = "added";
    public const string ADDED_TO_STAGE = "addedToStage";
    public const string CANCEL = "cancel";
    public const string CHANGE = "change";
    public const string CLOSE = "close";
    public const string COMPLETE = "complete";
    public const string CONNECT = "connect";
    public const string DEACTIVATE = "deactivate";
    public const string ENTER_FRAME = "enterFrame";
    public const string EXIT_FRAME = "exitFrame";
    public const string FULLSCREEN = "fullScreen";
    public const string INIT = "init";
    public const string OPEN = "open";
    public const string REMOVED = "removed";
    public const string REMOVED_FROM_STAGE = "removedFromStage";
    public const string RENDER = "render";
    public const string RESIZE = "resize";
    public const string SCROLL = "scroll";
    public const string SELECT = "select";
    public const string UNLOAD = "unload";

    internal bool StopAfterNode;
    internal bool StopNow;
    internal bool Prevented;

    public Event(string type, bool bubbles = false, bool cancelable = false)
    {
        Type = type;
        Bubbles = bubbles;
        Cancelable = cancelable;
    }

    public string Type { get; }

    public bool Bubbles { get; }

    public bool Cancelable { get; }

    public object? Target { get; internal set; }

    public object? CurrentTarget { get; internal set; }

    public uint EventPhase { get; internal set; }

    public void StopPropagation() => StopAfterNode = true;

    public void StopImmediatePropagation()
    {
        StopAfterNode = true;
        StopNow = true;
    }

    public void PreventDefault()
    {
        if (Cancelable)
        {
            Prevented = true;
        }
    }

    public bool IsDefaultPrevented() => Prevented;

    public virtual Event Clone() => new Event(Type, Bubbles, Cancelable);

    public override string ToString() =>
        "[Event type=\"" + Type + "\" bubbles=" + (Bubbles ? "true" : "false") + " cancelable=" +
        (Cancelable ? "true" : "false") + " eventPhase=" + EventPhase + "]";
}

public class MouseEvent : Event
{
    public const string CLICK = "click";
    public const string DOUBLE_CLICK = "doubleClick";
    public const string MOUSE_DOWN = "mouseDown";
    public const string MOUSE_MOVE = "mouseMove";
    public const string MOUSE_OUT = "mouseOut";
    public const string MOUSE_OVER = "mouseOver";
    public const string MOUSE_UP = "mouseUp";
    public const string MOUSE_WHEEL = "mouseWheel";
    public const string ROLL_OUT = "rollOut";
    public const string ROLL_OVER = "rollOver";

    public MouseEvent(string type, bool bubbles = true, bool cancelable = false, double localX = double.NaN,
                      double localY = double.NaN, InteractiveObject? relatedObject = null, bool ctrlKey = false,
                      bool altKey = false, bool shiftKey = false, bool buttonDown = false, int delta = 0)
        : base(type, bubbles, cancelable)
    {
        LocalX = localX;
        LocalY = localY;
        RelatedObject = relatedObject;
        CtrlKey = ctrlKey;
        AltKey = altKey;
        ShiftKey = shiftKey;
        ButtonDown = buttonDown;
        Delta = delta;
    }

    public double LocalX { get; set; }

    public double LocalY { get; set; }

    public double StageX { get; set; }

    public double StageY { get; set; }

    public InteractiveObject? RelatedObject { get; set; }

    public bool CtrlKey { get; set; }

    public bool AltKey { get; set; }

    public bool ShiftKey { get; set; }

    public bool ButtonDown { get; set; }

    public int Delta { get; set; }

    public override Event Clone()
    {
        MouseEvent copy = new MouseEvent(Type, Bubbles, Cancelable, LocalX, LocalY, RelatedObject, CtrlKey, AltKey,
                                         ShiftKey, ButtonDown, Delta);
        copy.StageX = StageX;
        copy.StageY = StageY;
        return copy;
    }
}

public class KeyboardEvent : Event
{
    public const string KEY_DOWN = "keyDown";
    public const string KEY_UP = "keyUp";

    public KeyboardEvent(string type, bool bubbles = true, bool cancelable = false, uint charCode = 0, uint keyCode = 0,
                         uint keyLocation = 0, bool ctrlKey = false, bool altKey = false, bool shiftKey = false)
        : base(type, bubbles, cancelable)
    {
        CharCode = charCode;
        KeyCode = keyCode;
        KeyLocation = keyLocation;
        CtrlKey = ctrlKey;
        AltKey = altKey;
        ShiftKey = shiftKey;
    }

    public uint CharCode { get; set; }

    public uint KeyCode { get; set; }

    public uint KeyLocation { get; set; }

    public bool CtrlKey { get; set; }

    public bool AltKey { get; set; }

    public bool ShiftKey { get; set; }

    public override Event Clone() =>
        new KeyboardEvent(Type, Bubbles, Cancelable, CharCode, KeyCode, KeyLocation, CtrlKey, AltKey, ShiftKey);
}

public class FocusEvent : Event
{
    public const string FOCUS_IN = "focusIn";
    public const string FOCUS_OUT = "focusOut";
    public const string KEY_FOCUS_CHANGE = "keyFocusChange";
    public const string MOUSE_FOCUS_CHANGE = "mouseFocusChange";

    public FocusEvent(string type, bool bubbles = true, bool cancelable = false, InteractiveObject? relatedObject = null,
                      bool shiftKey = false, uint keyCode = 0)
        : base(type, bubbles, cancelable)
    {
        RelatedObject = relatedObject;
        ShiftKey = shiftKey;
        KeyCode = keyCode;
    }

    public InteractiveObject? RelatedObject { get; set; }

    public bool ShiftKey { get; set; }

    public uint KeyCode { get; set; }

    public override Event Clone() => new FocusEvent(Type, Bubbles, Cancelable, RelatedObject, ShiftKey, KeyCode);
}

public class TextEvent : Event
{
    public const string LINK = "link";
    public const string TEXT_INPUT = "textInput";

    public TextEvent(string type, bool bubbles = false, bool cancelable = false, string text = "")
        : base(type, bubbles, cancelable)
    {
        Text = text;
    }

    public string Text { get; set; }

    public override Event Clone() => new TextEvent(Type, Bubbles, Cancelable, Text);
}

public class ErrorEvent : TextEvent
{
    public const string ERROR = "error";

    public ErrorEvent(string type, bool bubbles = false, bool cancelable = false, string text = "", int id = 0)
        : base(type, bubbles, cancelable, text)
    {
        ErrorID = id;
    }

    public int ErrorID { get; set; }

    public override Event Clone() => new ErrorEvent(Type, Bubbles, Cancelable, Text, ErrorID);
}

public class IOErrorEvent : ErrorEvent
{
    public const string IO_ERROR = "ioError";

    public IOErrorEvent(string type, bool bubbles = false, bool cancelable = false, string text = "", int id = 0)
        : base(type, bubbles, cancelable, text, id)
    {
    }

    public override Event Clone() => new IOErrorEvent(Type, Bubbles, Cancelable, Text, ErrorID);
}

public class ProgressEvent : Event
{
    public const string PROGRESS = "progress";

    public ProgressEvent(string type, bool bubbles = false, bool cancelable = false, double bytesLoaded = 0,
                         double bytesTotal = 0)
        : base(type, bubbles, cancelable)
    {
        BytesLoaded = bytesLoaded;
        BytesTotal = bytesTotal;
    }

    public double BytesLoaded { get; set; }

    public double BytesTotal { get; set; }

    public override Event Clone() => new ProgressEvent(Type, Bubbles, Cancelable, BytesLoaded, BytesTotal);
}

public class TimerEvent : Event
{
    public const string TIMER = "timer";
    public const string TIMER_COMPLETE = "timerComplete";

    public TimerEvent(string type, bool bubbles = false, bool cancelable = false)
        : base(type, bubbles, cancelable)
    {
    }

    public override Event Clone() => new TimerEvent(Type, Bubbles, Cancelable);
}

public class FullScreenEvent : Event
{
    public const string FULL_SCREEN = "fullScreen";

    public FullScreenEvent(string type, bool bubbles = false, bool cancelable = false, bool fullScreen = false)
        : base(type, bubbles, cancelable)
    {
        FullScreen = fullScreen;
    }

    public bool FullScreen { get; set; }

    public override Event Clone() => new FullScreenEvent(Type, Bubbles, Cancelable, FullScreen);
}
