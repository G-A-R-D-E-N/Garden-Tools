using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using Radicle.Interop.Native;

namespace Radicle.Interop;

public enum PapyrusDiagnosticSeverity
{
    Warning = 0,
    Error = 1,
}

public sealed record PapyrusDiagnostic(PapyrusDiagnosticSeverity Severity, string Code, int Line, int Column, string Message);

public sealed record PapyrusCompileResult(PexFile? Pex, IReadOnlyList<PapyrusDiagnostic> Diagnostics);

public static unsafe class PapyrusCompiler
{
    public static PapyrusCompileResult CompileSource(ReadOnlySpan<byte> source, string fileName, bool release = false,
        bool noDebugInfo = false, string? imports = null)
    {
        using var strings = new NativeStrings();
        var options = new Native.PapyrusCompileOptions
        {
            Release = release ? 1 : 0,
            NoDebugInfo = noDebugInfo ? 1 : 0,
            Imports = strings.Add(imports),
        };
        byte* empty = stackalloc byte[1];
        IntPtr handle = IntPtr.Zero;
        IntPtr* rows;
        nuint count;
        fixed (byte* pointer = source)
        {
            Call.Check(RadicleNative.PapyrusCompileSource(pointer == null ? empty : pointer, (nuint)source.Length, fileName,
                options, &handle, &rows, &count));
        }
        var diagnostics = Call.TakeStrings(rows, count).Select(ParseDiagnostic).ToArray();
        return new PapyrusCompileResult(handle == IntPtr.Zero ? null : PexFile.Snapshot(handle), diagnostics);
    }

    public static string Decompile(PexFile pex, bool assembly = false)
    {
        using var use = pex.Use();
        byte* text = null;
        nuint size = 0;
        Call.Check(RadicleNative.PapyrusDecompile(use.Pointer, assembly ? 1 : 0, &text, &size));
        return Call.TakeText(text, size);
    }

    private static PapyrusDiagnostic ParseDiagnostic(string row)
    {
        var fields = row.Split('\t', 5);
        return new PapyrusDiagnostic(fields[0] == "error" ? PapyrusDiagnosticSeverity.Error : PapyrusDiagnosticSeverity.Warning,
            fields[1], int.Parse(fields[2], CultureInfo.InvariantCulture), int.Parse(fields[3], CultureInfo.InvariantCulture), fields[4]);
    }
}
