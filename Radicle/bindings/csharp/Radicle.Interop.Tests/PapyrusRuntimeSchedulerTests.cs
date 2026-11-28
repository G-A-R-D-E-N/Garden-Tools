using Radicle.Interop.Native;
using System;
using System.Collections.Generic;
using System.Text;
using Xunit;

namespace Radicle.Interop.Tests;

public class PapyrusRuntimeSchedulerTests
{
    private const string BooterSource =
        "Scriptname Booter\nFunction Arrive() global native\n" +
        "Function XxInit()\n  Arrive()\nEndFunction\nFunction Poke()\nEndFunction\n";

    private static byte[] CompileBooter()
    {
        using var compiled = PapyrusCompiler.CompileSource(Encoding.UTF8.GetBytes(BooterSource), "Booter.psc", noDebugInfo: true).Pex;
        Assert.NotNull(compiled);
        return PapyrusProbe.Patch(compiled!.ToBytes(), "XxInit", "OnInit");
    }

    private static object? RoundTrip(PapyrusRuntime runtime, ulong instance, object? value)
    {
        runtime.SetVariable(instance, "total", value);
        Assert.True(runtime.TryGetVariable(instance, "total", out var read));
        return read;
    }

    [Fact]
    public void ARealTimeTimerFiresOncePerWholeIntervalAndANonRepeatingTimerRetires()
    {
        using var runtime = PapyrusProbe.Create();
        var instance = runtime.Attach("Probe", PapyrusProbe.Actor);
        var repeating = runtime.RegisterTimer(instance, 1.0, 1, PapyrusClock.RealTime);
        var once = runtime.RegisterTimer(instance, 1.0, 0, PapyrusClock.RealTime);
        Assert.Equal(3, runtime.AdvanceClock("GameSession", 2.5, PapyrusClock.RealTime));
        Assert.Equal(1, runtime.AdvanceClock("GameSession", 1.0, PapyrusClock.RealTime));
        Assert.False(runtime.Unregister(once));
        Assert.True(runtime.Unregister(repeating));
    }

    [Fact]
    public void TheClockRefusesForeignOwnersNegativeTimeAndUnknownInstancesAndSkipsOtherClocks()
    {
        using var runtime = PapyrusProbe.Create();
        var instance = runtime.Attach("Probe", PapyrusProbe.Actor);
        var game = runtime.RegisterTimer(instance, 1.0, 1, PapyrusClock.GameTime);
        Assert.Throws<RadicleException>(() => runtime.AdvanceClock("Other", 1.0, PapyrusClock.RealTime));
        Assert.Throws<RadicleException>(() => runtime.AdvanceClock("GameSession", -1.0, PapyrusClock.RealTime));
        Assert.Equal(0, runtime.AdvanceClock("GameSession", 5.0, PapyrusClock.RealTime));
        Assert.True(runtime.Unregister(game));
        Assert.Throws<RadicleException>(() => runtime.RegisterTimer(instance + 100, 1.0, 1, PapyrusClock.RealTime));
    }

    [Fact]
    public void RemoteEventsMatchEveryRegisteredTargetInHandleOrderUntilUnregisteredOrDetached()
    {
        using var runtime = PapyrusProbe.Create();
        var first = runtime.Attach("Probe", PapyrusProbe.Actor);
        var second = runtime.Attach("Probe", PapyrusProbe.Other);
        var firstHandle = runtime.RegisterRemoteEvent(first, PapyrusProbe.Quest, "Quest.OnStageSet");
        var secondHandle = runtime.RegisterRemoteEvent(second, PapyrusProbe.Quest, "quest.onstageset");

        var matched = runtime.MatchRemoteEvent(PapyrusProbe.Quest, "QUEST.ONSTAGESET");
        Assert.Equal(new[] { new PapyrusRemoteTarget(firstHandle, first), new PapyrusRemoteTarget(secondHandle, second) }, matched);

        Assert.True(runtime.Unregister(firstHandle));
        Assert.Equal(new[] { new PapyrusRemoteTarget(secondHandle, second) }, runtime.MatchRemoteEvent(PapyrusProbe.Quest, "Quest.OnStageSet"));
        Assert.True(runtime.Detach(second));
        Assert.Empty(runtime.MatchRemoteEvent(PapyrusProbe.Quest, "Quest.OnStageSet"));
    }

    [Fact]
    public void ACustomEventRegistrationUnregistersOnce()
    {
        using var runtime = PapyrusProbe.Create();
        var instance = runtime.Attach("Probe", PapyrusProbe.Actor);
        var handle = runtime.RegisterCustomEvent(instance, "MyEvent");
        Assert.True(runtime.Unregister(handle));
        Assert.False(runtime.Unregister(handle));
    }

    [Fact]
    public void ScalarValuesRoundTripThroughAnInstanceVariable()
    {
        using var runtime = PapyrusProbe.Create();
        var instance = runtime.Attach("Probe", PapyrusProbe.Actor);
        Assert.Equal(1.5f, Assert.IsType<float>(RoundTrip(runtime, instance, 1.5f)));
        Assert.Equal(2.25f, Assert.IsType<float>(RoundTrip(runtime, instance, 2.25d)));
        Assert.True(Assert.IsType<bool>(RoundTrip(runtime, instance, true)));
        Assert.False(Assert.IsType<bool>(RoundTrip(runtime, instance, false)));
        Assert.Equal("héllo", Assert.IsType<string>(RoundTrip(runtime, instance, "héllo")));
        Assert.Equal(7L, Convert.ToInt64(RoundTrip(runtime, instance, 7)));
        Assert.Null(RoundTrip(runtime, instance, null));
    }

    [Fact]
    public void ObjectAndArrayValuesRoundTripThroughAnInstanceVariable()
    {
        using var runtime = PapyrusProbe.Create();
        var instance = runtime.Attach("Probe", PapyrusProbe.Actor);
        var reference = new PapyrusObjectRef(7, "Actor", "Idle");
        Assert.Equal(reference, Assert.IsType<PapyrusObjectRef>(RoundTrip(runtime, instance, reference)));
        var items = new object?[] { 1, "two", true, null, 2.5f, reference };
        var read = Assert.IsType<object?[]>(RoundTrip(runtime, instance, items));
        Assert.Equal(items.Length, read.Length);
        Assert.Equal(1L, Convert.ToInt64(read[0]));
        Assert.Equal("two", read[1]);
        Assert.True(Assert.IsType<bool>(read[2]));
        Assert.Null(read[3]);
        Assert.Equal(2.5f, Assert.IsType<float>(read[4]));
        Assert.Equal(reference, read[5]);
        Assert.Empty(Assert.IsType<object?[]>(RoundTrip(runtime, instance, Array.Empty<object?>())));
    }

    [Fact]
    public void AValueTheRuntimeCannotHoldIsRefusedBeforeTheNativeCall()
    {
        using var runtime = PapyrusProbe.Create();
        var instance = runtime.Attach("Probe", PapyrusProbe.Actor);
        Assert.Throws<ArgumentException>(() => runtime.SetVariable(instance, "total", new object()));
    }

    [Fact]
    public void AnUnknownInstanceHasNoVariableOrProperty()
    {
        using var runtime = PapyrusProbe.Create();
        var instance = runtime.Attach("Probe", PapyrusProbe.Actor);
        Assert.False(runtime.TryGetVariable(instance + 100, "total", out _));
        Assert.False(runtime.TryGetProperty(instance + 100, "Count", out _));
        Assert.False(runtime.TryGetProperty(instance, "Missing", out _));
    }

    [Fact]
    public void AttachWithPropertiesReadsBackWithoutCaseAndRefusesDuplicateNames()
    {
        using var runtime = PapyrusProbe.Create();
        var instance = runtime.AttachWithProperties("Probe", PapyrusProbe.Actor, new Dictionary<string, object?> { ["Count"] = 5 });
        Assert.True(runtime.TryGetProperty(instance, "COUNT", out var count));
        Assert.Equal(5L, Convert.ToInt64(count));
        Assert.Equal(PapyrusLifecycle.NotInitialized, runtime.Lifecycle(instance));
        var thrown = Assert.Throws<RadicleException>(() => runtime.AttachWithProperties("Probe", PapyrusProbe.Other,
            new Dictionary<string, object?> { ["Count"] = 1, ["COUNT"] = 2 }));
        Assert.Equal(Status.InvalidArgument, thrown.Status);
    }

    [Fact]
    public void InstanceStateIsTheAutoStateName()
    {
        using var compiled = PapyrusCompiler.CompileSource(
            Encoding.UTF8.GetBytes("Scriptname Staged\nAuto State Idle\n    Function Poke()\n    EndFunction\nEndState\n"), "Staged.psc").Pex;
        Assert.NotNull(compiled);
        using var runtime = new PapyrusRuntime();
        runtime.AddPex(compiled!.ToBytes());
        var instance = runtime.Attach("Staged", PapyrusProbe.Actor);
        Assert.Equal("Idle", runtime.InstanceState(instance));
    }

    [Fact]
    public void FailureAndInstanceStateReadTheInstanceRecord()
    {
        using var runtime = PapyrusProbe.Create();
        var healthy = PapyrusProbe.Ready(runtime, PapyrusProbe.Actor);
        Assert.Null(runtime.Failure(healthy));
        Assert.Equal("no such instance", runtime.Failure(healthy + 100));
        Assert.Equal(string.Empty, runtime.InstanceState(healthy + 100));

        runtime.RegisterOverride("Probe", "OnInit");
        var refused = runtime.Attach("Probe", PapyrusProbe.Other);
        Assert.Equal(PapyrusStatus.HostError, runtime.Initialize(refused));
        Assert.Equal(PapyrusLifecycle.Failed, runtime.Lifecycle(refused));
        Assert.Equal("the script does not define OnInit", runtime.Failure(refused));
    }

    [Fact]
    public void AFreshRuntimeReportsZeroStats()
    {
        using var runtime = PapyrusProbe.Create();
        var stats = runtime.GetStats();
        Assert.Equal(0, stats.Completed);
        Assert.Equal(0, stats.Refused);
        Assert.Equal(0, stats.Dropped);
        Assert.Equal(0, stats.QueuedDuringOninit);
        Assert.Equal(0, stats.OverridesHandled);
    }

    [Fact]
    public void AnEventForAMissingFunctionIsRefusedAndCounted()
    {
        using var runtime = PapyrusProbe.Create();
        var instance = PapyrusProbe.Ready(runtime, PapyrusProbe.Actor);
        Assert.Equal(PapyrusStatus.FunctionNotFound, runtime.SendEvent(instance, "Missing"));
        Assert.Equal(1, runtime.GetStats().Refused);
    }

    [Fact]
    public void AnEventDuringOnInitIsRefusedByDefault()
    {
        var seen = new List<PapyrusStatus>();
        var (runtime, instance) = Boot(queue: false, seen);
        using var guard = runtime;
        Assert.Equal(PapyrusStatus.Ok, runtime.Initialize(instance));
        Assert.Equal(PapyrusLifecycle.Ready, runtime.Lifecycle(instance));
        Assert.Equal(new[] { PapyrusStatus.GateRefused }, seen);
        Assert.Equal(1, runtime.GetStats().Refused);
        Assert.Equal(0, runtime.Step(512).Completed);
    }

    [Fact]
    public void AnEventDuringOnInitIsQueuedWhenThePolicyQueues()
    {
        var seen = new List<PapyrusStatus>();
        var (runtime, instance) = Boot(queue: true, seen);
        using var guard = runtime;
        Assert.Equal(PapyrusStatus.Ok, runtime.Initialize(instance));
        Assert.Equal(new[] { PapyrusStatus.Ok }, seen);
        Assert.Equal(1, runtime.GetStats().QueuedDuringOninit);
        Assert.Equal(1, runtime.Step(512).Completed);
    }

    // Invariant: OnInit runs inside Initialize, so a callback is the only point where an instance is observably Initializing.
    private static (PapyrusRuntime Runtime, ulong Instance) Boot(bool queue, List<PapyrusStatus> seen)
    {
        PapyrusRuntime? runtime = null;
        ulong instance = 0;
        runtime = new PapyrusRuntime(new PapyrusHostCallbacks(CallNative: (string script, string function, IReadOnlyList<object?> args, out object? result) =>
        {
            seen.Add(runtime!.SendEvent(instance, "Poke"));
            result = null;
            return true;
        }));
        runtime.AddPex(CompileBooter());
        runtime.RegisterNative("Booter", "Arrive");
        runtime.SetOninitPolicy(queue);
        instance = runtime.Attach("Booter", PapyrusProbe.Actor);
        return (runtime, instance);
    }
}
