using Radicle.Interop.Native;
using System;
using System.Text;
using Xunit;

namespace Radicle.Interop.Tests;

internal static class PapyrusProbe
{
    public const string Source =
        "Scriptname Probe\nInt total\nInt Property Count Auto\n" +
        "Int Function Hit() global native\nInt Function Wait() global native\n" +
        "Function Ping()\n  total = Hit()\nEndFunction\n" +
        "Function Run()\n  total = Wait()\nEndFunction\n";

    public static readonly PapyrusOwner Actor = new(PapyrusOwnerKind.Form, "000800:Probe.esp");
    public static readonly PapyrusOwner Other = new(PapyrusOwnerKind.Form, "000900:Probe.esp");
    public static readonly PapyrusOwner Quest = new(PapyrusOwnerKind.Form, "000801:Quests.esp");

    public static byte[] Compile()
    {
        using var compiled = PapyrusCompiler.CompileSource(Encoding.UTF8.GetBytes(Source), "Probe.psc").Pex;
        Assert.NotNull(compiled);
        return compiled!.ToBytes();
    }

    public static byte[] Patch(byte[] bytes, string from, string to)
    {
        var source = Encoding.Latin1.GetBytes(from);
        var target = Encoding.Latin1.GetBytes(to);
        Assert.Equal(source.Length, target.Length);
        var copy = (byte[])bytes.Clone();
        var found = 0;
        for (var i = 0; i + source.Length <= copy.Length; ++i)
        {
            if (!copy.AsSpan(i, source.Length).SequenceEqual(source.AsSpan())) continue;
            target.CopyTo(copy, i);
            found++;
        }
        Assert.NotEqual(0, found);
        return copy;
    }

    public static PapyrusRuntime Create(PapyrusHostCallbacks? host = null)
    {
        var runtime = new PapyrusRuntime(host);
        try { runtime.AddPex(Compile()); }
        catch
        {
            runtime.Dispose();
            throw;
        }
        return runtime;
    }

    public static ulong Ready(PapyrusRuntime runtime, PapyrusOwner owner)
    {
        var instance = runtime.Attach("Probe", owner);
        Assert.Equal(PapyrusStatus.Ok, runtime.Initialize(instance));
        return instance;
    }
}
