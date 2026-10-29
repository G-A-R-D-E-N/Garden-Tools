using System;
using System.Collections.Generic;
using Radicle.Interop.Native;

namespace Radicle.Interop;

public static unsafe class PapyrusAnalysis
{
    public static int LineColumnToOffset(ReadOnlySpan<byte> text, int line, int column)
    {
        byte* empty = stackalloc byte[1];
        int offset = 0;
        fixed (byte* pointer = text)
        {
            Call.Check(RadicleNative.PapyrusLineColumnToOffset(pointer == null ? empty : pointer, (nuint)text.Length, line, column, &offset));
        }
        return offset;
    }

    public static string Check(string sourcePath, bool semantic, string? imports = null, IEnumerable<string>? baseImports = null)
    {
        using var strings = new NativeStrings();
        var roots = strings.AddAll(baseImports);
        byte* text = null;
        nuint size = 0;
        fixed (IntPtr* bases = roots)
        {
            Call.Check(RadicleNative.PapyrusCheck(sourcePath, semantic ? 1 : 0, imports, bases, (nuint)roots.Length, &text, &size));
        }
        return Call.TakeText(text, size);
    }

    public static string Outline(string sourcePath)
    {
        byte* text = null;
        nuint size = 0;
        Call.Check(RadicleNative.PapyrusOutline(sourcePath, &text, &size));
        return Call.TakeText(text, size);
    }

    public static string Definition(string sourcePath, int line, int column, string? imports = null, IEnumerable<string>? baseImports = null)
    {
        using var strings = new NativeStrings();
        var roots = strings.AddAll(baseImports);
        byte* text = null;
        nuint size = 0;
        fixed (IntPtr* bases = roots)
        {
            Call.Check(RadicleNative.PapyrusDefinition(sourcePath, line, column, imports, bases, (nuint)roots.Length, &text, &size));
        }
        return Call.TakeText(text, size);
    }

    public static string AnalyzeJson(ReadOnlySpan<byte> text, string? filePath = null)
    {
        byte* empty = stackalloc byte[1];
        byte* json = null;
        nuint size = 0;
        fixed (byte* pointer = text)
        {
            Call.Check(RadicleNative.PapyrusAnalyzeJson(pointer == null ? empty : pointer, (nuint)text.Length, filePath, &json, &size));
        }
        return Call.TakeText(json, size);
    }

    public static string SymbolAtJson(ReadOnlySpan<byte> text, string? filePath, int offset, string? imports = null,
        IEnumerable<string>? baseImports = null)
    {
        using var strings = new NativeStrings();
        var roots = strings.AddAll(baseImports);
        byte* empty = stackalloc byte[1];
        byte* json = null;
        nuint size = 0;
        fixed (byte* pointer = text)
        fixed (IntPtr* bases = roots)
        {
            Call.Check(RadicleNative.PapyrusSymbolAtJson(pointer == null ? empty : pointer, (nuint)text.Length, filePath, offset,
                imports, bases, (nuint)roots.Length, &json, &size));
        }
        return Call.TakeText(json, size);
    }

    public static string CompilePath(string sourcePath, string? outputPath = null, string? imports = null, bool release = false,
        bool noDebugInfo = false, string? flagFile = null, IEnumerable<string>? baseImports = null)
    {
        using var strings = new NativeStrings();
        var roots = strings.AddAll(baseImports);
        byte* text = null;
        nuint size = 0;
        fixed (IntPtr* bases = roots)
        {
            Call.Check(RadicleNative.PapyrusCompilePath(sourcePath, outputPath, imports, release ? 1 : 0, noDebugInfo ? 1 : 0,
                flagFile, bases, (nuint)roots.Length, &text, &size));
        }
        return Call.TakeText(text, size);
    }

    public static IReadOnlyList<string> NaturalRoots(string folder)
    {
        IntPtr* roots;
        nuint count;
        Call.Check(RadicleNative.PapyrusNaturalRoots(folder, &roots, &count));
        return Call.TakeStrings(roots, count);
    }
}
