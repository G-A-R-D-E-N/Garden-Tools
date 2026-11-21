using System;
using System.Runtime.InteropServices;
using System.Text;
using Radicle.Interop.Native;
using Xunit;

namespace Radicle.Interop.Tests;

public unsafe class PapyrusAttachRemoteTests : IDisposable
{
    private const string Source = "Scriptname Actor\nInt Property Count Auto\n";
    private const string Quest = "000801:Quests.esp";
    private const string Event = "Quest.OnStageSet";

    private readonly IntPtr _runtime;
    private readonly IntPtr _pex;

    public PapyrusAttachRemoteTests()
    {
        using var compiled = PapyrusCompiler.CompileSource(Encoding.UTF8.GetBytes(Source), "Actor.psc").Pex;
        Assert.NotNull(compiled);
        var bytes = compiled!.ToBytes();
        IntPtr pex;
        fixed (byte* data = bytes) Assert.Equal(Status.Ok, RadicleNative.PexParse(data, (nuint)bytes.Length, &pex));
        _pex = pex;
        IntPtr runtime;
        Assert.Equal(Status.Ok, RadicleNative.PapyrusRuntimeCreate(null, &runtime));
        _runtime = runtime;
        Assert.Equal(Status.Ok, RadicleNative.PapyrusRuntimeAddPex(_runtime, _pex));
    }

    public void Dispose()
    {
        RadicleNative.PapyrusRuntimeDestroy(_runtime);
        RadicleNative.PexDestroy(_pex);
    }

    private static IntPtr Utf8(string text) => Marshal.StringToCoTaskMemUTF8(text);

    private Status AttachWith(string[] names, IntPtr[] values, out ulong instance)
    {
        var raw = new IntPtr[names.Length];
        for (var i = 0; i < names.Length; i++) raw[i] = names[i] is null ? IntPtr.Zero : Utf8(names[i]);
        try
        {
            ulong id;
            Status status;
            fixed (IntPtr* n = raw)
            fixed (IntPtr* v = values)
                status = RadicleNative.PapyrusRuntimeAttachWithProperties(_runtime, "Actor", PapyrusOwnerKind.Form,
                    "000800:Actor.esp", "", n, v, (nuint)names.Length, &id);
            instance = id;
            return status;
        }
        finally
        {
            foreach (var pointer in raw) if (pointer != IntPtr.Zero) Marshal.FreeCoTaskMem(pointer);
        }
    }

    private static IntPtr Int(int number)
    {
        IntPtr value;
        Assert.Equal(Status.Ok, RadicleNative.PapyrusValueInt(number, &value));
        return value;
    }

    private ulong Plain(string baseKey)
    {
        ulong instance;
        Assert.Equal(Status.Ok, RadicleNative.PapyrusRuntimeAttach(_runtime, "Actor", PapyrusOwnerKind.Form, baseKey, "", &instance));
        return instance;
    }

    private long Register(ulong instance, PapyrusOwnerKind kind, string source, string discriminator, string name)
    {
        long handle;
        Assert.Equal(Status.Ok, RadicleNative.PapyrusRuntimeRegisterRemoteEvent(_runtime, instance, kind, source, discriminator, name, &handle));
        return handle;
    }

    private nuint Match(string name, long[]? handles = null, ulong[]? instances = null)
    {
        nuint count;
        var capacity = (nuint)(handles?.Length ?? 0);
        fixed (long* h = handles)
        fixed (ulong* i = instances)
            Assert.Equal(Status.Ok, RadicleNative.PapyrusRuntimeMatchRemoteEvent(_runtime, PapyrusOwnerKind.Form, Quest, "", name, h, i, capacity, &count));
        return count;
    }

    [Fact]
    public void AttachWithPropertiesStoresTheValuesAndReadsThemBackWithoutCase()
    {
        var count = Int(5);
        ulong instance;
        Assert.Equal(Status.Ok, AttachWith(new[] { "Count" }, new[] { count }, out instance));
        RadicleNative.PapyrusValueDestroy(count);
        IntPtr read;
        Assert.Equal(Status.Ok, RadicleNative.PapyrusRuntimeGetProperty(_runtime, instance, "COUNT", &read));
        Assert.Equal(5, RadicleNative.PapyrusValueIntOf(read));
        RadicleNative.PapyrusValueDestroy(read);
        Assert.Equal(Status.NotFound, RadicleNative.PapyrusRuntimeGetProperty(_runtime, instance, "Missing", &read));
    }

    [Fact]
    public void ABadPropertyRefusesTheWholeAttachAndConsumesNoInstance()
    {
        var one = Int(1);
        Assert.Equal(Status.InvalidArgument, AttachWith(new[] { "Count", "COUNT" }, new[] { one, one }, out var instance));
        Assert.Equal(0ul, instance);
        Assert.Equal(Status.InvalidArgument, AttachWith(new[] { "Count", " " }, new[] { one, one }, out _));
        Assert.Equal(Status.InvalidArgument, AttachWith(new[] { "Count", null! }, new[] { one, one }, out _));
        RadicleNative.PapyrusValueDestroy(one);
        Assert.Equal(1ul, Plain("000800:Actor.esp"));
    }

    [Fact]
    public void RemoteEventsRegisterMatchAndListTheirTargetsInHandleOrder()
    {
        var first = Plain("000800:Actor.esp");
        var second = Plain("000900:Actor.esp");
        var firstHandle = Register(first, PapyrusOwnerKind.Form, Quest, "", Event);
        var secondHandle = Register(second, PapyrusOwnerKind.Form, Quest, "", Event.ToLowerInvariant());
        Register(first, PapyrusOwnerKind.DynamicReference, Quest, "token", Event);

        var handles = new long[2];
        var instances = new ulong[2];
        Assert.Equal((nuint)2, Match(Event.ToUpperInvariant(), handles, instances));
        Assert.Equal(new[] { firstHandle, secondHandle }, handles);
        Assert.Equal(new[] { first, second }, instances);

        int removed;
        Assert.Equal(Status.Ok, RadicleNative.PapyrusRuntimeUnregister(_runtime, firstHandle, &removed));
        Assert.Equal(1, removed);
        Assert.Equal((nuint)1, Match(Event));
        Assert.Equal(1, RadicleNative.PapyrusRuntimeDetach(_runtime, second));
        Assert.Equal((nuint)0, Match(Event));
    }

    [Fact]
    public void ABlankRemoteSourceOrNameIsRefusedAndRegistersNothing()
    {
        var instance = Plain("000800:Actor.esp");
        long handle;
        Assert.Equal(Status.InvalidArgument, RadicleNative.PapyrusRuntimeRegisterRemoteEvent(_runtime, instance, PapyrusOwnerKind.Form, "", "", Event, &handle));
        Assert.Equal(Status.InvalidArgument, RadicleNative.PapyrusRuntimeRegisterRemoteEvent(_runtime, instance, PapyrusOwnerKind.Form, Quest, "", " ", &handle));
        Assert.Equal((nuint)0, Match(Event));
    }
}
