using Radicle.Interop.Native;
using System;
using System.Collections.Generic;
using System.Text;
using Xunit;

namespace Radicle.Interop.Tests;

public class PapyrusRuntimeWrapperTests
{
    private const string Source = "Scriptname Give\nInt Property Count Auto\nInt Function Value()\n  Return 41\nEndFunction\n";
    private static readonly PapyrusOwner Owner = new(PapyrusOwnerKind.Form, "000800:Give.esp");

    private static byte[] Compile()
    {
        using var compiled = PapyrusCompiler.CompileSource(Encoding.UTF8.GetBytes(Source), "Give.psc").Pex;
        Assert.NotNull(compiled);
        return compiled!.ToBytes();
    }

    private static void ExpectValue(PapyrusRuntime runtime, ulong instance)
    {
        var (ticket, status) = runtime.CallFunction(instance, "Value");
        Assert.Equal(PapyrusStatus.Ok, status);
        runtime.Step(1000);
        var result = runtime.CallResult(ticket, consume: true);
        Assert.True(result.Found);
        Assert.Equal(41L, Convert.ToInt64(result.Value));
    }

    [Fact]
    public void StatusNameMatchesTheManagedEnumAndMapsUnknownValues()
    {
        foreach (var status in Enum.GetValues<PapyrusStatus>()) Assert.Equal(status.ToString(), status.Name());
        Assert.Equal("Unknown", ((PapyrusStatus)999).Name());
    }

    [Fact]
    public void CallFunctionReadsItsReturnValueAfterStep()
    {
        using var runtime = new PapyrusRuntime();
        runtime.AddPex(Compile());
        Assert.Equal(1, runtime.PexCount);
        Assert.True(runtime.HasScript("Give"));
        var instance = runtime.Attach("Give", Owner);
        Assert.Equal("Give", runtime.InstanceScript(instance), ignoreCase: true);
        Assert.Contains(instance, runtime.ListInstances());
        Assert.True(runtime.InstanceDeclares(instance, "Value"));
        Assert.Equal(PapyrusStatus.Ok, runtime.Initialize(instance));

        var (ticket, status) = runtime.CallFunction(instance, "Value");
        Assert.Equal(PapyrusStatus.Ok, status);
        runtime.Step(1000);
        var result = runtime.CallResult(ticket, consume: true);
        Assert.True(result.Found);
        Assert.Equal(PapyrusCallState.Done, result.State);
        Assert.Equal(41L, Convert.ToInt64(result.Value));
        Assert.False(runtime.CallResult(ticket, consume: false).Found);
    }

    [Fact]
    public void AttachWithPropertiesSeedsTheProperty()
    {
        using var runtime = new PapyrusRuntime();
        runtime.AddPex(Compile());
        var instance = runtime.AttachWithProperties("Give", Owner, new Dictionary<string, object?> { ["Count"] = 7 });
        Assert.True(runtime.TryGetProperty(instance, "Count", out var value));
        Assert.Equal(7L, Convert.ToInt64(value));
        Assert.False(runtime.Detach(instance + 100));
        Assert.True(runtime.Detach(instance));
    }

    [Fact]
    public void UnknownScriptThrowsRadicleException()
    {
        using var runtime = new PapyrusRuntime();
        Assert.False(runtime.HasScript("Missing"));
        var thrown = Assert.Throws<RadicleException>(() => runtime.Attach("Missing", Owner));
        Assert.IsAssignableFrom<InvalidOperationException>(thrown);
    }

    [Fact]
    public void AddPexBytesOnADisposedRuntimeThrowsObjectDisposed()
    {
        var runtime = new PapyrusRuntime();
        runtime.Dispose();
        Assert.Throws<ObjectDisposedException>(() => runtime.AddPex(Compile()));
        Assert.Throws<ObjectDisposedException>(() => runtime.AddPex(new byte[] { 0 }));
    }

    [Fact]
    public void AddPexOfJunkBytesThrowsRadicleExceptionAndKeepsTheCount()
    {
        using var runtime = new PapyrusRuntime();
        Assert.Throws<RadicleException>(() => runtime.AddPex(new byte[] { 1, 2, 3 }));
        Assert.Equal(0, runtime.PexCount);
    }

    [Fact]
    public void AddPexFileKeepsTheNativePexAliveAfterTheCallerDisposesIt()
    {
        var pex = PexFile.FromBytes(Compile());
        using var runtime = new PapyrusRuntime();
        runtime.AddPex(pex);
        pex.Dispose();
        GC.Collect();
        GC.WaitForPendingFinalizers();
        Assert.Equal(1, runtime.PexCount);
        Assert.True(runtime.HasScript("Give"));
        var instance = runtime.Attach("Give", Owner);
        Assert.Equal(PapyrusStatus.Ok, runtime.Initialize(instance));
        ExpectValue(runtime, instance);
    }

    [Fact]
    public void AddPexFileRefusesNullAndADisposedRuntime()
    {
        var runtime = new PapyrusRuntime();
        Assert.Throws<ArgumentNullException>(() => runtime.AddPex((PexFile)null!));
        runtime.Dispose();
        using var pex = PexFile.FromBytes(Compile());
        Assert.Throws<ObjectDisposedException>(() => runtime.AddPex(pex));
    }
}
