using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Text;
using Radicle.Interop.Native;

namespace Radicle.Interop;

public readonly record struct QuestRuntimeValue
{
    public int? Integer { get; init; }

    public float? Real { get; init; }

    public bool? Boolean { get; init; }

    public string? Text { get; init; }

    public byte[]? TextBytes => Text is null ? null : Encoding.Latin1.GetBytes(Text);

    public uint? ObjectFormId { get; init; }

    public short? Alias { get; init; }

    public uint? ElementCount { get; init; }

    public IReadOnlyList<ScriptElement>? Elements { get; init; }

    public IReadOnlyList<ScriptStruct>? Structs { get; init; }
}

public readonly record struct QuestRuntimeProperty(
    string Script,
    string Name,
    byte Type,
    byte Flags,
    QuestRuntimeValue Value);

public readonly record struct QuestRuntimeScript(int Alias, string Script);

public readonly record struct QuestRuntimeAliasScriptProperty(
    int Alias,
    string Script,
    string Name,
    byte Type,
    byte Flags,
    QuestRuntimeValue Value);

public readonly record struct QuestRuntimeFragment(ushort Stage, int StageIndex, string Script, string Function);

public readonly record struct QuestRuntimePropertyEntry(
    QuestPropertyOwner Owner,
    int Alias,
    string Script,
    string Name,
    byte Type,
    byte Flags,
    QuestRuntimeValue Value);

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
    public const int QuestOwnedAlias = -1;

    public static unsafe string[] Rows(PluginFile plugin, FormKey quest)
    {
        IntPtr* rows = null;
        nuint count = 0;
        using var use = plugin.Use();
        Call.Check(RadicleNative.QuestRuntimeRows(use.Pointer, quest.ToString(), &rows, &count));
        return Call.TakeStrings(rows, count);
    }

    public static unsafe IReadOnlyList<QuestRuntimePropertyEntry> Properties(PluginFile plugin, FormKey quest)
    {
        QuestProperty* properties = null;
        nuint count = 0;
        using var use = plugin.Use();
        Call.Check(RadicleNative.QuestRuntimeProperties(use.Pointer, quest.ToString(), &properties, &count));
        try
        {
            var entries = new QuestRuntimePropertyEntry[checked((int)count)];
            for (var i = 0; i < entries.Length; ++i)
            {
                var property = properties[i];
                var text = property.Text == IntPtr.Zero ? null : Utf8.Raw(property.Text, property.TextSize);
                entries[i] = new QuestRuntimePropertyEntry(
                    property.Owner,
                    property.Alias,
                    Utf8.Read(property.Script) ?? string.Empty,
                    Utf8.Read(property.Name) ?? string.Empty,
                    property.Type,
                    property.Flags,
                    new QuestRuntimeValue
                    {
                        Integer = property.HasInteger != 0 ? property.Integer : null,
                        Real = property.HasReal != 0 ? property.Real : null,
                        Boolean = property.HasBoolean != 0 ? property.Boolean != 0 : null,
                        Text = text is null ? null : Encoding.Latin1.GetString(text),
                        ObjectFormId = property.HasObject != 0 ? property.ObjectFormId : null,
                        Alias = property.HasPropertyAlias != 0 ? property.PropertyAlias : null,
                        ElementCount = property.HasElementCount != 0 ? property.ElementCount : null,
                        Elements = property.HasElements != 0
                            ? Call.Copy<Native.ScriptElement>(property.Elements, property.ElementValueCount)
                                .Select(e => new ScriptElement(FormKey.FromNative(e.Object), e.Alias, Utf8.Read(e.Text),
                                                               e.Integer, e.Real, e.Boolean != 0))
                                .ToArray()
                            : null,
                        Structs = property.HasStructs != 0
                            ? RadicleEnvironment.ScriptStructs(property.Structs, property.StructCount)
                            : null,
                    });
            }
            return entries;
        }
        finally
        {
            RadicleNative.FreeQuestProperties(properties, count);
        }
    }

    public static QuestRuntimeData Decode(PluginFile plugin, FormKey quest) =>
        DecodeRows(Rows(plugin, quest), Properties(plugin, quest), quest);

    public static QuestRuntimeData DecodeRows(IReadOnlyList<string> rows,
                                              IReadOnlyList<QuestRuntimePropertyEntry> properties,
                                              FormKey quest)
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
                case "fragment-script":
                    fragmentScript = fields.Length > 1 ? fields[1] : string.Empty;
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
                case "unsupported":
                    unsupported.Add(fields.Length > 1 ? fields[1] : string.Empty);
                    break;
            }
        }

        foreach (var entry in properties)
        {
            switch (entry.Owner)
            {
                case QuestPropertyOwner.Script:
                    scriptProperties.Add(new QuestRuntimeProperty(
                        entry.Script, entry.Name, entry.Type, entry.Flags, entry.Value));
                    break;
                case QuestPropertyOwner.Fragment:
                    fragmentProperties.Add(new QuestRuntimeProperty(
                        entry.Script, entry.Name, entry.Type, entry.Flags, entry.Value));
                    break;
                case QuestPropertyOwner.AliasScript:
                    aliasProperties.Add(new QuestRuntimeAliasScriptProperty(
                        entry.Alias, entry.Script, entry.Name, entry.Type, entry.Flags, entry.Value));
                    break;
            }
        }

        return new QuestRuntimeData(quest, startGameEnabled, scripts, scriptProperties, fragmentScript,
            fragmentProperties, fragments, aliases, aliasScripts, aliasProperties, unsupported);
    }
}
