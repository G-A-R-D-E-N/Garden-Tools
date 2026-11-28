using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using Radicle.Interop.Native;
using Xunit;

namespace Radicle.Interop.Tests;

public class PapyrusSaveTests
{
    private const string Source = "Scriptname Give\nInt Property Count Auto\nInt Function Value()\n  Return 41\nEndFunction\n";
    private static readonly PapyrusOwner Owner = new(PapyrusOwnerKind.Form, "000800:Give.esp");

    private static PexFile Compile()
    {
        var pex = PapyrusCompiler.CompileSource(Encoding.UTF8.GetBytes(Source), "Give.psc").Pex;
        Assert.NotNull(pex);
        return pex!;
    }

    [Fact]
    public void FingerprintIsAStableSha256TextAndNullForAnUnknownScript()
    {
        using var pex = Compile();
        var fingerprint = PapyrusSave.ScriptFingerprint(pex, "Give");
        Assert.NotNull(fingerprint);
        Assert.Equal(64, fingerprint!.Length);
        Assert.Equal(fingerprint, PapyrusSave.ScriptFingerprint(pex, "Give"));
        Assert.Null(PapyrusSave.ScriptFingerprint(pex, "Missing"));
    }

    [Fact]
    public void CaptureIsStableForTheSameStateAndRestoreReportsCounts()
    {
        using var pex = Compile();
        using var runtime = new PapyrusRuntime();
        runtime.AddPex(pex);
        var instance = runtime.Attach("Give", Owner);
        Assert.Equal(PapyrusStatus.Ok, runtime.Initialize(instance));

        var first = runtime.CaptureSave();
        Assert.NotEmpty(first.Json);
        Assert.Equal(64, first.Digest.Length);
        Assert.Equal(first, runtime.CaptureSave());

        var report = PapyrusSave.Restore(first.Json, new Dictionary<string, string> { ["Give"] = PapyrusSave.ScriptFingerprint(pex, "Give")! });
        Assert.NotNull(report.Report);
        Assert.Equal((0, 0), (report.Resumed, report.Rejected));
        Assert.Contains("threads=0 resumed=0 rejected=0", report.Report);
    }

    [Fact]
    public void RestoreWithNoFingerprintsPassesNullArraysAndReportsZeroThreads()
    {
        using var pex = Compile();
        using var runtime = new PapyrusRuntime();
        runtime.AddPex(pex);
        Assert.Equal(PapyrusStatus.Ok, runtime.Initialize(runtime.Attach("Give", Owner)));

        var report = PapyrusSave.Restore(runtime.CaptureSave().Json, new Dictionary<string, string>());
        Assert.Equal((0, 0), (report.Resumed, report.Rejected));
        Assert.Contains("threads=0 resumed=0 rejected=0", report.Report);
    }

    [Fact]
    public void RestoreOfMalformedJsonIsAParseError()
    {
        var thrown = Assert.Throws<RadicleException>(() => PapyrusSave.Restore("{ not json", new Dictionary<string, string>()));
        Assert.Equal(Status.ParseError, thrown.Status);
    }

    [Fact]
    public void CaptureOnADisposedRuntimeThrowsObjectDisposed()
    {
        var runtime = new PapyrusRuntime();
        runtime.Dispose();
        Assert.Throws<ObjectDisposedException>(() => runtime.CaptureSave());
    }

    private const string StateSource = "Scriptname Box\nInt Property Count Auto\nString Property Label Auto\nInt[] Property Items Auto\nBox Property Other Auto\n";
    private static readonly PapyrusOwner First = new(PapyrusOwnerKind.Form, "000801:Box.esp");
    private static readonly PapyrusOwner Second = new(PapyrusOwnerKind.Form, "000802:Box.esp");

    private static PexFile CompileState()
    {
        var pex = PapyrusCompiler.CompileSource(Encoding.UTF8.GetBytes(StateSource), "Box.psc").Pex;
        Assert.NotNull(pex);
        return pex!;
    }

    [Fact]
    public void InstanceStateRoundTripsPropertiesArraysAndObjectReferencesIntoARuntimeWithDifferentIds()
    {
        using var pex = CompileState();
        using var source = new PapyrusRuntime();
        source.AddPex(pex);
        var firstSource = source.Attach("Box", First);
        var secondSource = source.Attach("Box", Second);
        source.SetVariable(firstSource, "::Count_var", 42);
        source.SetVariable(firstSource, "::Label_var", "café \"q\"");
        source.SetVariable(firstSource, "::Items_var", new object?[] { 3, 4, 5 });
        source.SetVariable(firstSource, "::Other_var", new PapyrusObjectRef(secondSource, "Box", ""));
        var capture = source.CaptureState();
        Assert.Equal(64, capture.Digest.Length);
        Assert.Empty(capture.EngineHandles);
        Assert.All(capture.Blob, ch => Assert.True(ch == '\n' || (ch >= ' ' && ch < 0x7F)));

        using var target = new PapyrusRuntime();
        target.AddPex(pex);
        var secondTarget = target.Attach("Box", Second);
        var firstTarget = target.Attach("Box", First);
        Assert.NotEqual(firstSource, firstTarget);
        var report = target.RestoreState(capture.Blob);
        Assert.Equal((2, 0, 0), (report.Instances, report.SkippedInstances, report.UnboundReferences));
        Assert.True(target.TryGetProperty(firstTarget, "Count", out var count));
        Assert.Equal(42, count);
        Assert.True(target.TryGetProperty(firstTarget, "Label", out var label));
        Assert.Equal("café \"q\"", label);
        Assert.True(target.TryGetProperty(firstTarget, "Items", out var items));
        Assert.Equal(new object?[] { 3, 4, 5 }, ((IEnumerable<object?>)items!).ToArray());
        Assert.True(target.TryGetProperty(firstTarget, "Other", out var other));
        Assert.Equal(secondTarget, ((PapyrusObjectRef)other!).Handle);
        var settled = target.CaptureState();
        Assert.Contains("\"etn\":\"Int\"", settled.Blob);
        target.RestoreState(settled.Blob);
        Assert.Equal(settled.Digest, target.CaptureState().Digest);
    }

    [Fact]
    public void InstanceStateRefusalsMapToStatusesAndLeaveTheRuntimeUntouched()
    {
        using var pex = CompileState();
        using var source = new PapyrusRuntime();
        source.AddPex(pex);
        var saved = source.Attach("Box", First);
        source.SetVariable(saved, "::Count_var", 7);
        source.Attach("Box", Second);
        var blob = source.CaptureState().Blob;

        using var target = new PapyrusRuntime();
        target.AddPex(pex);
        var live = target.Attach("Box", First);
        target.SetVariable(live, "::Count_var", 1);

        Assert.Equal(Status.ParseError, Assert.Throws<RadicleException>(() => target.RestoreState(blob[..^1])).Status);
        Assert.Equal(Status.ParseError, Assert.Throws<RadicleException>(() => target.RestoreState(blob.Replace("\"version\":1", "\"version\":9"))).Status);
        Assert.Equal(Status.NotFound, Assert.Throws<RadicleException>(() => target.RestoreState(blob)).Status);
        Assert.True(target.TryGetProperty(live, "Count", out var untouched));
        Assert.Equal(1, untouched);

        var report = target.RestoreState(blob, skipUnknown: true);
        Assert.Equal((1, 1), (report.Instances, report.SkippedInstances));
        Assert.True(target.TryGetProperty(live, "Count", out var restored));
        Assert.Equal(7, restored);
    }

    [Fact]
    public void RestoreInstancesAppliesOnlyTheChosenIdentities()
    {
        using var pex = CompileState();
        using var source = new PapyrusRuntime();
        source.AddPex(pex);
        source.SetVariable(source.Attach("Box", First), "::Count_var", 7);
        source.SetVariable(source.Attach("Box", Second), "::Count_var", 8);
        var blob = source.CaptureState().Blob;

        using var target = new PapyrusRuntime();
        target.AddPex(pex);
        var restoredOnce = target.Attach("Box", First);
        target.SetVariable(restoredOnce, "::Count_var", 99);
        var late = target.Attach("Box", Second);

        var report = target.RestoreInstances(blob, new[] { new PapyrusInstanceIdentity(Second, "Box") });
        Assert.Equal((1, 0), (report.Instances, report.SkippedInstances));
        Assert.True(target.TryGetProperty(late, "Count", out var lateCount));
        Assert.Equal(8, lateCount);
        Assert.True(target.TryGetProperty(restoredOnce, "Count", out var kept));
        Assert.Equal(99, kept);

        Assert.Equal(0, target.RestoreInstances(blob, Array.Empty<PapyrusInstanceIdentity>()).Instances);
        Assert.Equal(0, target.RestoreInstances(blob, new[] { new PapyrusInstanceIdentity(new PapyrusOwner(PapyrusOwnerKind.Form, "000999:Box.esp"), "Box") }).Instances);
        Assert.Equal(Status.InvalidArgument, Assert.Throws<RadicleException>(() => target.RestoreInstances(blob, new[] { new PapyrusInstanceIdentity(new PapyrusOwner(PapyrusOwnerKind.PlacedReference, "000999:Box.esp"), "Box") })).Status);
        Assert.Throws<ArgumentNullException>(() => target.RestoreInstances(blob, new PapyrusInstanceIdentity[] { null! }));
        Assert.Throws<ArgumentNullException>(() => target.RestoreInstances(blob, new[] { new PapyrusInstanceIdentity(null!, "Box") }));
    }

    [Fact]
    public void InstanceStateOnADisposedRuntimeThrowsObjectDisposed()
    {
        var runtime = new PapyrusRuntime();
        runtime.Dispose();
        Assert.Throws<ObjectDisposedException>(() => runtime.CaptureState());
        Assert.Throws<ObjectDisposedException>(() => runtime.RestoreState("{}"));
    }

    private const string EngineSource = "Scriptname Holder\nVar Property First Auto\nVar Property Second Auto\n";
    private static readonly PapyrusOwner HolderOwner = new(PapyrusOwnerKind.Form, "000803:Holder.esp");

    [Fact]
    public void EngineHandlesAreReadFromTheCaptureAndRemappedByRestore()
    {
        const ulong bit = 0x8000000000000000UL;
        var pex = PapyrusCompiler.CompileSource(Encoding.UTF8.GetBytes(EngineSource), "Holder.psc").Pex;
        Assert.NotNull(pex);
        using var owned = pex!;
        using var source = new PapyrusRuntime();
        source.AddPex(owned);
        var holder = source.Attach("Holder", HolderOwner);
        source.SetVariable(holder, "::First_var", new PapyrusObjectRef(bit | 0x2A, "", ""));
        source.SetVariable(holder, "::Second_var", new PapyrusObjectRef(bit | 5, "", ""));
        var capture = source.CaptureState();
        Assert.Equal(new[] { bit | 5, bit | 0x2A }, capture.EngineHandles.ToArray());

        using var target = new PapyrusRuntime();
        target.AddPex(owned);
        var live = target.Attach("Holder", HolderOwner);
        Assert.Equal(Status.NotFound, Assert.Throws<RadicleException>(() => target.RestoreState(capture.Blob)).Status);
        Assert.Equal(Status.NotFound, Assert.Throws<RadicleException>(() => target.RestoreState(capture.Blob, engineHandles: new Dictionary<ulong, ulong> { [bit | 5] = bit | 105 })).Status);

        var report = target.RestoreState(capture.Blob, engineHandles: new Dictionary<ulong, ulong> { [bit | 0x2A] = bit | 7, [bit | 5] = bit | 9 });
        Assert.Equal((1, 0), (report.Instances, report.UnboundReferences));
        Assert.True(target.TryGetProperty(live, "First", out var first));
        Assert.Equal(bit | 7, ((PapyrusObjectRef)first!).Handle);
        Assert.True(target.TryGetProperty(live, "Second", out var second));
        Assert.Equal(bit | 9, ((PapyrusObjectRef)second!).Handle);

        var skipped = target.RestoreState(capture.Blob, skipUnknown: true);
        Assert.Equal(2, skipped.UnboundReferences);
    }
}
