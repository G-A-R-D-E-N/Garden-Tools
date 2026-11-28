using System;
using System.Collections.Generic;
using Flash.Display;
using Flash.Events;
using Xunit;

namespace Radicle.Interop.Tests;

public class FlashEventTests
{
    private sealed class Host : IEventDispatcher
    {
        private readonly EventDispatcher _inner;

        public Host()
        {
            _inner = new EventDispatcher(this);
        }

        public void AddEventListener(string type, Delegate listener, bool useCapture = false, int priority = 0,
                                     bool useWeakReference = false) =>
            _inner.AddEventListener(type, listener, useCapture, priority, useWeakReference);

        public void RemoveEventListener(string type, Delegate listener, bool useCapture = false) =>
            _inner.RemoveEventListener(type, listener, useCapture);

        public bool DispatchEvent(Event e) => _inner.DispatchEvent(e);

        public bool HasEventListener(string type) => _inner.HasEventListener(type);

        public bool WillTrigger(string type) => _inner.WillTrigger(type);
    }

    private static Action<Event> Mark(List<string> log, string label) => e => log.Add(label);

    private static Action<Event> MarkPhase(List<string> log, string label) =>
        e => log.Add(label + ":" + e.EventPhase);

    [Fact]
    public void ConstantsMatchTheActionScriptStrings()
    {
        Assert.Equal("enterFrame", Event.ENTER_FRAME);
        Assert.Equal("addedToStage", Event.ADDED_TO_STAGE);
        Assert.Equal("removedFromStage", Event.REMOVED_FROM_STAGE);
        Assert.Equal("click", MouseEvent.CLICK);
        Assert.Equal("mouseDown", MouseEvent.MOUSE_DOWN);
        Assert.Equal("rollOver", MouseEvent.ROLL_OVER);
        Assert.Equal("keyDown", KeyboardEvent.KEY_DOWN);
        Assert.Equal("focusIn", FocusEvent.FOCUS_IN);
        Assert.Equal("timerComplete", TimerEvent.TIMER_COMPLETE);
        Assert.Equal("ioError", IOErrorEvent.IO_ERROR);
        Assert.Equal("progress", ProgressEvent.PROGRESS);
        Assert.Equal(1u, EventPhase.CAPTURING_PHASE);
        Assert.Equal(2u, EventPhase.AT_TARGET);
        Assert.Equal(3u, EventPhase.BUBBLING_PHASE);
    }

    [Fact]
    public void EventStoresItsFlagsAndCloneKeepsThem()
    {
        Event e = new Event("custom", true, true);
        Assert.Equal("custom", e.Type);
        Assert.True(e.Bubbles);
        Assert.True(e.Cancelable);
        Assert.Equal(0u, e.EventPhase);
        Event copy = e.Clone();
        Assert.NotSame(e, copy);
        Assert.Equal("custom", copy.Type);
        Assert.True(copy.Bubbles);
        Assert.True(copy.Cancelable);
        Assert.Equal("[Event type=\"custom\" bubbles=true cancelable=true eventPhase=0]", e.ToString());
    }

    [Fact]
    public void ListenerReceivesTheEventWithTargetAndPhase()
    {
        EventDispatcher d = new EventDispatcher();
        Event? seen = null;
        uint phase = 0;
        object? current = null;
        Action<Event> handler = e =>
        {
            seen = e;
            phase = e.EventPhase;
            current = e.CurrentTarget;
        };
        d.AddEventListener("ping", handler);
        Assert.True(d.HasEventListener("ping"));
        Assert.False(d.HasEventListener("pong"));
        Event ping = new Event("ping");
        Assert.True(d.DispatchEvent(ping));
        Assert.Same(ping, seen);
        Assert.Same(d, ping.Target);
        Assert.Same(d, current);
        Assert.Equal(EventPhase.AT_TARGET, phase);
    }

    [Fact]
    public void DuplicateRegistrationIsIgnoredUnlessTheCaptureFlagDiffers()
    {
        Stage stage = new Stage();
        Sprite child = new Sprite();
        stage.AddChild(child);
        int calls = 0;
        Action<Event> handler = e => calls++;
        stage.AddEventListener("x", handler);
        stage.AddEventListener("x", handler);
        child.DispatchEvent(new Event("x", true));
        Assert.Equal(1, calls);
        stage.AddEventListener("x", handler, true);
        child.DispatchEvent(new Event("x", true));
        Assert.Equal(3, calls);
    }

    [Fact]
    public void RemovedListenersStopFiringAndTheTypeIsForgotten()
    {
        EventDispatcher d = new EventDispatcher();
        int calls = 0;
        Action<Event> handler = e => calls++;
        d.AddEventListener("x", handler);
        d.RemoveEventListener("x", handler, true);
        d.DispatchEvent(new Event("x"));
        Assert.Equal(1, calls);
        d.RemoveEventListener("x", handler);
        d.DispatchEvent(new Event("x"));
        Assert.Equal(1, calls);
        Assert.False(d.HasEventListener("x"));
        d.RemoveEventListener("never", handler);
    }

    [Fact]
    public void HigherPriorityRunsFirstAndTiesKeepInsertionOrder()
    {
        EventDispatcher d = new EventDispatcher();
        List<string> log = new List<string>();
        d.AddEventListener("x", Mark(log, "low"), false, -5);
        d.AddEventListener("x", Mark(log, "high"), false, 10);
        d.AddEventListener("x", Mark(log, "mid1"), false, 0);
        d.AddEventListener("x", Mark(log, "mid2"), false, 0);
        d.DispatchEvent(new Event("x"));
        Assert.Equal(new[] { "high", "mid1", "mid2", "low" }, log);
    }

    [Fact]
    public void StopImmediatePropagationSkipsTheRestOfTheNodeButStopPropagationDoesNot()
    {
        EventDispatcher d = new EventDispatcher();
        List<string> log = new List<string>();
        Action<Event> immediate = e => e.StopImmediatePropagation();
        d.AddEventListener("now", immediate, false, 5);
        d.AddEventListener("now", Mark(log, "after"));
        d.DispatchEvent(new Event("now"));
        Assert.Empty(log);

        Action<Event> polite = e => e.StopPropagation();
        d.AddEventListener("soft", polite, false, 5);
        d.AddEventListener("soft", Mark(log, "after"));
        d.DispatchEvent(new Event("soft"));
        Assert.Equal(new[] { "after" }, log);
    }

    [Fact]
    public void PreventDefaultOnlyWorksOnCancelableEvents()
    {
        EventDispatcher d = new EventDispatcher();
        Action<Event> veto = e => e.PreventDefault();
        d.AddEventListener("x", veto);
        Event cancelable = new Event("x", false, true);
        Assert.False(d.DispatchEvent(cancelable));
        Assert.True(cancelable.IsDefaultPrevented());
        Event plain = new Event("x");
        Assert.True(d.DispatchEvent(plain));
        Assert.False(plain.IsDefaultPrevented());
    }

    [Fact]
    public void ListenerMayRemoveItselfAndListenersAddedDuringDispatchWaitForTheNextOne()
    {
        EventDispatcher d = new EventDispatcher();
        List<string> log = new List<string>();
        Action<Event> late = e => log.Add("late");
        Action<Event>? once = null;
        once = e =>
        {
            log.Add("once");
            d.RemoveEventListener("x", once!);
            d.AddEventListener("x", late);
        };
        d.AddEventListener("x", once, false, 5);
        d.AddEventListener("x", Mark(log, "steady"));
        d.DispatchEvent(new Event("x"));
        Assert.Equal(new[] { "once", "steady" }, log);
        log.Clear();
        d.DispatchEvent(new Event("x"));
        Assert.Equal(new[] { "steady", "late" }, log);
    }

    [Fact]
    public void ListenersMayDeclareFewerOrMoreParametersThanTheEvent()
    {
        EventDispatcher d = new EventDispatcher();
        int none = 0;
        Action bare = () => none++;
        Func<Event, object?, int> wide = (e, extra) => 1;
        d.AddEventListener("x", bare);
        d.AddEventListener("x", wide);
        d.DispatchEvent(new Event("x"));
        Assert.Equal(1, none);
    }

    [Fact]
    public void ListenerExceptionsReachTheDispatcher()
    {
        EventDispatcher d = new EventDispatcher();
        Action<Event> boom = e => throw new InvalidOperationException("boom");
        d.AddEventListener("x", boom);
        Assert.Throws<InvalidOperationException>(() => d.DispatchEvent(new Event("x")));
    }

    [Fact]
    public void DelegatingDispatcherReportsTheWrapperAsTarget()
    {
        Host host = new Host();
        object? target = null;
        Action<Event> handler = e => target = e.Target;
        host.AddEventListener("x", handler);
        host.DispatchEvent(new Event("x"));
        Assert.Same(host, target);
        Assert.True(host.WillTrigger("x"));
    }

    [Fact]
    public void DispatchWalksCaptureThenTargetThenBubbleUpTheDisplayList()
    {
        Stage stage = new Stage();
        Sprite a = new Sprite();
        Sprite b = new Sprite();
        stage.AddChild(a);
        a.AddChild(b);
        List<string> log = new List<string>();
        stage.AddEventListener("ping", MarkPhase(log, "stage-capture"), true);
        stage.AddEventListener("ping", MarkPhase(log, "stage-bubble"));
        a.AddEventListener("ping", MarkPhase(log, "a-capture"), true);
        a.AddEventListener("ping", MarkPhase(log, "a-bubble"));
        b.AddEventListener("ping", MarkPhase(log, "b-capture"), true);
        b.AddEventListener("ping", MarkPhase(log, "b-target"));
        Event bubbling = new Event("ping", true);
        b.DispatchEvent(bubbling);
        Assert.Equal(
            new[] { "stage-capture:1", "a-capture:1", "b-target:2", "a-bubble:3", "stage-bubble:3" }, log);
        Assert.Same(b, bubbling.Target);
        log.Clear();
        b.DispatchEvent(new Event("ping", false));
        Assert.Equal(new[] { "stage-capture:1", "a-capture:1", "b-target:2" }, log);
    }

    [Fact]
    public void StopPropagationEndsAfterTheCurrentNode()
    {
        Stage stage = new Stage();
        Sprite a = new Sprite();
        Sprite b = new Sprite();
        stage.AddChild(a);
        a.AddChild(b);
        List<string> log = new List<string>();
        Action<Event> stop = e => e.StopPropagation();
        a.AddEventListener("ping", stop, true);
        a.AddEventListener("ping", Mark(log, "a-capture-second"), true);
        b.AddEventListener("ping", Mark(log, "b"));
        stage.AddEventListener("ping", Mark(log, "stage-bubble"));
        b.DispatchEvent(new Event("ping", true));
        Assert.Equal(new[] { "a-capture-second" }, log);

        log.Clear();
        a.RemoveEventListener("ping", stop, true);
        a.AddEventListener("ping", stop);
        b.DispatchEvent(new Event("ping", true));
        Assert.Equal(new[] { "a-capture-second", "b" }, log);
    }

    [Fact]
    public void WillTriggerLooksUpTheParentChain()
    {
        Stage stage = new Stage();
        Sprite child = new Sprite();
        stage.AddChild(child);
        stage.AddEventListener("ping", Mark(new List<string>(), "x"));
        Assert.True(child.WillTrigger("ping"));
        Assert.False(child.HasEventListener("ping"));
        Assert.False(child.WillTrigger("pong"));
    }

    [Fact]
    public void MouseEventDefaultsAndClone()
    {
        MouseEvent click = new MouseEvent(MouseEvent.CLICK);
        Assert.True(click.Bubbles);
        Assert.False(click.Cancelable);
        Assert.True(double.IsNaN(click.LocalX));
        MouseEvent full = new MouseEvent(MouseEvent.MOUSE_WHEEL, false, true, 3, 4, null, true, false, true, true, -2);
        full.StageX = 30;
        full.StageY = 40;
        MouseEvent copy = Assert.IsType<MouseEvent>(full.Clone());
        Assert.Equal("mouseWheel", copy.Type);
        Assert.False(copy.Bubbles);
        Assert.True(copy.Cancelable);
        Assert.Equal(3.0, copy.LocalX);
        Assert.Equal(4.0, copy.LocalY);
        Assert.Equal(30.0, copy.StageX);
        Assert.Equal(40.0, copy.StageY);
        Assert.True(copy.CtrlKey);
        Assert.False(copy.AltKey);
        Assert.True(copy.ShiftKey);
        Assert.True(copy.ButtonDown);
        Assert.Equal(-2, copy.Delta);
    }

    [Fact]
    public void KeyboardAndFocusEventsClone()
    {
        KeyboardEvent key = new KeyboardEvent(KeyboardEvent.KEY_DOWN, true, false, 97, 65, 1, true, false, true);
        KeyboardEvent keyCopy = Assert.IsType<KeyboardEvent>(key.Clone());
        Assert.Equal(97u, keyCopy.CharCode);
        Assert.Equal(65u, keyCopy.KeyCode);
        Assert.Equal(1u, keyCopy.KeyLocation);
        Assert.True(keyCopy.CtrlKey);
        Assert.True(keyCopy.ShiftKey);
        Sprite related = new Sprite();
        FocusEvent focus = new FocusEvent(FocusEvent.FOCUS_OUT, true, false, related, true, 9);
        FocusEvent focusCopy = Assert.IsType<FocusEvent>(focus.Clone());
        Assert.Same(related, focusCopy.RelatedObject);
        Assert.True(focusCopy.ShiftKey);
        Assert.Equal(9u, focusCopy.KeyCode);
    }

    [Fact]
    public void ErrorProgressTimerAndFullScreenEventsClone()
    {
        IOErrorEvent io = new IOErrorEvent(IOErrorEvent.IO_ERROR, false, false, "boom", 2032);
        IOErrorEvent ioCopy = Assert.IsType<IOErrorEvent>(io.Clone());
        Assert.Equal("boom", ioCopy.Text);
        Assert.Equal(2032, ioCopy.ErrorID);
        ErrorEvent error = new ErrorEvent(ErrorEvent.ERROR, false, false, "bad", 7);
        Assert.IsType<ErrorEvent>(error.Clone());
        ProgressEvent progress = new ProgressEvent(ProgressEvent.PROGRESS, false, false, 10, 40);
        ProgressEvent progressCopy = Assert.IsType<ProgressEvent>(progress.Clone());
        Assert.Equal(10.0, progressCopy.BytesLoaded);
        Assert.Equal(40.0, progressCopy.BytesTotal);
        TimerEvent timer = new TimerEvent(TimerEvent.TIMER);
        Assert.IsType<TimerEvent>(timer.Clone());
        TextEvent link = new TextEvent(TextEvent.LINK, false, false, "event:go");
        Assert.Equal("event:go", Assert.IsType<TextEvent>(link.Clone()).Text);
        FullScreenEvent screen = new FullScreenEvent(FullScreenEvent.FULL_SCREEN, false, false, true);
        Assert.True(Assert.IsType<FullScreenEvent>(screen.Clone()).FullScreen);
    }
}
