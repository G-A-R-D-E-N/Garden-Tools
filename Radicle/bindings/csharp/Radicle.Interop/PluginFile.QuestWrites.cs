using System;
using System.Collections.Generic;
using Radicle.Interop.Native;

namespace Radicle.Interop;

public sealed record QuestAliasEdit(QuestAliasKind Kind, uint Id)
{
    public string? Name { get; init; }
    public uint Flags { get; init; }
    public int? ForceIntoAlias { get; init; }
    public int? ClosestToAlias { get; init; }
    public QuestFillType FillType { get; init; } = QuestFillType.None;
    public FormKey? Target { get; init; }
    public FormKey? SecondaryTarget { get; init; }
    public int? FillAlias { get; init; }
    public ushort CreateMode { get; init; }
    public uint Level { get; init; }
    public uint NearType { get; init; }
    public uint Event { get; init; }
    public uint EventData { get; init; }
    public IReadOnlyList<QuestStoredCollectionEntry>? CollectionEntries { get; init; }
}

public sealed unsafe partial class PluginFile
{
    public bool AddQuestStage(FormKey quest, ushort index, byte flags) =>
        Edit(quest, (plugin, s, key) => RadicleNative.PluginAddQuestStage(plugin, key, index, flags));

    public bool SetQuestStageFlags(FormKey quest, ushort index, byte flags) =>
        Edit(quest, (plugin, s, key) => RadicleNative.PluginSetQuestStageFlags(plugin, key, index, flags));

    public bool RemoveQuestStage(FormKey quest, ushort index) =>
        Edit(quest, (plugin, s, key) => RadicleNative.PluginRemoveQuestStage(plugin, key, index));

    public bool ReorderQuestStage(FormKey quest, ushort index, int to)
    {
        var destination = Index(to);
        return Edit(quest, (plugin, s, key) => RadicleNative.PluginReorderQuestStage(plugin, key, index, destination));
    }

    public bool SetQuestLogEntry(FormKey quest, ushort stageIndex, int logIndex, byte flags,
                                 string? inlineText = null, uint? textStringId = null, string? note = null)
    {
        var (kind, id) = ObjectiveText(inlineText, textStringId);
        var at = Index(logIndex);
        return Edit(quest, (plugin, s, key) => RadicleNative.PluginSetQuestLogEntry(
            plugin, key, stageIndex, at, flags, kind, inlineText, id, note));
    }

    public bool AddQuestLogEntry(FormKey quest, ushort stageIndex, byte flags,
                                 string? inlineText = null, uint? textStringId = null, string? note = null)
    {
        var (kind, id) = ObjectiveText(inlineText, textStringId);
        return Edit(quest, (plugin, s, key) => RadicleNative.PluginAddQuestLogEntry(
            plugin, key, stageIndex, flags, kind, inlineText, id, note));
    }

    // Constraint: inlineText and textStringId are exclusive; neither keeps the stored text on a set.
    public bool AddQuestObjective(FormKey quest, ushort index, uint flags, string? inlineText = null, uint? textStringId = null)
    {
        var (kind, id) = ObjectiveText(inlineText, textStringId);
        return Edit(quest, (plugin, s, key) =>
            RadicleNative.PluginAddQuestObjective(plugin, key, index, flags, kind, inlineText, id));
    }

    public bool SetQuestObjective(FormKey quest, ushort index, uint flags, string? inlineText = null, uint? textStringId = null)
    {
        var (kind, id) = ObjectiveText(inlineText, textStringId);
        return Edit(quest, (plugin, s, key) =>
            RadicleNative.PluginSetQuestObjective(plugin, key, index, flags, kind, inlineText, id));
    }

    public bool RemoveQuestObjective(FormKey quest, ushort index) =>
        Edit(quest, (plugin, s, key) => RadicleNative.PluginRemoveQuestObjective(plugin, key, index));

    public bool ReorderQuestObjective(FormKey quest, ushort index, int to)
    {
        var destination = Index(to);
        return Edit(quest, (plugin, s, key) => RadicleNative.PluginReorderQuestObjective(plugin, key, index, destination));
    }

    public bool AddQuestObjectiveTarget(FormKey quest, ushort objectiveIndex, int alias, uint flags, FormKey? keyword = null) =>
        Edit(quest, (plugin, s, key) =>
            RadicleNative.PluginAddQuestObjectiveTarget(plugin, key, objectiveIndex, alias, flags, s.Key(keyword)));

    public bool SetQuestObjectiveTarget(FormKey quest, ushort objectiveIndex, int targetIndex, int alias, uint flags,
                                        FormKey? keyword = null)
    {
        var at = Index(targetIndex);
        return Edit(quest, (plugin, s, key) =>
            RadicleNative.PluginSetQuestObjectiveTarget(plugin, key, objectiveIndex, at, alias, flags, s.Key(keyword)));
    }

    public bool RemoveQuestObjectiveTarget(FormKey quest, ushort objectiveIndex, int targetIndex)
    {
        var at = Index(targetIndex);
        return Edit(quest, (plugin, s, key) =>
            RadicleNative.PluginRemoveQuestObjectiveTarget(plugin, key, objectiveIndex, at));
    }

    public bool AddQuestAlias(FormKey quest, QuestAliasEdit alias)
    {
        ArgumentNullException.ThrowIfNull(alias);
        return Edit(quest, (plugin, s, key) =>
        {
            var input = AliasInput(alias, s);
            return RadicleNative.PluginAddQuestAlias(plugin, key, &input);
        });
    }

    // Constraint: alias.Id must equal aliasId; members the edit does not carry are kept.
    public bool ReplaceQuestAlias(FormKey quest, uint aliasId, QuestAliasEdit alias)
    {
        ArgumentNullException.ThrowIfNull(alias);
        return Edit(quest, (plugin, s, key) =>
        {
            var input = AliasInput(alias, s);
            return RadicleNative.PluginReplaceQuestAlias(plugin, key, aliasId, &input);
        });
    }

    public bool RemoveQuestAlias(FormKey quest, uint aliasId) =>
        Edit(quest, (plugin, s, key) => RadicleNative.PluginRemoveQuestAlias(plugin, key, aliasId));

    public bool ReorderQuestAlias(FormKey quest, uint aliasId, int to)
    {
        var destination = Index(to);
        return Edit(quest, (plugin, s, key) => RadicleNative.PluginReorderQuestAlias(plugin, key, aliasId, destination));
    }

    private static (NameKind Kind, uint StringId) ObjectiveText(string? inlineText, uint? textStringId)
    {
        if (inlineText is not null && textStringId is not null)
            throw new ArgumentException("Objective text is either inline or a string id, not both.");
        if (inlineText is not null) return (NameKind.Inline, 0);
        return textStringId is { } id ? (NameKind.StringId, id) : (NameKind.Absent, 0);
    }

    private static QuestAliasInput AliasInput(QuestAliasEdit alias, NativeStrings s)
    {
        var entries = new List<QuestCollectionEntry>();
        foreach (var entry in alias.CollectionEntries ?? Array.Empty<QuestStoredCollectionEntry>())
        {
            entries.Add(new QuestCollectionEntry
            {
                Id = entry.Id,
                HasMaxInitialFillCount = (byte)(entry.MaxInitialFillCount.HasValue ? 1 : 0),
                MaxInitialFillCount = entry.MaxInitialFillCount ?? 0
            });
        }

        return new QuestAliasInput
        {
            Kind = alias.Kind,
            Id = alias.Id,
            Name = s.Add(alias.Name),
            Flags = alias.Flags,
            HasForceIntoAlias = (byte)(alias.ForceIntoAlias.HasValue ? 1 : 0),
            ForceIntoAlias = alias.ForceIntoAlias ?? 0,
            HasClosestToAlias = (byte)(alias.ClosestToAlias.HasValue ? 1 : 0),
            ClosestToAlias = alias.ClosestToAlias ?? 0,
            FillType = alias.FillType,
            Target = s.Key(alias.Target),
            SecondaryTarget = s.Key(alias.SecondaryTarget),
            HasFillAlias = (byte)(alias.FillAlias.HasValue ? 1 : 0),
            FillAlias = alias.FillAlias ?? 0,
            CreateMode = alias.CreateMode,
            Level = alias.Level,
            NearType = alias.NearType,
            Event = alias.Event,
            EventData = alias.EventData,
            CollectionEntries = s.AddArray(entries.ToArray()),
            CollectionEntryCount = (nuint)entries.Count
        };
    }
}
