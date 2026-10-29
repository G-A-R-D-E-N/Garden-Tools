using System;
using System.Collections.Generic;
using Radicle.Interop.Native;

namespace Radicle.Interop;

public static unsafe class GraphTools
{
    public static string Validate(string graphPath, string? imports = null, IEnumerable<string>? baseImports = null)
    {
        using var strings = new NativeStrings();
        var roots = strings.AddAll(baseImports);
        byte* text = null;
        nuint size = 0;
        fixed (IntPtr* bases = roots)
        {
            Call.Check(RadicleNative.GraphValidate(graphPath, imports, bases, (nuint)roots.Length, &text, &size));
        }
        return Call.TakeText(text, size);
    }

    public static string Compile(string graphPath, string? outputPath = null, string? imports = null, bool sourceOnly = false,
        IEnumerable<string>? baseImports = null)
    {
        using var strings = new NativeStrings();
        var roots = strings.AddAll(baseImports);
        byte* text = null;
        nuint size = 0;
        fixed (IntPtr* bases = roots)
        {
            Call.Check(RadicleNative.GraphCompile(graphPath, outputPath, imports, sourceOnly ? 1 : 0, bases, (nuint)roots.Length,
                &text, &size));
        }
        return Call.TakeText(text, size);
    }

    public static string PaletteSearch(string query, int limit = 0, string? imports = null, IEnumerable<string>? baseImports = null)
    {
        using var strings = new NativeStrings();
        var roots = strings.AddAll(baseImports);
        byte* text = null;
        nuint size = 0;
        fixed (IntPtr* bases = roots)
        {
            Call.Check(RadicleNative.GraphPaletteSearch(query, imports, limit, bases, (nuint)roots.Length, &text, &size));
        }
        return Call.TakeText(text, size);
    }

    public static string DescribeNode(string nodeType, string? imports = null, IEnumerable<string>? baseImports = null)
    {
        using var strings = new NativeStrings();
        var roots = strings.AddAll(baseImports);
        byte* text = null;
        nuint size = 0;
        fixed (IntPtr* bases = roots)
        {
            Call.Check(RadicleNative.GraphDescribeNode(nodeType, imports, bases, (nuint)roots.Length, &text, &size));
        }
        return Call.TakeText(text, size);
    }
}
