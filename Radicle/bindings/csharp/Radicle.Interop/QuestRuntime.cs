using System;
using System.Collections.Generic;
using System.Globalization;
using Radicle.Interop.Native;

namespace Radicle.Interop;

public readonly record struct QuestRuntimeProperty(string Script, string Name, byte Type);

/// <summary>
/// A script attached to a quest or to one of its aliases.
/// </summary>
/// <param name="Alias">The alias id that owns the script, or -1 when the quest itself owns it.</param>
/// <param name="Script">The script name.</param>
public readonly record struct QuestRuntimeScript(int Alias, string Script);

/// <summary>
/// A property of an alias-owned script, keeping the alias it belongs to.
/// </summary>
/// <param name="Alias">The alias id that owns the script.</param>
/// <param name="Script">The owning script name.</param>
/// <param name="Name">The property name.</param>
/// <param name="Type">The decoded VMAD property type.</param>
public readonly record struct QuestRuntimeAliasScriptProperty(int Alias, string Script, string Name, byte Type);

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
    IReadOnlyList<QuestRuntimeScript> Scripts,
    IReadOnlyList<QuestRuntimeProperty> ScriptProperties,
    string FragmentScript,
    IReadOnlyList<QuestRuntimeProperty> FragmentProperties,
    IReadOnlyList<QuestRuntimeFragment> Fragments,
    IReadOnlyList<int> Aliases,
    IReadOnlyList<QuestRuntimeScript> AliasScripts,
    IReadOnlyList<QuestRuntimeAliasScriptProperty> AliasScriptProperties,
    IReadOnlyList<string> Unsupported);

public static class QuestRuntime
{
    /// <summary>The alias id used for a script the quest itself owns.</summary>
    public const int QuestOwnedAlias = -1;

    /// <summary>One tab-separated row per fact, exactly as the C ABI returns them.</summary>
    public static unsafe string[] Rows(PluginFile plugin, FormKey quest)
    {
        IntPtr* rows = null;
        nuint count = 0;
        Call.Check(RadicleNative.QuestRuntimeRows(plugin.Handle, quest.ToString(), &rows, &count));
        return Call.TakeStrings(rows, count);
    }

    public static QuestRuntimeData Decode(PluginFile plugin, FormKey quest) =>
        DecodeRows(Rows(plugin, quest), quest);

    /// <summary>
    /// Maps the C ABI rows onto the runtime model.
    /// </summary>
    /// <remarks>
    /// Every row kind the C ABI emits is represented here, including the attachment rows: a
    /// <c>script</c> row is a script the quest owns even when it declares no properties, and an
    /// <c>alias-script</c> row is a script an alias owns. Alias-owned scripts and their properties keep
    /// the alias id, so two aliases carrying the same script name stay distinct.
    /// </remarks>
    public static QuestRuntimeData DecodeRows(IReadOnlyList<string> rows, FormKey quest)
    {
        var scripts = new List<QuestRuntimeScript>();
        var scriptProperties = new List<QuestRuntimeProperty>();
        var fragmentProperties = new List<QuestRuntimeProperty>();
        var fragments = new List<QuestRuntimeFragment>();
        var aliases = new List<int>();
        var aliasScripts = new List<QuestRuntimeScript>();
        var aliasProperties = new List<QuestRuntimeAliasScriptProperty>();
        var unsupported = new List<string>();
        var fragmentScript = string.Empty;
        var startGameEnabled = false;

        foreach (var row in rows)
        {
            var fields = row.Split('\t');
            switch (fields[0])
            {
                case "quest":
                    startGameEnabled = fields.Length > 2 && fields[2] == "1";
                    break;
                case "script":
                    scripts.Add(new QuestRuntimeScript(QuestOwnedAlias, fields[1]));
                    break;
                case "script-property":
                    scriptProperties.Add(new QuestRuntimeProperty(fields[1], fields[2], ParseType(fields[3])));
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
                case "alias-script":
                    aliasScripts.Add(new QuestRuntimeScript(
                        int.Parse(fields[1], CultureInfo.InvariantCulture), fields[2]));
                    break;
                case "alias-script-property":
                    aliasProperties.Add(new QuestRuntimeAliasScriptProperty(
                        int.Parse(fields[1], CultureInfo.InvariantCulture),
                        fields[2],
                        fields[3],
                        ParseType(fields[4])));
                    break;
                case "unsupported":
                    unsupported.Add(fields.Length > 1 ? fields[1] : string.Empty);
                    break;
            }
        }

        return new QuestRuntimeData(quest, startGameEnabled, scripts, scriptProperties, fragmentScript,
            fragmentProperties, fragments, aliases, aliasScripts, aliasProperties, unsupported);
    }

    private static byte ParseType(string text) => byte.Parse(text, CultureInfo.InvariantCulture);
}
