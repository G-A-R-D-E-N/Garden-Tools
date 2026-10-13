using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using Radicle.Interop.Native;

namespace Radicle.Interop;

public sealed record PluginListEntry(string Name, bool Active, int Line);

public sealed record LoadOrderEntry(string Name, bool Checked, bool MasterFlag, bool Light);

public sealed record LoadOrderFinding(string Code, string Plugin, string Detail);

public sealed record AssembledLoadOrder(IReadOnlyList<LoadOrderEntry> Entries, IReadOnlyList<LoadOrderFinding> Findings, bool Failed);

public static unsafe class LoadOrderFiles
{
    static LoadOrderFiles() => RadicleLibrary.Register();

    public static IReadOnlyList<PluginListEntry> ReadPluginList(string path)
    {
        IntPtr* rows;
        nuint count;
        Call.Check(RadicleNative.PluginListRead(path, &rows, &count));
        return Call.TakeStrings(rows, count).Select(row =>
        {
            var fields = SplitTrailing(row, 2);
            return new PluginListEntry(fields[0], fields[1] == "1", int.Parse(fields[2], CultureInfo.InvariantCulture));
        }).ToArray();
    }

    public static IReadOnlyList<string> ReadCcc(string path)
    {
        IntPtr* names;
        nuint count;
        Call.Check(RadicleNative.CccRead(path, &names, &count));
        return Call.TakeStrings(names, count);
    }

    public static AssembledLoadOrder Assemble(string dataDir, string? pluginListPath, string? cccPath, OfficialTable table)
    {
        IntPtr* rows;
        nuint rowCount;
        IntPtr* findings;
        nuint findingCount;
        int failed;
        Call.Check(RadicleNative.LoadOrderAssemble(dataDir, pluginListPath, cccPath, table, &rows, &rowCount, &findings, &findingCount, &failed));
        var entries = Call.TakeStrings(rows, rowCount).Select(row =>
        {
            var fields = SplitTrailing(row, 3);
            return new LoadOrderEntry(fields[0], fields[1] == "1", fields[2] == "1", fields[3] == "1");
        }).ToArray();
        var issues = Call.TakeStrings(findings, findingCount).Select(text =>
        {
            var fields = text.Split('\t', 3);
            return new LoadOrderFinding(fields[0], fields[1], fields[2]);
        }).ToArray();
        return new AssembledLoadOrder(entries, issues, failed != 0);
    }

    private static string[] SplitTrailing(string row, int trailing)
    {
        var fields = new string[trailing + 1];
        var end = row.Length;
        for (var index = trailing; index > 0; index--)
        {
            var tab = row.LastIndexOf('\t', end - 1);
            fields[index] = row[(tab + 1)..end];
            end = tab;
        }
        fields[0] = row[..end];
        return fields;
    }
}
