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

public sealed record Mo2Instance(string SelectedProfile, string GamePath, string DataDir, string BaseDir, string ModsDir, string OverwriteDir, string ProfilesDir);

public sealed record Mo2Mod(string Name, bool Enabled, bool IsSeparator);

public sealed record Mo2Profile(IReadOnlyList<Mo2Mod> ModsHighToLow, IReadOnlyList<PluginListEntry> Plugins);

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

    public static Mo2Instance ReadMo2Instance(string instanceDir)
    {
        IntPtr* fields;
        nuint count;
        Call.Check(RadicleNative.Mo2InstanceRead(instanceDir, &fields, &count));
        var values = Call.TakeStrings(fields, count);
        return new Mo2Instance(values[0], values[1], values[2], values[3], values[4], values[5], values[6]);
    }

    public static Mo2Profile ReadMo2Profile(string instanceDir, string? profile)
    {
        IntPtr* modRows;
        nuint modCount;
        IntPtr* pluginRows;
        nuint pluginCount;
        Call.Check(RadicleNative.Mo2ProfileRead(instanceDir, profile, &modRows, &modCount, &pluginRows, &pluginCount));
        var mods = Call.TakeStrings(modRows, modCount).Select(row =>
        {
            var fields = SplitTrailing(row, 2);
            return new Mo2Mod(fields[0], fields[1] == "1", fields[2] == "1");
        }).ToArray();
        var plugins = Call.TakeStrings(pluginRows, pluginCount).Select(row =>
        {
            var fields = SplitTrailing(row, 2);
            return new PluginListEntry(fields[0], fields[1] == "1", int.Parse(fields[2], CultureInfo.InvariantCulture));
        }).ToArray();
        return new Mo2Profile(mods, plugins);
    }

    public static IReadOnlyList<string> ReadMo2AssetRoots(string instanceDir, string? profile)
    {
        IntPtr* roots;
        nuint count;
        Call.Check(RadicleNative.Mo2AssetRoots(instanceDir, profile, &roots, &count));
        return Call.TakeStrings(roots, count);
    }

    public static IReadOnlyDictionary<string, string> ReadMo2PluginPaths(string instanceDir, string? profile)
    {
        IntPtr* rows;
        nuint count;
        Call.Check(RadicleNative.Mo2PluginPaths(instanceDir, profile, &rows, &count));
        return Call.TakeStrings(rows, count).Select(row => row.Split('\t', 2)).ToDictionary(fields => fields[0], fields => fields[1]);
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
