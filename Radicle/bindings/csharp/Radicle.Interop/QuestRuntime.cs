using System;
using System.Collections.Generic;
using System.Globalization;
using Radicle.Interop.Native;

namespace Radicle.Interop;

public readonly record struct QuestRuntimeProperty(string Script, string Name, byte Type);

public readonly record struct QuestRuntimeFragment(ushort Stage, int StageIndex, string Script, string Function);

/// <summary>
/// The VMAD-derived runtime data Radicle decodes for one quest.
/// </summary>
/// <remarks>
/// Everything here comes from the QUST record's VMAD and DNAM subrecords. Radicle never guesses,
/// so anything it could not decode is reported in <see cref="Unsupported"/> and the caller is
/// expected to count it rather than treat it as absent.
/// </remarks>
public sealed record QuestRuntimeData(
    FormKey Quest,
    bool StartGameEnabled,
    IReadOnlyList<QuestRuntimeProperty> ScriptProperties,
    string FragmentScript,
    IReadOnlyList<QuestRuntimeProperty> FragmentProperties,
    IReadOnlyList<QuestRuntimeFragment> Fragments,
    IReadOnlyList<int> Aliases,
    IReadOnlyList<QuestRuntimeProperty> AliasScriptProperties,
    IReadOnlyList<string> Unsupported);

public static class QuestRuntime
{
    /// <summary>One tab-separated row per fact, exactly as the C ABI returns them.</summary>
    public static unsafe string[] Rows(PluginFile plugin, FormKey quest)
    {
        IntPtr* rows = null;
        nuint count = 0;
        Call.Check(RadicleNative.QuestRuntimeRows(plugin.Handle, quest.ToString(), &rows, &count));
        return Call.TakeStrings(rows, count);
    }

    public static QuestRuntimeData Decode(PluginFile plugin, FormKey quest)
    {
        var scripts = new List<QuestRuntimeProperty>();
        var fragmentProperties = new List<QuestRuntimeProperty>();
        var fragments = new List<QuestRuntimeFragment>();
        var aliases = new List<int>();
        var aliasProperties = new List<QuestRuntimeProperty>();
        var unsupported = new List<string>();
        var fragmentScript = string.Empty;
        var startGameEnabled = false;

        foreach (var row in Rows(plugin, quest))
        {
            var fields = row.Split('\t');
            switch (fields[0])
            {
                case "quest":
                    startGameEnabled = fields.Length > 2 && fields[2] == "1";
                    break;
                case "script-property":
                    scripts.Add(new QuestRuntimeProperty(fields[1], fields[2], ParseType(fields[3])));
                    break;
                case "fragment-script":
                    fragmentScript = fields.Length > 1 ? fields[1] : string.Empty;
                    break;
                case "fragment-property":
                    fragmentProperties.Add(new QuestRuntimeProperty(string.Empty, fields[1], ParseType(fields[2])));
                    break;
                case "fragment":
                    fragments.Add(new QuestRuntimeFragment(
                        ushort.Parse(fields[1], CultureInfo.InvariantCulture),
                        int.Parse(fields[2], CultureInfo.InvariantCulture),
                        fields[3],
                        fields[4]));
                    break;
                case "alias":
                    aliases.Add(int.Parse(fields[1], CultureInfo.InvariantCulture));
                    break;
                case "alias-script-property":
                    aliasProperties.Add(new QuestRuntimeProperty(fields[2], fields[3], ParseType(fields[4])));
                    break;
                case "unsupported":
                    unsupported.Add(fields.Length > 1 ? fields[1] : string.Empty);
                    break;
            }
        }

        return new QuestRuntimeData(quest, startGameEnabled, scripts, fragmentScript, fragmentProperties,
            fragments, aliases, aliasProperties, unsupported);
    }

    private static byte ParseType(string text) => byte.Parse(text, CultureInfo.InvariantCulture);
}
