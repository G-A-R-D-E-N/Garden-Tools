using System;
using System.IO;
using System.Text;
using System.Text.Json;
using Radicle.Interop.Native;
using Xunit;

namespace Radicle.Interop.Tests;

public class PapyrusTests
{
    private const string Probe = "Scriptname Probe\nInt Property Count Auto\nFunction Go(int amount)\n" +
        "    int total = Count + amount\n    If total > 3\n        Count = total\n    EndIf\nEndFunction\n";

    private static byte[] Utf8(string text) => Encoding.UTF8.GetBytes(text);

    private static PexFile Compile(string source)
    {
        var pex = PapyrusCompiler.CompileSource(Utf8(source), "Probe.psc").Pex;
        Assert.NotNull(pex);
        return pex!;
    }

    [Fact]
    public void ALexerErrorIsReturnedAsADiagnosticRowWithoutAHandle()
    {
        var result = PapyrusCompiler.CompileSource(Utf8("Scriptname Probe\nInt a = 1 @ 2\n"), "Probe.psc");

        Assert.Null(result.Pex);
        Assert.Contains(new PapyrusDiagnostic(PapyrusDiagnosticSeverity.Error, "PAP0004", 2, 11, "Unexpected character '@'."),
            result.Diagnostics);
    }

    [Fact]
    public void ACleanCompileHandleReadsBackAsTheScriptAndDecompilesToSourceThatCompilesAgain()
    {
        using var pex = Compile(Probe);

        Assert.Equal("Probe", Encoding.ASCII.GetString(Assert.Single(pex.Objects).Name));
        Assert.StartsWith("; Disassembly of Probe.psc\n", PapyrusCompiler.Decompile(pex, assembly: true));
        using var recompiled = PapyrusCompiler.CompileSource(Utf8(PapyrusCompiler.Decompile(pex)), "Probe.psc").Pex;
        Assert.NotNull(recompiled);
    }

    [Fact]
    public void AnEmptySourceIsRefusedByItsOwnMessageNotByANullPointer()
    {
        var error = Assert.Throws<RadicleException>(() => PapyrusCompiler.CompileSource(ReadOnlySpan<byte>.Empty, "Probe.psc"));

        Assert.Equal(Status.InvalidArgument, error.Status);
        Assert.Contains("source must not be empty", error.Message);
    }
}
