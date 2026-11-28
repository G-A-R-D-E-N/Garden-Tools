using System;
using System.Collections.Generic;
using System.Linq;
using Flash;
using Flash.Display;
using Flash.Events;
using Flash.External;
using Flash.Media;
using Flash.Net;
using Flash.System;
using Flash.Ui;
using Xunit;
using ByteArray = Flash.Utils.ByteArray;
using FlashDictionary = Flash.Utils.Dictionary;
using FlashTimer = Flash.Utils.Timer;
using NetGlobals = Flash.Net.Globals;
using Scheduler = Flash.Utils.Scheduler;
using SystemGlobals = Flash.System.Globals;
using UtilGlobals = Flash.Utils.Globals;

namespace Radicle.Interop.Tests;

[Collection("FlashStatics")]
public sealed class FlashServiceTests : IDisposable
{
    private readonly ApplicationDomain _domain;
    private double _now;

    public FlashServiceTests()
    {
        Scheduler.Reset();
        Scheduler.Clock = () => _now;
        ExternalInterface.Reset();
        _domain = ApplicationDomain.CurrentDomain;
        ApplicationDomain.CurrentDomain = new ApplicationDomain();
    }

    public void Dispose()
    {
        Scheduler.Reset();
        ExternalInterface.Reset();
        NetGlobals.Handler = null;
        SystemGlobals.Handler = null;
        Mouse.Show();
        ApplicationDomain.CurrentDomain = _domain;
    }

    [Fact]
    public void SchedulerFiresAnEntryOnceItsDelayHasPassed()
    {
        List<string> log = new List<string>();
        Action callback = () => log.Add("a");
        Scheduler.Schedule(callback, 100, false);
        _now = 99;
        Assert.Equal(0, Scheduler.Pump());
        _now = 100;
        Assert.Equal(1, Scheduler.Pump());
        Assert.Equal(0, Scheduler.Pump());
        Assert.Equal(new[] { "a" }, log);
    }

    [Fact]
    public void SchedulerRunsDueEntriesInDueOrderThenInsertionOrder()
    {
        List<string> log = new List<string>();
        Action slow = () => log.Add("slow");
        Action quick = () => log.Add("quick");
        Action twin = () => log.Add("twin");
        Scheduler.Schedule(slow, 20, false);
        Scheduler.Schedule(quick, 10, false);
        Scheduler.Schedule(twin, 20, false);
        _now = 20;
        Assert.Equal(3, Scheduler.Pump());
        Assert.Equal(new[] { "quick", "slow", "twin" }, log);
    }

    [Fact]
    public void RepeatingEntriesFireAtMostOncePerPumpAndRescheduleFromNow()
    {
        int calls = 0;
        Action callback = () => calls++;
        Scheduler.Schedule(callback, 50, true);
        _now = 50;
        Assert.Equal(1, Scheduler.Pump());
        Assert.Equal(0, Scheduler.Pump());
        _now = 300;
        Assert.Equal(1, Scheduler.Pump());
        Assert.Equal(2, calls);
        _now = 349;
        Assert.Equal(0, Scheduler.Pump());
        _now = 350;
        Assert.Equal(1, Scheduler.Pump());
    }

    [Fact]
    public void CancelAndTheOneMillisecondFloor()
    {
        int calls = 0;
        Action callback = () => calls++;
        int id = Scheduler.Schedule(callback, 10, false);
        Scheduler.Cancel(id);
        _now = 100;
        Assert.Equal(0, Scheduler.Pump());
        Assert.Equal(0, calls);
        _now = 200;
        Scheduler.Schedule(callback, 0, false);
        Assert.Equal(0, Scheduler.Pump());
        _now = 201;
        Assert.Equal(1, Scheduler.Pump());
        Assert.Equal(1, calls);
    }

    [Fact]
    public void ResetRestoresTheRealClock()
    {
        _now = -5;
        Scheduler.Reset();
        Assert.True(Scheduler.Now >= 0);
    }

    [Fact]
    public void TimerTicksThenCompletesAfterRepeatCount()
    {
        FlashTimer timer = new FlashTimer(100, 3);
        List<string> log = new List<string>();
        Action<Event> record = e => log.Add(e.Type);
        timer.AddEventListener(TimerEvent.TIMER, record);
        timer.AddEventListener(TimerEvent.TIMER_COMPLETE, record);
        timer.Start();
        timer.Start();
        Assert.True(timer.Running);
        _now = 100;
        Scheduler.Pump();
        Assert.Equal(1, timer.CurrentCount);
        _now = 200;
        Scheduler.Pump();
        _now = 300;
        Scheduler.Pump();
        Assert.Equal(new[] { "timer", "timer", "timer", "timerComplete" }, log);
        Assert.False(timer.Running);
        _now = 400;
        Assert.Equal(0, Scheduler.Pump());
        Assert.Equal(3, timer.CurrentCount);
    }

    [Fact]
    public void TimerWithoutRepeatCountKeepsTickingUntilStopped()
    {
        FlashTimer timer = new FlashTimer(10);
        timer.Start();
        for (int i = 1; i <= 5; i++)
        {
            _now = i * 10;
            Scheduler.Pump();
        }
        Assert.Equal(5, timer.CurrentCount);
        Assert.True(timer.Running);
        timer.Stop();
        _now = 100;
        Scheduler.Pump();
        Assert.Equal(5, timer.CurrentCount);
    }

    [Fact]
    public void TimerResetClearsTheCountAndCanRestart()
    {
        FlashTimer timer = new FlashTimer(100);
        timer.Start();
        _now = 100;
        Scheduler.Pump();
        timer.Reset();
        Assert.Equal(0, timer.CurrentCount);
        Assert.False(timer.Running);
        _now = 200;
        timer.Start();
        _now = 299;
        Assert.Equal(0, Scheduler.Pump());
        _now = 300;
        Assert.Equal(1, Scheduler.Pump());
        Assert.Equal(1, timer.CurrentCount);
    }

    [Fact]
    public void ChangingTheDelayOfARunningTimerReschedulesIt()
    {
        FlashTimer timer = new FlashTimer(100);
        timer.Start();
        _now = 50;
        timer.Delay = 200;
        _now = 100;
        Assert.Equal(0, Scheduler.Pump());
        _now = 250;
        Assert.Equal(1, Scheduler.Pump());
    }

    [Fact]
    public void TimerRejectsNegativeOrNaNDelays()
    {
        Assert.Throws<ArgumentOutOfRangeException>(() => new FlashTimer(-1));
        Assert.Throws<ArgumentOutOfRangeException>(() => new FlashTimer(double.NaN));
    }

    [Fact]
    public void SetTimeoutPassesBoundArgumentsAndFiresOnce()
    {
        List<string> log = new List<string>();
        Action<string, int> handler = (s, n) => log.Add(s + n);
        uint first = UtilGlobals.SetTimeout(handler, 50, "x", 7);
        uint second = UtilGlobals.SetTimeout(handler, 50, "y", 8);
        Assert.NotEqual(first, second);
        _now = 50;
        Scheduler.Pump();
        _now = 100;
        Scheduler.Pump();
        Assert.Equal(new[] { "x7", "y8" }, log);
    }

    [Fact]
    public void ClearTimeoutAndClearIntervalCancelPendingCalls()
    {
        int calls = 0;
        Action handler = () => calls++;
        uint timeout = UtilGlobals.SetTimeout(handler, 10);
        UtilGlobals.ClearTimeout(timeout);
        uint interval = UtilGlobals.SetInterval(handler, 10);
        _now = 10;
        Scheduler.Pump();
        _now = 20;
        Scheduler.Pump();
        Assert.Equal(2, calls);
        UtilGlobals.ClearInterval(interval);
        _now = 30;
        Scheduler.Pump();
        Assert.Equal(2, calls);
    }

    [Fact]
    public void GetTimerReadsTheSchedulerClockInWholeMilliseconds()
    {
        _now = 1234.7;
        Assert.Equal(1234, UtilGlobals.GetTimer());
    }

    [Fact]
    public void ExternalInterfaceWithoutAHostIsUnavailableAndReturnsNull()
    {
        Assert.False(ExternalInterface.Available);
        Assert.Null(ExternalInterface.Call("anything", 1, 2));
    }

    [Fact]
    public void ExternalInterfaceCallForwardsToTheHost()
    {
        List<string> calls = new List<string>();
        Func<string, object?[], object?> host = (name, args) =>
        {
            calls.Add(name + ":" + args.Length);
            return "ok";
        };
        ExternalInterface.Host = host;
        Assert.True(ExternalInterface.Available);
        Assert.Equal("ok", ExternalInterface.Call("f", 1, 2));
        Assert.Equal("ok", ExternalInterface.Call("g"));
        Assert.Equal("ok", ExternalInterface.Call("h", null));
        Assert.Equal(new[] { "f:2", "g:0", "h:1" }, calls);
    }

    [Fact]
    public void ExternalInterfaceCallbacksAreRegisteredAndInvokedByName()
    {
        Func<int, int, int> add = (a, b) => a + b;
        ExternalInterface.AddCallback("add", add);
        Assert.True(ExternalInterface.HasCallback("add"));
        Assert.False(ExternalInterface.HasCallback("sub"));
        Assert.Equal(5, ExternalInterface.InvokeCallback("add", 2, 3));
        Func<int, int, int> subtract = (a, b) => a - b;
        ExternalInterface.addCallback("add", subtract);
        Assert.Equal(-1, ExternalInterface.InvokeCallback("add", 2, 3));
        Assert.Throws<InvalidOperationException>(() => ExternalInterface.InvokeCallback("missing"));
    }

    [Fact]
    public void ExternalInterfaceResetClearsHostCallbacksAndObjectId()
    {
        Func<string, object?[], object?> host = (name, args) => null;
        Action noop = () => { };
        ExternalInterface.Host = host;
        ExternalInterface.AddCallback("x", noop);
        ExternalInterface.ObjectID = "movie";
        ExternalInterface.Reset();
        Assert.False(ExternalInterface.Available);
        Assert.False(ExternalInterface.HasCallback("x"));
        Assert.Equal(string.Empty, ExternalInterface.ObjectID);
    }

    [Fact]
    public void ApplicationDomainResolvesDefinitionsThroughItsParent()
    {
        ApplicationDomain parent = new ApplicationDomain();
        parent.SetDefinition("lib.Thing", typeof(string));
        ApplicationDomain child = new ApplicationDomain(parent);
        child.SetDefinition("own.Thing", typeof(int));
        Assert.Same(parent, child.ParentDomain);
        Assert.True(child.HasDefinition("lib.Thing"));
        Assert.False(parent.HasDefinition("own.Thing"));
        Assert.Equal(typeof(string), child.GetDefinition("lib.Thing"));
        Assert.Equal(typeof(int), child.GetDefinition("own.Thing"));
        Assert.Throws<ArgumentException>(() => child.GetDefinition("nope"));
    }

    [Fact]
    public void RegisterDefinitionAndGetDefinitionByNameUseTheCurrentDomain()
    {
        UtilGlobals.RegisterDefinition("game.Widget", typeof(Sprite));
        Assert.Equal(typeof(Sprite), UtilGlobals.GetDefinitionByName("game.Widget"));
        Assert.True(ApplicationDomain.CurrentDomain.HasDefinition("game.Widget"));
        Assert.Throws<ArgumentException>(() => UtilGlobals.GetDefinitionByName("game.Missing"));
    }

    [Fact]
    public void QualifiedClassNamesFollowActionScriptConventions()
    {
        Assert.Equal("String", UtilGlobals.GetQualifiedClassName("x"));
        Assert.Equal("Boolean", UtilGlobals.GetQualifiedClassName(true));
        Assert.Equal("int", UtilGlobals.GetQualifiedClassName(1));
        Assert.Equal("uint", UtilGlobals.GetQualifiedClassName(1u));
        Assert.Equal("Number", UtilGlobals.GetQualifiedClassName(1.5));
        Assert.Equal("null", UtilGlobals.GetQualifiedClassName(null));
        Assert.Equal("void", UtilGlobals.GetQualifiedClassName(Undefined.Value));
        Assert.Equal("Object", UtilGlobals.GetQualifiedClassName(new AsObject()));
        Assert.Equal("Array", UtilGlobals.GetQualifiedClassName(new AsArray()));
        Assert.Equal("Math", UtilGlobals.GetQualifiedClassName(typeof(AsMath)));
        Assert.Equal("__AS3__.vec::Vector.<int>", UtilGlobals.GetQualifiedClassName(new AsVector<int>()));
        Assert.Equal("__AS3__.vec::Vector.<String>", UtilGlobals.GetQualifiedClassName(new AsVector<string>()));
        Assert.Equal("flash.display::Sprite", UtilGlobals.GetQualifiedClassName(new Sprite()));
        Assert.Equal("flash.events::MouseEvent", UtilGlobals.GetQualifiedClassName(new MouseEvent("click")));
        Assert.Equal("flash.geom::Point", UtilGlobals.GetQualifiedClassName(typeof(Flash.Geom.Point)));
        Assert.Equal("flash.utils::Timer", UtilGlobals.GetQualifiedClassName(new FlashTimer(1)));
        Assert.Equal("scaleform.gfx::TextFieldEx", UtilGlobals.GetQualifiedClassName(typeof(Scaleform.Gfx.TextFieldEx)));
        Assert.Equal("__AS3__.vec::Vector.<flash.display::Sprite>",
                     UtilGlobals.GetQualifiedClassName(new AsVector<Sprite>()));
    }

    [Fact]
    public void DictionaryKeysUseStrictEqualityAndReferenceIdentity()
    {
        FlashDictionary d = new FlashDictionary();
        d[1] = "number";
        Assert.Equal("number", d[1.0]);
        Assert.Null(d["1"]);
        d["1"] = "text";
        Assert.Equal(2, d.Count);
        object first = new object();
        object second = new object();
        d[first] = "first";
        d[second] = "second";
        Assert.Equal("first", d[first]);
        Assert.Equal("second", d[second]);
        Assert.Equal(4, d.Count);
        Assert.True(d.HasOwnProperty(first));
        Assert.True(d.Remove(first));
        Assert.False(d.HasOwnProperty(first));
        Assert.False(d.Remove(first));
    }

    [Fact]
    public void DictionaryAcceptsANullKeyAndEnumeratesIt()
    {
        FlashDictionary d = new FlashDictionary(true);
        Assert.True(d.WeakKeys);
        d[null] = "nothing";
        d["a"] = 1;
        Assert.True(d.HasOwnProperty(null));
        Assert.Equal("nothing", d[null]);
        List<object?> keys = d.Keys.ToList();
        Assert.Equal(2, keys.Count);
        Assert.Single(keys, k => k == null);
        List<KeyValuePair<object?, object?>> pairs = d.ToList();
        Assert.Equal(2, pairs.Count);
        Assert.Contains(pairs, pair => pair.Key == null && (string?)pair.Value == "nothing");
    }

    [Fact]
    public void ByteArrayLengthPositionAndBytesAvailable()
    {
        ByteArray bytes = new ByteArray();
        Assert.Equal(0u, bytes.Length);
        bytes.Length = 4;
        Assert.Equal(new byte[] { 0, 0, 0, 0 }, bytes.ToArray());
        bytes.Position = 1;
        Assert.Equal(3u, bytes.BytesAvailable);
        bytes.Position = 9;
        Assert.Equal(0u, bytes.BytesAvailable);
        bytes.Length = 2;
        Assert.Equal(2u, bytes.Length);
        ByteArray seeded = new ByteArray(new byte[] { 1, 2, 3 });
        Assert.Equal(3u, seeded.Length);
        Assert.Equal(new byte[] { 1, 2, 3 }, seeded.ToArray());
    }

    [Fact]
    public void NavigateToUrlGoesThroughTheHandlerAndIsANoOpWithoutOne()
    {
        URLRequest request = new URLRequest("http://example.invalid/page");
        NetGlobals.NavigateToUrl(request, "_blank");
        URLRequest? seen = null;
        string? window = null;
        NetGlobals.Handler = (r, w) =>
        {
            seen = r;
            window = w;
        };
        NetGlobals.NavigateToUrl(request, "_blank");
        Assert.Same(request, seen);
        Assert.Equal("_blank", window);
        Assert.Equal("GET", request.Method);
        URLLoader loader = new URLLoader(request);
        Assert.Same(request, loader.Request);
        Assert.Equal("text", loader.DataFormat);
    }

    [Fact]
    public void FscommandGoesThroughTheHandler()
    {
        List<string> log = new List<string>();
        SystemGlobals.Fscommand("ignored");
        SystemGlobals.Handler = (command, args) => log.Add(command + "|" + args);
        SystemGlobals.Fscommand("quit");
        SystemGlobals.Fscommand("set", "1");
        Assert.Equal(new[] { "quit|", "set|1" }, log);
    }

    [Fact]
    public void CapabilitiesAreSettablePlaceholders()
    {
        string os = Capabilities.Os;
        try
        {
            Capabilities.Os = "Linux";
            Assert.Equal("Linux", Capabilities.Os);
        }
        finally
        {
            Capabilities.Os = os;
        }
        Assert.Equal(os, Capabilities.Os);
    }

    [Fact]
    public void KeyboardCodesAndMouseVisibility()
    {
        Assert.Equal(13u, Keyboard.ENTER);
        Assert.Equal(27u, Keyboard.ESCAPE);
        Assert.Equal(32u, Keyboard.SPACE);
        Assert.Equal(37u, Keyboard.LEFT);
        Assert.Equal(40u, Keyboard.DOWN);
        Assert.Equal(48u, Keyboard.NUMBER_0);
        Assert.Equal(112u, Keyboard.F1);
        Assert.Equal(123u, Keyboard.F12);
        Assert.False(Mouse.IsHidden);
        Mouse.Hide();
        Assert.True(Mouse.IsHidden);
        Mouse.Show();
        Assert.False(Mouse.IsHidden);
        Assert.Equal("auto", Mouse.Cursor);
    }

    [Fact]
    public void SoundPlayRecordsTheChannelStateWithoutPlayingAudio()
    {
        Sound sound = new Sound();
        SoundTransform transform = new SoundTransform(0.5, -1);
        SoundChannel channel = sound.Play(500, 1, transform);
        Assert.Equal(500.0, channel.Position);
        Assert.Same(transform, channel.SoundTransform);
        Assert.False(channel.Stopped);
        channel.Stop();
        Assert.True(channel.Stopped);
        Assert.Equal(1.0, sound.Play().SoundTransform.Volume);
    }
}
