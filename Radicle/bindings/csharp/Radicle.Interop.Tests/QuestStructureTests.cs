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
}
