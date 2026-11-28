using Radicle.Interop.Native;
using System;
using System.IO;
using Xunit;

namespace Radicle.Interop.Tests;

public class PapyrusEngineNativeTests
{
    private static PapyrusRuntime Create(Func<PapyrusNativeCall, bool> handler, PapyrusNativeFlags flags = 0)
    {
        var runtime = PapyrusProbe.Create();
        try
        {
            runtime.RegisterNativeEx("Probe", "Hit", PapyrusNativeKind.Static, string.Empty, (uint)flags);
            runtime.RegisterNativeEx("Probe", "Wait", PapyrusNativeKind.Static, string.Empty, (uint)flags);
            runtime.SetEngineCallbacks(new PapyrusEngineHandlers(handler));
        }
        catch
        {
            runtime.Dispose();
            throw;
        }
        return runtime;
    }

    [Fact]
    public void SetResultStoresTheValueAndTheViewDescribesTheCall()
    {
        string? script = null;
        string? function = null;
        PapyrusObjectRef? self = null;
        var args = -1;
        var accepted = false;
        using var runtime = Create(call =>
        {
            script = call.Script;
            function = call.Function;
            self = call.SelfRef;
            args = call.Args.Count;
            accepted = call.SetResult(42);
            return true;
        });
        var instance = PapyrusProbe.Ready(runtime, PapyrusProbe.Actor);
        Assert.Equal(PapyrusStatus.Ok, runtime.SendEvent(instance, "Ping"));
        Assert.Equal(1, runtime.Step(512).Completed);
        Assert.True(accepted);
        Assert.Equal("Probe", script, ignoreCase: true);
        Assert.Equal("Hit", function, ignoreCase: true);
        Assert.Null(self);
        Assert.Equal(0, args);
        Assert.True(runtime.TryGetVariable(instance, "total", out var total));
        Assert.Equal(42L, Convert.ToInt64(total));
    }

    [Fact]
    public void TheCallViewIsClosedOnceTheCallbackReturns()
    {
        PapyrusNativeCall? kept = null;
        using var runtime = Create(call =>
        {
            kept = call;
            return call.SetResult(1);
        });
        var instance = PapyrusProbe.Ready(runtime, PapyrusProbe.Actor);
        Assert.Equal(PapyrusStatus.Ok, runtime.SendEvent(instance, "Ping"));
        Assert.Equal(1, runtime.Step(512).Completed);
        var view = kept!;
        Assert.Equal("Hit", view.Function, ignoreCase: true);
        Assert.Throws<InvalidOperationException>(() => { _ = view.Args; });
        Assert.Throws<InvalidOperationException>(() => { _ = view.SetResult(2); });
        Assert.Throws<InvalidOperationException>(() => { _ = view.TryBeginWait(out _); });
        Assert.Throws<InvalidOperationException>(() => view.Fail(PapyrusStatus.HostError));
    }

    [Fact]
    public void BeginWaitParksTheRunUntilCompleteLatentAndRefusesASecondWait()
    {
        var first = false;
        var second = true;
        ulong token = 0;
        using var runtime = Create(call =>
        {
            first = call.TryBeginWait(out token);
            second = call.TryBeginWait(out _);
            return true;
        }, PapyrusNativeFlags.MayWait);
        var instance = PapyrusProbe.Ready(runtime, PapyrusProbe.Actor);
        Assert.Equal(PapyrusStatus.Ok, runtime.SendEvent(instance, "Run"));
        Assert.Equal(1, runtime.Step(512).Waiting);
        Assert.True(first);
        Assert.False(second);
        Assert.NotEqual(0UL, token);
        Assert.True(runtime.CompleteLatent(token, 3));
        Assert.Equal(1, runtime.Step(512).Completed);
        Assert.True(runtime.TryGetVariable(instance, "total", out var total));
        Assert.Equal(3L, Convert.ToInt64(total));
        Assert.False(runtime.CompleteLatent(token, 4));
    }

    [Fact]
    public void BeginWaitIsRefusedWhenTheNativeWasNotRegisteredToWait()
    {
        var began = true;
        using var runtime = Create(call =>
        {
            began = call.TryBeginWait(out _);
            return call.SetResult(1);
        });
        var instance = PapyrusProbe.Ready(runtime, PapyrusProbe.Actor);
        Assert.Equal(PapyrusStatus.Ok, runtime.SendEvent(instance, "Run"));
        Assert.Equal(1, runtime.Step(512).Completed);
        Assert.False(began);
        Assert.True(runtime.TryGetVariable(instance, "total", out var total));
        Assert.Equal(1L, Convert.ToInt64(total));
    }

    [Fact]
    public void FailRefusesTheCallAndTheTrackedCallEndsFailed()
    {
        using var runtime = Create(call =>
        {
            call.Fail(PapyrusStatus.HostError);
            return false;
        });
        var instance = PapyrusProbe.Ready(runtime, PapyrusProbe.Actor);
        var (ticket, status) = runtime.CallFunction(instance, "Ping");
        Assert.Equal(PapyrusStatus.Ok, status);
        Assert.Equal(1, runtime.Step(512).Refused);
        var result = runtime.CallResult(ticket, consume: false);
        Assert.True(result.Found);
        Assert.Equal(PapyrusCallState.Failed, result.State);
    }

    [Fact]
    public void AThrowingEngineNativeSurfacesFromTheStepThatRanIt()
    {
        using var runtime = Create(call => throw new InvalidOperationException("engine native failed"));
        var instance = PapyrusProbe.Ready(runtime, PapyrusProbe.Actor);
        Assert.Equal(PapyrusStatus.Ok, runtime.SendEvent(instance, "Ping"));
        var thrown = Assert.Throws<InvalidOperationException>(() => runtime.Step(512));
        Assert.Equal("engine native failed", thrown.Message);
        Assert.Null(Xunit.Record.Exception(() => runtime.Step(512)));
    }

    [Fact]
    public void ClearingTheEngineCallbacksFailsTheNativeAndReinstallingRestoresIt()
    {
        using var runtime = Create(call => call.SetResult(1));
        var instance = PapyrusProbe.Ready(runtime, PapyrusProbe.Actor);
        runtime.SetEngineCallbacks(null);
        var (cleared, clearedStatus) = runtime.CallFunction(instance, "Ping");
        Assert.Equal(PapyrusStatus.Ok, clearedStatus);
        Assert.Equal(1, runtime.Step(512).Refused);
        Assert.Equal(PapyrusCallState.Failed, runtime.CallResult(cleared, consume: true).State);

        runtime.SetEngineCallbacks(new PapyrusEngineHandlers(call => call.SetResult(2)));
        var (restored, restoredStatus) = runtime.CallFunction(instance, "Ping");
        Assert.Equal(PapyrusStatus.Ok, restoredStatus);
        Assert.Equal(1, runtime.Step(512).Completed);
        Assert.Equal(PapyrusCallState.Done, runtime.CallResult(restored, consume: true).State);
    }

    [Fact]
    public void RegisterNativeExRefusesInvalidFlagCombinations()
    {
        using var runtime = PapyrusProbe.Create();
        var scriptInstance = Assert.Throws<RadicleException>(() => runtime.RegisterNativeEx(
            "Probe", "Hit", PapyrusNativeKind.Static, string.Empty, (uint)PapyrusNativeFlags.AcceptScriptInstance));
        Assert.Equal(Status.InvalidArgument, scriptInstance.Status);
        var unknown = Assert.Throws<RadicleException>(() => runtime.RegisterNativeEx(
            "Probe", "Hit", PapyrusNativeKind.Static, string.Empty, 0x80));
        Assert.Equal(Status.InvalidArgument, unknown.Status);
    }

    [Fact]
    public void StructArgumentsAreBuiltMemberByMemberAndBadMembersAreRefused()
    {
        const string source = "Scriptname StructProbe\nStruct Point\n Int X\n String Label\nEndStruct\n" +
            "Int Function ReadX(Point value)\n Return value.X\nEndFunction\n" +
            "String Function ReadLabel(Point value)\n Return value.Label\nEndFunction\n";
        using var compiled = PapyrusCompiler.CompileSource(System.Text.Encoding.UTF8.GetBytes(source), "StructProbe.psc").Pex;
        Assert.NotNull(compiled);
        using var runtime = new PapyrusRuntime();
        runtime.AddPex(compiled!.ToBytes());
        var instance = runtime.Attach("StructProbe", PapyrusProbe.Actor);
        Assert.Equal(PapyrusStatus.Ok, runtime.Initialize(instance));
        var point = new PapyrusStructValue("StructProbe#Point", new System.Collections.Generic.Dictionary<string, object?>
        {
            ["X"] = 41, ["Label"] = "lbl",
        });
        var (ticket, status) = runtime.CallFunction(instance, "ReadX", point);
        Assert.Equal(PapyrusStatus.Ok, status);
        runtime.Step(512);
        Assert.Equal(41, Convert.ToInt32(runtime.CallResult(ticket, consume: true).Value));
        (ticket, status) = runtime.CallFunction(instance, "ReadLabel", point);
        Assert.Equal(PapyrusStatus.Ok, status);
        runtime.Step(512);
        Assert.Equal("lbl", runtime.CallResult(ticket, consume: true).Value);
        var duplicates = new PapyrusStructValue("StructProbe#Point", new System.Collections.Generic.Dictionary<string, object?> { ["X"] = 1, ["x"] = 2 });
        Assert.Throws<ArgumentException>(() => runtime.CallFunction(instance, "ReadX", duplicates));
        var bad = new PapyrusStructValue("Point", new System.Collections.Generic.Dictionary<string, object?> { ["X"] = DateTime.UnixEpoch });
        Assert.Throws<ArgumentException>(() => runtime.SendEvent(instance, "Ping", bad));
    }

    private static PapyrusRuntime CastRuntime(Func<PapyrusObjectRef, string, ulong?> castHandle)
    {
        var runtime = new PapyrusRuntime();
        try
        {
            var imports = Path.Combine(Path.GetTempPath(), "radicle-cast-" + Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(imports);
            try
            {
                File.WriteAllText(Path.Combine(imports, "ScriptObject.psc"), "Scriptname ScriptObject\n");
                File.WriteAllText(Path.Combine(imports, "Form.psc"), "Scriptname Form extends ScriptObject\n");
                File.WriteAllText(Path.Combine(imports, "Counter.psc"), "Scriptname Counter extends Form\nInt total\nInt Function Bump()\n  total = total + 1\n  return total\nEndFunction\n");
                foreach (var (name, source) in new[] { ("Counter", File.ReadAllText(Path.Combine(imports, "Counter.psc"))), ("Caller", "Scriptname Caller\nInt Function Run(Form target)\n  Counter held = target as Counter\n  return held.Bump()\nEndFunction\n") })
                {
                    var result = PapyrusCompiler.CompileSource(System.Text.Encoding.UTF8.GetBytes(source), name + ".psc", imports: imports);
                    using var compiled = result.Pex;
                    Assert.True(compiled is not null, string.Join("; ", result.Diagnostics));
                    runtime.AddPex(compiled!.ToBytes());
                }
            }
            finally { Directory.Delete(imports, recursive: true); }
            runtime.SetEngineCallbacks(new PapyrusEngineHandlers(call => false));
            runtime.SetCastHandler(castHandle);
        }
        catch
        {
            runtime.Dispose();
            throw;
        }
        return runtime;
    }

    private static object? RunCaller(PapyrusRuntime runtime, ulong caller, PapyrusObjectRef target)
    {
        var (ticket, status) = runtime.CallFunction(caller, "Run", target);
        Assert.Equal(PapyrusStatus.Ok, status);
        runtime.Step(512);
        var result = runtime.CallResult(ticket, consume: true);
        Assert.Equal(PapyrusCallState.Done, result.State);
        return result.Value;
    }

    [Fact]
    public void ACastHandlerLetsAnEngineHandleRunTheScriptInstanceItNames()
    {
        PapyrusObjectRef? seen = null;
        string? asked = null;
        ulong counter = 0;
        using var runtime = CastRuntime((target, type) =>
        {
            seen = target;
            asked = type;
            return counter;
        });
        counter = runtime.Attach("Counter", PapyrusProbe.Quest);
        Assert.Equal(PapyrusStatus.Ok, runtime.Initialize(counter));
        var caller = runtime.Attach("Caller", PapyrusProbe.Actor);
        Assert.Equal(PapyrusStatus.Ok, runtime.Initialize(caller));
        var quest = new PapyrusObjectRef(PapyrusRuntime.EngineHandleBit | 0x2A, "", "");
        Assert.Equal(1L, Convert.ToInt64(RunCaller(runtime, caller, quest)));
        Assert.Equal(2L, Convert.ToInt64(RunCaller(runtime, caller, quest)));
        Assert.True(runtime.TryGetVariable(counter, "total", out var total));
        Assert.Equal(2L, Convert.ToInt64(total));
        Assert.Equal(quest, seen);
        Assert.Equal("Counter", asked, ignoreCase: true);
    }

    [Fact]
    public void ACastHandlerThatDeclinesLeavesTheEngineHandleUntyped()
    {
        using var runtime = CastRuntime((_, _) => null);
        var caller = runtime.Attach("Caller", PapyrusProbe.Actor);
        Assert.Equal(PapyrusStatus.Ok, runtime.Initialize(caller));
        var quest = new PapyrusObjectRef(PapyrusRuntime.EngineHandleBit | 0x2A, "", "");
        var (ticket, status) = runtime.CallFunction(caller, "Run", quest);
        Assert.Equal(PapyrusStatus.Ok, status);
        runtime.Step(512);
        var failed = runtime.CallResult(ticket, consume: true);
        Assert.Equal(PapyrusCallState.Failed, failed.State);
        Assert.Equal(PapyrusStatus.FunctionNotFound, failed.Status);
        Assert.Contains("Bump", failed.Detail);
    }

    [Fact]
    public void AThrowingCastHandlerSurfacesFromTheStepThatRanIt()
    {
        using var runtime = CastRuntime((_, _) => throw new InvalidOperationException("cast failed"));
        var caller = runtime.Attach("Caller", PapyrusProbe.Actor);
        Assert.Equal(PapyrusStatus.Ok, runtime.Initialize(caller));
        runtime.CallFunction(caller, "Run", new PapyrusObjectRef(PapyrusRuntime.EngineHandleBit | 0x2A, "", ""));
        var thrown = Assert.Throws<InvalidOperationException>(() => runtime.Step(512));
        Assert.Equal("cast failed", thrown.Message);
    }

    [Fact]
    public void SetEngineCallbacksOnADisposedRuntimeThrowsObjectDisposed()
    {
        var runtime = PapyrusProbe.Create();
        runtime.Dispose();
        Assert.Throws<ObjectDisposedException>(() => runtime.SetEngineCallbacks(new PapyrusEngineHandlers(call => true)));
        Assert.Throws<ObjectDisposedException>(() => runtime.SetEngineCallbacks(null));
    }
}
