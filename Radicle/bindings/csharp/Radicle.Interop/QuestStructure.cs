using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using Radicle.Interop.Native;

namespace Radicle.Interop;

public sealed record QuestStoredText(uint? StringId, byte[]? InlineBytes, byte[]? MalformedBytes = null);
public sealed record QuestStoredLog(byte Flags, QuestStoredText? Text, byte[]? NoteBytes);
public sealed record QuestStoredStage(ushort Index, byte Flags, byte OpaqueByte,
                                      IReadOnlyList<QuestStoredLog> Logs, bool IsStartup = false);
public sealed record QuestStoredTarget(int Alias, uint Flags, FormKey? Keyword);
public sealed record QuestStoredObjective(ushort Index, uint Flags, QuestStoredText? Text,
                                          IReadOnlyList<QuestStoredTarget> Targets);
public sealed record QuestStoredFill(bool Supported, QuestFillType Type, FormKey? Target,
                                     FormKey? SecondaryTarget, int? Alias, ushort CreateMode,
                                     uint? Level, uint NearType, uint Event, uint EventData);
public sealed record QuestStoredCollectionEntry(uint Id, byte? MaxInitialFillCount);
public sealed record QuestStoredAlias(QuestAliasKind Kind, uint? Id, byte[]? NameBytes, uint? Flags,
                                      int? ForceIntoAlias, int? ClosestToAlias, QuestStoredFill Fill,
                                      IReadOnlyList<QuestStoredCollectionEntry> CollectionEntries,
                                      IReadOnlyList<FormKey> Packages);
public sealed record QuestStoredCondition(byte FlagsByte, uint ComparisonRaw, ushort Function,
                                          ushort Unknown2, int Param1, int Param2, uint RunOn,
                                          uint Reference, int Unknown3,
                                          byte[]? Parameter1Bytes, byte[]? Parameter2Bytes);
public sealed record QuestStoredConditionList(QuestScope Scope, nuint Primary, nuint Secondary,
                                              bool Supported, IReadOnlyList<QuestStoredCondition> Conditions);
public sealed record QuestStoredUnsupported(QuestScope Scope, nuint Primary, nuint Secondary,
                                            nuint Member, string Signature, string Reason);
public sealed record QuestStoredStructure(string SourcePlugin, bool StagesSupported,
                                          bool ObjectivesSupported, bool AliasesSupported,
                                          IReadOnlyList<QuestStoredStage> Stages,
                                          IReadOnlyList<QuestStoredObjective> Objectives,
                                          IReadOnlyList<QuestStoredAlias> Aliases,
                                          IReadOnlyList<QuestStoredConditionList> ConditionLists,
                                          IReadOnlyList<QuestStoredUnsupported> Unsupported)
{
    public int StartStageCount => Stages.Count(stage => stage.IsStartup);

    // Constraint: exactly one stage carrying the startup bit identifies the startup stage; none or several yield null.
    public ushort? StartStage => StartStageCount == 1 ? Stages.Single(stage => stage.IsStartup).Index : null;
}

public sealed record QuestStartStageInfo(bool StagesSupported, ushort? Stage, int MarkedCount);
public sealed record QuestEnvRead(QuestStoredStructure Structure, QuestStartStageInfo StartStage);

public static class QuestStructure
{
    private static byte[]? Bytes(IntPtr pointer, nuint size) =>
        pointer == IntPtr.Zero ? null : Utf8.Raw(pointer, size);

    private static QuestStoredText? Text(QuestText text) => text.Kind switch
    {
        NameKind.Absent => null,
        NameKind.StringId => new QuestStoredText(text.StringId, null),
        NameKind.Inline => new QuestStoredText(null, Bytes(text.Bytes, text.Size)),
        NameKind.Malformed => new QuestStoredText(null, null, Bytes(text.Bytes, text.Size)),
        _ => throw new RadicleException(Status.ParseError, "Unsupported stored quest text kind")
    };

    private static T[] Range<T>(T[] items, nuint first, nuint count) =>
        items.AsSpan(checked((int)first), checked((int)count)).ToArray();

    public static unsafe QuestStoredStructure FromView(QuestStructureView view)
    {
        var logs = Call.Copy<QuestLog>(view.Logs, view.LogCount)
            .Select(row => new QuestStoredLog(row.Flags, Text(row.Text), Bytes(row.Note, row.NoteSize))).ToArray();
        var stages = Call.Copy<QuestStage>(view.Stages, view.StageCount)
            .Select(row => new QuestStoredStage(row.Index, row.Flags, row.OpaqueByte,
                                                Range(logs, row.FirstLog, row.LogCount), row.IsStartupStage != 0)).ToArray();
        var targets = Call.Copy<QuestTarget>(view.Targets, view.TargetCount)
            .Select(row => new QuestStoredTarget(row.Alias, row.Flags, FormKey.FromNative(row.Keyword))).ToArray();
        var objectives = Call.Copy<QuestObjective>(view.Objectives, view.ObjectiveCount)
            .Select(row => new QuestStoredObjective(row.Index, row.Flags, Text(row.Text),
                                                    Range(targets, row.FirstTarget, row.TargetCount))).ToArray();
        var entries = Call.Copy<QuestCollectionEntry>(view.CollectionEntries, view.CollectionEntryCount)
            .Select(row => new QuestStoredCollectionEntry(row.Id,
                row.HasMaxInitialFillCount != 0 ? row.MaxInitialFillCount : null)).ToArray();
        var packages = Call.Copy<Native.FormKey>(view.Packages, view.PackageCount)
            .Select(row => FormKey.Required(row, "quest alias package")).ToArray();
        var aliases = Call.Copy<QuestAlias>(view.Aliases, view.AliasCount)
            .Select(row => new QuestStoredAlias(row.Kind,
                row.Kind == QuestAliasKind.Collection ? null : row.Id,
                Bytes(row.Name, row.NameSize), row.HasFlags != 0 ? row.Flags : null,
                row.HasForceIntoAlias != 0 ? row.ForceIntoAlias : null,
                row.HasClosestToAlias != 0 ? row.ClosestToAlias : null,
                new QuestStoredFill(row.FillSupported != 0, row.FillType,
                    FormKey.FromNative(row.Target), FormKey.FromNative(row.SecondaryTarget),
                    row.HasFillAlias != 0 ? row.FillAlias : null, row.CreateMode,
                    row.HasLevel != 0 ? row.Level : null, row.NearType, row.Event, row.EventData),
                Range(entries, row.FirstCollectionEntry, row.CollectionEntryCount),
                Range(packages, row.FirstPackage, row.PackageCount))).ToArray();
        var conditions = Call.Copy<QuestCondition>(view.Conditions, view.ConditionCount)
            .Select(row => new QuestStoredCondition(row.FlagsByte, row.ComparisonRaw, row.Function,
                row.Unknown2, row.Param1, row.Param2, row.RunOn, row.Reference, row.Unknown3,
                Bytes(row.Parameter1String, row.Parameter1Size), Bytes(row.Parameter2String, row.Parameter2Size))).ToArray();
        var lists = Call.Copy<QuestConditionList>(view.ConditionLists, view.ConditionListCount)
            .Select(row => new QuestStoredConditionList(row.Scope, row.Primary, row.Secondary,
                row.Supported != 0, Range(conditions, row.FirstCondition, row.ConditionCount))).ToArray();
        var unsupported = new List<QuestStoredUnsupported>();
        foreach (var row in Call.Copy<QuestUnsupported>(view.Unsupported, view.UnsupportedCount))
        {
            var item = row;
            var signature = Encoding.ASCII.GetString(new ReadOnlySpan<byte>(item.Signature, 4)).TrimEnd('\0');
            unsupported.Add(new QuestStoredUnsupported(item.Scope, item.Primary, item.Secondary,
                item.Member, signature, Utf8.Read(item.Reason) ?? string.Empty));
        }

        return new QuestStoredStructure(Utf8.Read(view.SourcePlugin)!, view.StagesSupported != 0,
            view.ObjectivesSupported != 0, view.AliasesSupported != 0,
            stages, objectives, aliases, lists, unsupported);
    }

    // Constraint: null when the env has no record for the key; a winner that is not a QUST throws.
    public static unsafe QuestEnvRead? ReadEnv(RadicleEnvironment env, FormKey quest)
    {
        ArgumentNullException.ThrowIfNull(env);
        using var use = env.Use();
        using var strings = new NativeStrings();
        var snapshot = IntPtr.Zero;
        if (!Call.Found(RadicleNative.QuestStructureReadEnv(use.Pointer, strings.Key(quest), &snapshot))) return null;
        try
        {
            QuestStructureView view = default;
            Call.Check(RadicleNative.QuestStructureGetView(snapshot, &view));
            QuestStartStage start = default;
            Call.Check(RadicleNative.QuestStructureStartStage(snapshot, &start));
            return new QuestEnvRead(FromView(view),
                new QuestStartStageInfo(start.StagesSupported != 0, start.Resolved != 0 ? start.Stage : null,
                    checked((int)start.MarkedCount)));
        }
        finally
        {
            RadicleNative.QuestStructureDestroy(snapshot);
        }
    }

    public static unsafe QuestStoredStructure Read(PluginFile plugin, FormKey quest)
    {
        var snapshot = IntPtr.Zero;
        using var use = plugin.Use();
        Call.Check(RadicleNative.QuestStructureRead(use.Pointer, quest.ToString(), &snapshot));
        try
        {
            QuestStructureView view = default;
            Call.Check(RadicleNative.QuestStructureGetView(snapshot, &view));
            return FromView(view);
        }
        finally
        {
            RadicleNative.QuestStructureDestroy(snapshot);
        }
    }
}
