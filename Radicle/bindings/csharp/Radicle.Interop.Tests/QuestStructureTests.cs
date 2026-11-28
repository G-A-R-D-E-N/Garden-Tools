using System;
using System.Linq;
using System.Text;
using Radicle.Interop.Native;
using Xunit;
using static Radicle.Interop.Tests.Bytes;
using static Radicle.Interop.Tests.PluginWriter;

namespace Radicle.Interop.Tests;

public class QuestStructureTests
{
    private static byte[] Condition(ushort function, bool opaque = false) =>
        (opaque ? new byte[] { 0, 0x12, 0x34, 0x56 } : new byte[4])
        .Concat(U32(0x3f800000, function, 0xfffffffd, 5, 2, 0x800, 0xffffffff)).ToArray();

    private static byte[] Fixture(bool localized = false, bool malformed = false, bool opaque = false, bool shortText = false) =>
        Build(localized ? 0x80u : 0u, Array.Empty<string>(), Top("QUST", new Rec("QUST", 0x800, new[]
        {
            ("EDID", Z("TestQuest")), ("CTDA", Condition(70, opaque)), ("NEXT", Array.Empty<byte>()),
            ("CTDA", Condition(277, opaque)), ("INDX", new byte[] { 10, 0, 2, 7 }), ("QSDT", new byte[] { 1 }),
            ("CTDA", malformed ? new byte[1] : Condition(72, opaque)), ("CIS1", Z("")), ("NAM2", Z("")),
            ("CNAM", localized ? U32(0x1234) : new byte[] { 0xe9, 0 }),
            ("QOBJ", new byte[] { 30, 0 }), ("FNAM", U32(2)), ("NNAM", localized ? (shortText ? new byte[] { 0x45, 0x23, 0 } : U32(0x2345)) : Z("")),
            ("QSTA", U32(0xffffffff, 6, 0x800)), ("CTDA", Condition(73, opaque)), ("ANAM", U32(3)),
            ("ALST", U32(1)), ("ALID", Z("ref")), ("FNAM", U32(7)), ("ALFI", U32(0xfffffffd)),
            ("ALFR", U32(0x800)), ("CTDA", Condition(74, opaque)), ("ALED", Array.Empty<byte>()),
            ("ALCS", U32(2)), ("ALMI", new byte[] { 2 })
        })));

    [Fact]
    public void CopiesEveryStoredFamilyAndPreservesScopesAfterDisposal()
    {
        using var temp = new TempDirectory();
        var bytes = Fixture();
        QuestStoredStructure value;
        using (var plugin = PluginFile.Read(temp.Write("Quests.esp", bytes)))
        {
            value = QuestStructure.Read(plugin, new Radicle.Interop.FormKey("Quests.esp", 0x800));
            Assert.Equal(bytes, plugin.ToBytes());
        }
        Assert.True(value.StagesSupported && value.ObjectivesSupported && value.AliasesSupported);
        Assert.Empty(value.Unsupported);
        var stage = Assert.Single(value.Stages);
        Assert.Equal((ushort)10, stage.Index);
        Assert.Equal((byte)7, stage.OpaqueByte);
        Assert.True(stage.IsStartup);
        Assert.Equal((ushort)10, value.StartStage);
        Assert.Equal(1, value.StartStageCount);
        var log = Assert.Single(stage.Logs);
        Assert.Equal(new byte[] { 0xe9 }, log.Text!.InlineBytes);
        Assert.Empty(log.NoteBytes!);
        var objective = Assert.Single(value.Objectives);
        Assert.Equal(30, objective.Index);
        Assert.Empty(objective.Text!.InlineBytes!);
        var target = Assert.Single(objective.Targets);
        Assert.Equal(-1, target.Alias);
        Assert.Equal(new Radicle.Interop.FormKey("Quests.esp", 0x800), target.Keyword);
        var alias = value.Aliases[0];
        Assert.Equal(Encoding.ASCII.GetBytes("ref"), alias.NameBytes);
        Assert.Equal(-3, alias.ForceIntoAlias);
        Assert.Equal(QuestFillType.SpecificReference, alias.Fill.Type);
        Assert.True(alias.Fill.Supported);
        Assert.Equal(QuestAliasKind.Collection, value.Aliases[1].Kind);
        Assert.Null(value.Aliases[1].Id);
        Assert.Equal((byte)2, Assert.Single(value.Aliases[1].CollectionEntries).MaxInitialFillCount);
        Assert.Equal(new ushort[] { 70, 277, 72, 73, 74 },
            value.ConditionLists.SelectMany(list => list.Conditions).Select(c => c.Function));
        Assert.Equal(QuestScope.Dialogue, value.ConditionLists[0].Scope);
        Assert.Equal(QuestScope.StoryManager, value.ConditionLists[1].Scope);
        var scoped = value.ConditionLists.Single(list => list.Scope == QuestScope.LogEntry);
        Assert.Equal((nuint)0, scoped.Primary);
        Assert.Equal((nuint)0, scoped.Secondary);
        Assert.Empty(Assert.Single(scoped.Conditions).Parameter1Bytes!);
    }

    [Fact]
    public void LocalizedTextRetainsIds()
    {
        using var temp = new TempDirectory();
        using var plugin = PluginFile.Read(temp.Write("Quests.esp", Fixture(true)));
        var value = QuestStructure.Read(plugin, new Radicle.Interop.FormKey("Quests.esp", 0x800));
        Assert.Equal(0x1234u, value.Stages[0].Logs[0].Text!.StringId);
        Assert.Null(value.Stages[0].Logs[0].Text!.InlineBytes);
        Assert.Equal(0x2345u, value.Objectives[0].Text!.StringId);
    }

    [Fact]
    public void ShortLocalizedObjectiveTextIsReportedAsMalformed()
    {
        using var temp = new TempDirectory();
        var bytes = Fixture(localized: true, shortText: true);
        using var plugin = PluginFile.Read(temp.Write("Quests.esp", bytes));
        var value = QuestStructure.Read(plugin, new Radicle.Interop.FormKey("Quests.esp", 0x800));
        var text = value.Objectives[0].Text!;
        Assert.Null(text.StringId);
        Assert.Null(text.InlineBytes);
        Assert.Equal(new byte[] { 0x45, 0x23, 0 }, text.MalformedBytes);
        Assert.Equal(bytes, plugin.ToBytes());
    }

    [Fact]
    public void OpaqueConditionPrefixesRemainSupportedAndByteExact()
    {
        using var temp = new TempDirectory();
        var bytes = Fixture(opaque: true);
        using var plugin = PluginFile.Read(temp.Write("Quests.esp", bytes));
        var value = QuestStructure.Read(plugin, new Radicle.Interop.FormKey("Quests.esp", 0x800));
        Assert.Empty(value.Unsupported);
        Assert.All(value.ConditionLists, list => Assert.True(list.Supported));
        var conditions = value.ConditionLists.SelectMany(list => list.Conditions).ToArray();
        Assert.Equal(new ushort[] { 70, 277, 72, 73, 74 }, conditions.Select(c => c.Function));
        Assert.All(conditions, condition =>
        {
            Assert.Equal(0x3f800000u, condition.ComparisonRaw);
            Assert.Equal(-3, condition.Param1);
            Assert.Equal(5, condition.Param2);
            Assert.Equal(0x800u, condition.Reference);
        });
        Assert.Equal(bytes, plugin.ToBytes());
    }

    [Fact]
    public void UnsupportedListsAreObservableAndMissingRecordsFail()
    {
        using var temp = new TempDirectory();
        using var plugin = PluginFile.Read(temp.Write("Quests.esp", Fixture(malformed: true)));
        var value = QuestStructure.Read(plugin, new Radicle.Interop.FormKey("Quests.esp", 0x800));
        var log = value.ConditionLists.Single(list => list.Scope == QuestScope.LogEntry);
        Assert.False(log.Supported);
        Assert.Empty(log.Conditions);
        Assert.Contains(value.Unsupported, item => item.Scope == QuestScope.LogEntry && item.Signature == "CTDA");
        Assert.Equal(Status.NotFound, Assert.Throws<RadicleException>(() =>
            QuestStructure.Read(plugin, new Radicle.Interop.FormKey("Quests.esp", 0x999))).Status);
        plugin.Dispose();
        Assert.Throws<ObjectDisposedException>(() =>
            QuestStructure.Read(plugin, new Radicle.Interop.FormKey("Quests.esp", 0x800)));
    }

    private static readonly Radicle.Interop.FormKey QuestKey = new("Quests.esp", 0x800);

    private static byte[] Override(params (ushort Index, byte Flags)[] stages) =>
        Build(0, new[] { "Quests.esp" }, Top("QUST", new Rec("QUST", 0x800,
            new[] { ("EDID", Z("TestQuest")) }
                .Concat(stages.Select(stage => ("INDX", new[] { (byte)stage.Index, (byte)(stage.Index >> 8), stage.Flags, (byte)0 })))
                .Append(("ANAM", U32(1))).ToArray())),
            Top("GLOB", new Rec("GLOB", 0x01000801, new[] { ("EDID", Z("TestGlobal")) })));

    private static RadicleEnvironment LoadWithOverride(TempDirectory temp, params (ushort Index, byte Flags)[] stages) =>
        RadicleEnvironment.Load(new[]
        {
            temp.Write("Quests.esp", Fixture()),
            temp.Write("Over.esp", Override(stages))
        });

    // Constraint: ReadEnv returns the winning override, so stage 99 and not the master's stage 10.
    [Fact]
    public void ReadEnvReturnsTheWinnerAndItsStartStage()
    {
        using var temp = new TempDirectory();
        using var env = LoadWithOverride(temp, (99, 2), (98, 0));
        var read = QuestStructure.ReadEnv(env, QuestKey)!;
        Assert.Equal("Over.esp", read.Structure.SourcePlugin);
        Assert.Equal(new ushort[] { 99, 98 }, read.Structure.Stages.Select(stage => stage.Index));
        Assert.Equal((ushort)99, read.StartStage.Stage);
        Assert.Equal(1, read.StartStage.MarkedCount);
        Assert.True(read.StartStage.StagesSupported);
        Assert.Equal(read.Structure.StartStage, read.StartStage.Stage);
        Assert.Null(QuestStructure.ReadEnv(env, new Radicle.Interop.FormKey("Quests.esp", 0x8ff)));
        Assert.Equal(Status.InvalidArgument, Assert.Throws<RadicleException>(() =>
            QuestStructure.ReadEnv(env, new Radicle.Interop.FormKey("Over.esp", 0x801))).Status);
    }

    // Constraint: two marked stages or none resolve to no start stage.
    [Theory]
    [InlineData(2, 2)]
    [InlineData(0, 0)]
    public void StartStageIsNullUnlessExactlyOneStageIsMarked(int marked, int flags)
    {
        using var temp = new TempDirectory();
        var stages = Enumerable.Range(0, Math.Max(marked, 1)).Select(i => ((ushort)(10 + i), (byte)flags)).ToArray();
        using var env = LoadWithOverride(temp, stages);
        var read = QuestStructure.ReadEnv(env, QuestKey)!;
        Assert.Null(read.StartStage.Stage);
        Assert.Equal(marked, read.StartStage.MarkedCount);
        Assert.Null(read.Structure.StartStage);
    }

    // Constraint: a stage write changes only the stage array; objectives and aliases stay.
    [Fact]
    public void StageWritesChangeOnlyTheStageArray()
    {
        using var temp = new TempDirectory();
        using var plugin = PluginFile.Read(temp.Write("Quests.esp", Fixture()));
        Assert.True(plugin.AddQuestStage(QuestKey, 20, 0));
        Assert.Throws<RadicleException>(() => plugin.AddQuestStage(QuestKey, 20, 0));
        Assert.True(plugin.SetQuestStageFlags(QuestKey, 10, 0));
        var value = QuestStructure.Read(plugin, QuestKey);
        Assert.Equal(new ushort[] { 10, 20 }, value.Stages.Select(stage => stage.Index));
        Assert.Equal((byte)7, value.Stages[0].OpaqueByte);
        Assert.Single(value.Stages[0].Logs);
        Assert.Null(value.StartStage);
        Assert.Single(value.Objectives);
        Assert.Equal(2, value.Aliases.Count);
        Assert.True(plugin.RemoveQuestStage(QuestKey, 10));
        Assert.False(plugin.RemoveQuestStage(QuestKey, 10));
        Assert.False(plugin.SetQuestStageFlags(QuestKey, 10, 2));
        Assert.False(plugin.AddQuestStage(new Radicle.Interop.FormKey("Quests.esp", 0x8ff), 1, 0));
        Assert.Equal(new ushort[] { 20 }, QuestStructure.Read(plugin, QuestKey).Stages.Select(stage => stage.Index));
    }

    [Fact]
    public void LogEntrySetKeepsConditionsAndTreatsAbsentValuesAsUnchanged()
    {
        using var temp = new TempDirectory();
        using var plugin = PluginFile.Read(temp.Write("Quests.esp", Fixture()));
        Assert.True(plugin.SetQuestLogEntry(QuestKey, 10, 0, 2, "Updated", note: "New note"));
        var structure = QuestStructure.Read(plugin, QuestKey);
        var log = Assert.Single(structure.Stages[0].Logs);
        Assert.Equal((byte)2, log.Flags);
        Assert.Equal(Encoding.ASCII.GetBytes("Updated"), log.Text!.InlineBytes);
        Assert.Equal(Encoding.ASCII.GetBytes("New note"), log.NoteBytes);
        var conditions = Assert.Single(structure.ConditionLists, item =>
            item.Scope == QuestScope.LogEntry && item.Primary == 0 && item.Secondary == 0);
        Assert.Single(conditions.Conditions);
        Assert.False(plugin.SetQuestLogEntry(QuestKey, 10, 1, 0));
        Assert.Throws<RadicleException>(() => plugin.SetQuestLogEntry(QuestKey, 10, 0, 0, textStringId: 5));
        structure = QuestStructure.Read(plugin, QuestKey);
        log = Assert.Single(structure.Stages[0].Logs);
        Assert.Equal((byte)2, log.Flags);
        Assert.Equal(Encoding.ASCII.GetBytes("Updated"), log.Text!.InlineBytes);
        Assert.Equal(Encoding.ASCII.GetBytes("New note"), log.NoteBytes);
    }

    [Fact]
    public void LogEntryAppendKeepsExistingLogAndItsConditions()
    {
        using var temp = new TempDirectory();
        using var plugin = PluginFile.Read(temp.Write("Quests.esp", Fixture()));
        Assert.True(plugin.AddQuestLogEntry(QuestKey, 10, 3, "Appended", note: "New note"));
        var structure = QuestStructure.Read(plugin, QuestKey);
        Assert.Equal(2, structure.Stages[0].Logs.Count);
        Assert.Equal((byte)1, structure.Stages[0].Logs[0].Flags);
        Assert.Equal(new byte[] { 0xe9 }, structure.Stages[0].Logs[0].Text!.InlineBytes);
        var appended = structure.Stages[0].Logs[1];
        Assert.Equal((byte)3, appended.Flags);
        Assert.Equal(Encoding.ASCII.GetBytes("Appended"), appended.Text!.InlineBytes);
        Assert.Equal(Encoding.ASCII.GetBytes("New note"), appended.NoteBytes);
        var existingConditions = Assert.Single(structure.ConditionLists, item =>
            item.Scope == QuestScope.LogEntry && item.Primary == 0 && item.Secondary == 0);
        Assert.Single(existingConditions.Conditions);
        var appendedConditions = Assert.Single(structure.ConditionLists, item =>
            item.Scope == QuestScope.LogEntry && item.Primary == 0 && item.Secondary == 1);
        Assert.Empty(appendedConditions.Conditions);
        Assert.False(plugin.AddQuestLogEntry(QuestKey, 99, 0));
        Assert.Throws<RadicleException>(() => plugin.AddQuestLogEntry(QuestKey, 10, 0, textStringId: 5));
        Assert.Equal(2, QuestStructure.Read(plugin, QuestKey).Stages[0].Logs.Count);
    }

    // Constraint: objective writes keep targets, stages and aliases and refuse the wrong text kind.
    [Fact]
    public void ObjectiveWritesKeepTheRestAndCheckTextKind()
    {
        using var temp = new TempDirectory();
        using var plugin = PluginFile.Read(temp.Write("Quests.esp", Fixture()));
        Assert.True(plugin.AddQuestObjective(QuestKey, 40, 1, "Find it"));
        Assert.Throws<RadicleException>(() => plugin.AddQuestObjective(QuestKey, 41, 0, textStringId: 5));
        Assert.Throws<ArgumentException>(() => plugin.AddQuestObjective(QuestKey, 42, 0, "x", 5));
        Assert.True(plugin.SetQuestObjective(QuestKey, 30, 3));
        Assert.True(plugin.AddQuestObjectiveTarget(QuestKey, 30, 4, 9));
        Assert.True(plugin.AddQuestObjectiveTarget(QuestKey, 30, 5, 1, QuestKey));
        Assert.True(plugin.SetQuestObjectiveTarget(QuestKey, 30, 1, -2, 7, QuestKey));
        Assert.False(plugin.AddQuestObjectiveTarget(QuestKey, 99, 1, 0));
        var value = QuestStructure.Read(plugin, QuestKey);
        Assert.Equal(new ushort[] { 30, 40 }, value.Objectives.Select(objective => objective.Index));
        Assert.Equal(Encoding.ASCII.GetBytes("Find it"), value.Objectives[1].Text!.InlineBytes);
        Assert.Equal(3u, value.Objectives[0].Flags);
        Assert.Equal(new[] { -1, -2, 5 }, value.Objectives[0].Targets.Select(target => target.Alias));
        Assert.Equal(QuestKey, value.Objectives[0].Targets[1].Keyword);
        Assert.Equal(QuestKey, value.Objectives[0].Targets[2].Keyword);
        Assert.Single(value.Stages);
        Assert.Equal(2, value.Aliases.Count);
        Assert.True(plugin.RemoveQuestObjectiveTarget(QuestKey, 30, 1));
        Assert.False(plugin.RemoveQuestObjectiveTarget(QuestKey, 30, 9));
        Assert.True(plugin.RemoveQuestObjective(QuestKey, 40));
        Assert.False(plugin.RemoveQuestObjective(QuestKey, 40));
        Assert.Equal(2, QuestStructure.Read(plugin, QuestKey).Objectives[0].Targets.Count);
    }

    // Constraint: alias replace keeps members the edit does not carry, such as the alias conditions.
    [Fact]
    public void AliasWritesAddReplaceAndRemoveById()
    {
        using var temp = new TempDirectory();
        using var plugin = PluginFile.Read(temp.Write("Quests.esp", Fixture()));
        Assert.True(plugin.AddQuestAlias(QuestKey, new QuestAliasEdit(QuestAliasKind.Collection, 0)));
        Assert.Equal(3, QuestStructure.Read(plugin, QuestKey).Aliases.Count);
        var added = new QuestAliasEdit(QuestAliasKind.Reference, 5)
        {
            Name = "Added", Flags = 3, FillType = QuestFillType.SpecificReference, Target = QuestKey
        };
        Assert.True(plugin.AddQuestAlias(QuestKey, added));
        Assert.Throws<RadicleException>(() => plugin.AddQuestAlias(QuestKey, added));
        var value = QuestStructure.Read(plugin, QuestKey);
        var alias = value.Aliases.Single(item => item.Id == 5u);
        Assert.Equal(Encoding.ASCII.GetBytes("Added"), alias.NameBytes);
        Assert.Equal(3u, alias.Flags);
        Assert.Equal(QuestFillType.SpecificReference, alias.Fill.Type);
        Assert.Equal(QuestKey, alias.Fill.Target);
        Assert.Throws<RadicleException>(() => plugin.AddQuestAlias(QuestKey,
            new QuestAliasEdit(QuestAliasKind.Reference, 6) { FillType = QuestFillType.CreateReference }));

        var replacement = new QuestAliasEdit(QuestAliasKind.Reference, 1)
        {
            Name = "renamed", Flags = 7, FillType = QuestFillType.UniqueActor, Target = QuestKey
        };
        Assert.True(plugin.ReplaceQuestAlias(QuestKey, 1, replacement));
        Assert.False(plugin.ReplaceQuestAlias(QuestKey, 9, replacement with { Id = 9 }));
        Assert.Throws<RadicleException>(() => plugin.ReplaceQuestAlias(QuestKey, 1, replacement with { Id = 2 }));
        value = QuestStructure.Read(plugin, QuestKey);
        Assert.Equal(Encoding.ASCII.GetBytes("renamed"), value.Aliases[0].NameBytes);
        Assert.Equal(QuestFillType.UniqueActor, value.Aliases[0].Fill.Type);
        var kept = value.ConditionLists.Single(list => list.Scope == QuestScope.Alias && list.Primary == 0);
        Assert.Equal((ushort)74, Assert.Single(kept.Conditions).Function);
        Assert.True(plugin.RemoveQuestAlias(QuestKey, 5));
        Assert.False(plugin.RemoveQuestAlias(QuestKey, 5));
        Assert.Equal(3, QuestStructure.Read(plugin, QuestKey).Aliases.Count);
    }

    // Constraint: inline objective text and alias names are stored as Windows-1252, so Café is 43 61 66 E9.
    [Fact]
    public void InlineTextIsStoredAsWindows1252()
    {
        using var temp = new TempDirectory();
        using var plugin = PluginFile.Read(temp.Write("Quests.esp", Fixture()));
        var stored = new byte[] { 0x43, 0x61, 0x66, 0xE9 };
        Assert.True(plugin.AddQuestObjective(QuestKey, 40, 0, "Café"));
        Assert.True(plugin.AddQuestAlias(QuestKey, new QuestAliasEdit(QuestAliasKind.Reference, 5) { Name = "Café" }));
        Assert.Equal(Status.InvalidArgument, Assert.Throws<RadicleException>(() =>
            plugin.AddQuestObjective(QuestKey, 41, 0, "漢")).Status);
        Assert.Equal(Status.InvalidArgument, Assert.Throws<RadicleException>(() =>
            plugin.AddQuestAlias(QuestKey, new QuestAliasEdit(QuestAliasKind.Reference, 6) { Name = "漢" })).Status);
        var value = QuestStructure.Read(plugin, QuestKey);
        Assert.Equal(stored, value.Objectives[1].Text!.InlineBytes);
        Assert.Equal(stored, value.Aliases.Single(item => item.Id == 5u).NameBytes);
    }

    // Constraint: reference alias flags are always written, zero included, and replace without a fill keeps the existing fill.
    [Fact]
    public void AliasFlagsAreWrittenAndReplaceKeepsTheFill()
    {
        using var temp = new TempDirectory();
        using var plugin = PluginFile.Read(temp.Write("Quests.esp", Fixture()));
        Assert.True(plugin.AddQuestAlias(QuestKey, new QuestAliasEdit(QuestAliasKind.Reference, 5) { Name = "Five" }));
        Assert.Equal(0u, QuestStructure.Read(plugin, QuestKey).Aliases.Single(item => item.Id == 5u).Flags);
        Assert.True(plugin.ReplaceQuestAlias(QuestKey, 1, new QuestAliasEdit(QuestAliasKind.Reference, 1) { Name = "ref", Flags = 7 }));
        var kept = QuestStructure.Read(plugin, QuestKey).Aliases[0];
        Assert.Equal(7u, kept.Flags);
        Assert.Equal(QuestFillType.SpecificReference, kept.Fill.Type);
        Assert.Equal(QuestKey, kept.Fill.Target);
    }

    [Fact]
    public void ReorderWritesMoveMembersAndReportInvalidRequests()
    {
        using var temp = new TempDirectory();
        using var plugin = PluginFile.Read(temp.Write("Quests.esp", Fixture()));

        Assert.True(plugin.AddQuestStage(QuestKey, 20, 0));
        Assert.True(plugin.ReorderQuestStage(QuestKey, 20, 0));
        Assert.Equal(new ushort[] { 20, 10 }, QuestStructure.Read(plugin, QuestKey).Stages.Select(stage => stage.Index));
        Assert.False(plugin.ReorderQuestStage(QuestKey, 99, 0));
        Assert.Equal(Status.InvalidArgument, Assert.Throws<RadicleException>(() =>
            plugin.ReorderQuestStage(QuestKey, 20, 2)).Status);

        Assert.True(plugin.AddQuestObjective(QuestKey, 40, 0));
        Assert.True(plugin.ReorderQuestObjective(QuestKey, 40, 0));
        Assert.Equal(new ushort[] { 40, 30 }, QuestStructure.Read(plugin, QuestKey).Objectives.Select(objective => objective.Index));
        Assert.False(plugin.ReorderQuestObjective(QuestKey, 99, 0));
        Assert.Equal(Status.InvalidArgument, Assert.Throws<RadicleException>(() =>
            plugin.ReorderQuestObjective(QuestKey, 40, 2)).Status);

        Assert.True(plugin.AddQuestAlias(QuestKey, new QuestAliasEdit(QuestAliasKind.Reference, 5) { Name = "Moved", Flags = 3 }));
        Assert.True(plugin.ReorderQuestAlias(QuestKey, 5, 0));
        Assert.Equal(new uint?[] { 5, 1, null }, QuestStructure.Read(plugin, QuestKey).Aliases.Select(alias => alias.Id));
        Assert.False(plugin.ReorderQuestAlias(QuestKey, 99, 0));
        Assert.Equal(Status.InvalidArgument, Assert.Throws<RadicleException>(() =>
            plugin.ReorderQuestAlias(QuestKey, 5, 3)).Status);
    }
}
