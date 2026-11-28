using Radicle.Interop.Native;
using System;
using System.Collections.Generic;
using Xunit;

namespace Radicle.Interop.Tests;

public class PapyrusRuntimeCallbackTests
{
    private static PapyrusNativeHandler Hit(List<string> calls, object? value) =>
        (string script, string function, IReadOnlyList<object?> args, out object? result) =>
        {
            calls.Add(function);
            result = value;
            return true;
        };

    private static PapyrusOverrideHandler Override(PapyrusOverrideOutcome verdict, object? value, bool handled, List<(ulong, string)> seen) =>
        (ulong instance, string function, IReadOnlyList<object?> args, out PapyrusOverrideOutcome outcome, out object? result) =>
        {
            seen.Add((instance, function));
            outcome = verdict;
            result = value;
            return handled;
        };

    [Fact]
    public void TheGateRefusesInitializeUntilItAllows()
    {
        var keys = new List<string>();
        var allow = false;
        using var runtime = PapyrusProbe.Create(new PapyrusHostCallbacks(Gate: key =>
        {
            keys.Add(key);
            return allow;
        }));
        var instance = runtime.Attach("Probe", PapyrusProbe.Actor);
        Assert.Equal(PapyrusStatus.GateRefused, runtime.Initialize(instance));
        Assert.Equal(PapyrusLifecycle.NotInitialized, runtime.Lifecycle(instance));
        allow = true;
        Assert.Equal(PapyrusStatus.Ok, runtime.Initialize(instance));
        Assert.Equal(PapyrusLifecycle.Ready, runtime.Lifecycle(instance));
        Assert.Equal(new[] { "vm.lifecycle.oninit_barrier", "vm.lifecycle.oninit_barrier" }, keys);
    }

    [Fact]
    public void AThrowingGateSurfacesFromInitializeAndLeavesTheNextCallClean()
    {
        var fail = true;
        using var runtime = PapyrusProbe.Create(new PapyrusHostCallbacks(Gate: key =>
        {
            if (fail) throw new InvalidOperationException("gate failed");
            return true;
        }));
        var instance = runtime.Attach("Probe", PapyrusProbe.Actor);
        var thrown = Assert.Throws<InvalidOperationException>(() => runtime.Initialize(instance));
        Assert.Equal("gate failed", thrown.Message);
        Assert.Equal(PapyrusLifecycle.NotInitialized, runtime.Lifecycle(instance));
        fail = false;
        Assert.Equal(PapyrusStatus.Ok, runtime.Initialize(instance));
    }

    [Fact]
    public void AnEventRunsTheManagedNativeCallbackAndStoresItsResult()
    {
        var calls = new List<string>();
        using var runtime = PapyrusProbe.Create(new PapyrusHostCallbacks(CallNative: Hit(calls, 42)));
        runtime.RegisterNative("Probe", "Hit");
        var instance = PapyrusProbe.Ready(runtime, PapyrusProbe.Actor);
        Assert.Equal(PapyrusLifecycle.Ready, runtime.Lifecycle(instance));
        Assert.Equal(PapyrusStatus.Ok, runtime.SendEvent(instance, "Ping"));
        Assert.Equal(1, runtime.Step(512).Completed);
        Assert.Equal(new[] { "Hit" }, calls);
        Assert.True(runtime.TryGetVariable(instance, "total", out var total));
        Assert.Equal(42L, Convert.ToInt64(total));
    }

    [Fact]
    public void ALatentNativeParksTheRunUntilItIsCompletedOnce()
    {
        using var runtime = PapyrusProbe.Create(new PapyrusHostCallbacks(BeginLatent: (_, _, _) => 9));
        runtime.RegisterNative("Probe", "Wait", latent: true);
        var instance = PapyrusProbe.Ready(runtime, PapyrusProbe.Actor);
        Assert.Equal(PapyrusStatus.Ok, runtime.SendEvent(instance, "Run"));
        Assert.Equal(1, runtime.Step(512).Waiting);
        Assert.True(runtime.CompleteLatent(9, 3));
        Assert.Equal(1, runtime.Step(512).Completed);
        Assert.True(runtime.TryGetVariable(instance, "total", out var total));
        Assert.Equal(3L, Convert.ToInt64(total));
        Assert.False(runtime.CompleteLatent(9, 4));
    }

    [Fact]
    public void AHandledOverrideSettlesATrackedCallWithoutRunningTheBody()
    {
        var seen = new List<(ulong, string)>();
        var calls = new List<string>();
        using var runtime = PapyrusProbe.Create(new PapyrusHostCallbacks(
            CallNative: Hit(calls, 1),
            Override: Override(PapyrusOverrideOutcome.Handled, 7, true, seen)));
        runtime.RegisterNative("Probe", "Hit");
        runtime.RegisterOverride("Probe", "Ping");
        var instance = PapyrusProbe.Ready(runtime, PapyrusProbe.Actor);
        var (ticket, status) = runtime.CallFunction(instance, "Ping");
        Assert.Equal(PapyrusStatus.Ok, status);
        var result = runtime.CallResult(ticket, consume: true);
        Assert.True(result.Found);
        Assert.Equal(PapyrusCallState.Done, result.State);
        Assert.Equal(7L, Convert.ToInt64(result.Value));
        Assert.Equal(new[] { (instance, "Ping") }, seen);
        Assert.Empty(calls);
        Assert.Equal(1, runtime.GetStats().OverridesHandled);
    }

    [Fact]
    public void ADeclinedOverrideRunsTheScriptBody()
    {
        var seen = new List<(ulong, string)>();
        var calls = new List<string>();
        using var runtime = PapyrusProbe.Create(new PapyrusHostCallbacks(
            CallNative: Hit(calls, 5),
            Override: Override(PapyrusOverrideOutcome.Declined, null, true, seen)));
        runtime.RegisterNative("Probe", "Hit");
        runtime.RegisterOverride("Probe", "Ping");
        var instance = PapyrusProbe.Ready(runtime, PapyrusProbe.Actor);
        Assert.Equal(PapyrusStatus.Ok, runtime.SendEvent(instance, "Ping"));
        Assert.Equal(1, runtime.Step(512).Completed);
        Assert.Single(calls);
        Assert.Equal(1, runtime.GetStats().OverridesDeclined);
    }

    [Fact]
    public void ARefusedOverrideRefusesTheEvent()
    {
        var seen = new List<(ulong, string)>();
        var calls = new List<string>();
        using var runtime = PapyrusProbe.Create(new PapyrusHostCallbacks(
            CallNative: Hit(calls, 5),
            Override: Override(PapyrusOverrideOutcome.Refused, null, true, seen)));
        runtime.RegisterNative("Probe", "Hit");
        runtime.RegisterOverride("Probe", "Ping");
        var instance = PapyrusProbe.Ready(runtime, PapyrusProbe.Actor);
        Assert.Equal(PapyrusStatus.HostError, runtime.SendEvent(instance, "Ping"));
        Assert.Equal(0, runtime.Step(512).Completed);
        Assert.Empty(calls);
        Assert.Equal(1, runtime.GetStats().OverridesRefused);
    }

    [Fact]
    public void AnOverrideThatReturnsFalseLetsTheScriptBodyRun()
    {
        var seen = new List<(ulong, string)>();
        var calls = new List<string>();
        using var runtime = PapyrusProbe.Create(new PapyrusHostCallbacks(
            CallNative: Hit(calls, 1),
            Override: Override(PapyrusOverrideOutcome.Handled, 99, false, seen)));
        runtime.RegisterNative("Probe", "Hit");
        runtime.RegisterOverride("Probe", "Ping");
        var instance = PapyrusProbe.Ready(runtime, PapyrusProbe.Actor);
        Assert.Equal(PapyrusStatus.Ok, runtime.SendEvent(instance, "Ping"));
        Assert.Equal(1, runtime.Step(512).Completed);
        Assert.Single(calls);
        Assert.Single(seen);
    }

    [Fact]
    public void AThrowingOverrideSurfacesFromTheSendEventThatRanIt()
    {
        using var runtime = PapyrusProbe.Create(new PapyrusHostCallbacks(
            Override: (ulong instance, string function, IReadOnlyList<object?> args, out PapyrusOverrideOutcome outcome, out object? result) =>
            {
                throw new InvalidOperationException("override failed");
            }));
        runtime.RegisterOverride("Probe", "Ping");
        var instance = PapyrusProbe.Ready(runtime, PapyrusProbe.Actor);
        var thrown = Assert.Throws<InvalidOperationException>(() => runtime.SendEvent(instance, "Ping"));
        Assert.Equal("override failed", thrown.Message);
    }

    [Fact]
    public void AThrowingNativeHandlerSurfacesFromTheStepThatRanItOnce()
    {
        using var runtime = PapyrusProbe.Create(new PapyrusHostCallbacks(
            CallNative: (string script, string function, IReadOnlyList<object?> args, out object? result) =>
            {
                throw new InvalidOperationException("native failed");
            }));
        runtime.RegisterNative("Probe", "Hit");
        var instance = PapyrusProbe.Ready(runtime, PapyrusProbe.Actor);
        Assert.Equal(PapyrusStatus.Ok, runtime.SendEvent(instance, "Ping"));
        var thrown = Assert.Throws<InvalidOperationException>(() => runtime.Step(512));
        Assert.Equal("native failed", thrown.Message);
        Assert.Null(Xunit.Record.Exception(() => runtime.Step(512)));
    }

    [Fact]
    public void AThrowingNativeHandlerSurfacesFromRunCall()
    {
        using var runtime = PapyrusProbe.Create(new PapyrusHostCallbacks(
            CallNative: (string script, string function, IReadOnlyList<object?> args, out object? result) =>
            {
                throw new InvalidOperationException("native failed");
            }));
        runtime.RegisterNative("Probe", "Hit");
        var instance = PapyrusProbe.Ready(runtime, PapyrusProbe.Actor);
        var (ticket, status) = runtime.CallFunction(instance, "Ping");
        Assert.Equal(PapyrusStatus.Ok, status);
        var thrown = Assert.Throws<InvalidOperationException>(() => runtime.RunCall(ticket, 512));
        Assert.Equal("native failed", thrown.Message);
    }

    [Fact]
    public void DisposeFromACallbackIsRefusedWhileTheNativeCallIsRunning()
    {
        PapyrusRuntime? runtime = null;
        runtime = PapyrusProbe.Create(new PapyrusHostCallbacks(
            CallNative: (string script, string function, IReadOnlyList<object?> args, out object? result) =>
            {
                result = null;
                runtime!.Dispose();
                return true;
            }));
        try
        {
            runtime.RegisterNative("Probe", "Hit");
            var instance = PapyrusProbe.Ready(runtime, PapyrusProbe.Actor);
            Assert.Equal(PapyrusStatus.Ok, runtime.SendEvent(instance, "Ping"));
            Assert.Throws<InvalidOperationException>(() => runtime!.Step(512));
            Assert.True(runtime.Detach(instance));
        }
        finally { runtime.Dispose(); }
    }
}
