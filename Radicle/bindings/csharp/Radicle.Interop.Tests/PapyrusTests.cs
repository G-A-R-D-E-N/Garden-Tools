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

    [Fact]
    public void LineColumnToOffsetCountsCrLfLineEndingsAndIsMinusOneOutsideTheText()
    {
        var text = Utf8("abc\r\ndef");

        Assert.Equal(5, PapyrusAnalysis.LineColumnToOffset(text, 2, 1));
        Assert.Equal(-1, PapyrusAnalysis.LineColumnToOffset(text, 1, 9));
        Assert.Equal(-1, PapyrusAnalysis.LineColumnToOffset(text, 5, 1));
    }

    [Fact]
    public void CheckReportsAnUnresolvedNameAsPap0030OnlyWhenSemanticsAreOn()
    {
        using var temp = new TempDirectory();
        var file = temp.Write("A.psc", Utf8("ScriptName A\nFunction F()\n    int n = nope\nEndFunction\n"));

        var semantic = PapyrusAnalysis.Check(file, semantic: true);
        Assert.Contains("PAP0030", semantic);
        Assert.Contains("1 with name or type errors", semantic);

        var syntaxOnly = PapyrusAnalysis.Check(file, semantic: false);
        Assert.Contains("does not resolve names or check types", syntaxOnly);
        Assert.DoesNotContain("PAP0030", syntaxOnly);
    }

    [Fact]
    public void AnalyzeJsonReadsTheBufferNotTheFileOnDiskAndAcceptsAnEmptyBuffer()
    {
        using var temp = new TempDirectory();
        var file = temp.Write("A.psc", Utf8("ScriptName A\n"));

        using var document = JsonDocument.Parse(PapyrusAnalysis.AnalyzeJson(Utf8("ScriptName A\nFunction Buffered()\nEndFunction\n"), file));
        Assert.Contains(document.RootElement.GetProperty("symbols").EnumerateArray(),
            symbol => symbol.GetProperty("name").GetString() == "Buffered");

        using var empty = JsonDocument.Parse(PapyrusAnalysis.AnalyzeJson(ReadOnlySpan<byte>.Empty));
        Assert.Equal(JsonValueKind.Object, empty.RootElement.ValueKind);
    }

    [Fact]
    public void AMissingGraphIsResultTextNotAnErrorStatus()
    {
        using var temp = new TempDirectory();

        Assert.StartsWith("RESULT: FAILED", GraphTools.Validate(Path.Combine(temp.Path, "missing.fograph")));
    }
}
