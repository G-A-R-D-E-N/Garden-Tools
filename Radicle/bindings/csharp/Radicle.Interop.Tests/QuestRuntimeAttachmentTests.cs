using System;
using System.Text;
using Radicle.Interop;
using Radicle.Interop.Native;
using Xunit;

namespace Radicle.Interop.Tests;

public class QuestRuntimeAttachmentTests
{
    private static readonly FormKey Quest = new("Quests.esp", 0x000800);

    private static QuestRuntimePropertyEntry ScriptProperty(string script, string name, byte type, int? value = null) =>
        new(QuestPropertyOwner.Script, -1, script, name, type, 0,
            new QuestRuntimeValue { Integer = value });

    private static QuestRuntimePropertyEntry AliasScriptProperty(int alias, string script, string name, byte type) =>
        new(QuestPropertyOwner.AliasScript, alias, script, name, type, 0, default);

    [Fact]
    public void AZeroPropertyScriptIsPreserved()
    {
        var rows = new[]
        {
            "quest\t000800:Quests.esp\t1",
            "script\tQF_Empty",
        };

        var data = QuestRuntime.DecodeRows(rows, Array.Empty<QuestRuntimePropertyEntry>(), Quest);

        var script = Assert.Single(data.Scripts);
        Assert.Equal(QuestRuntime.QuestOwnedAlias, script.Alias);
        Assert.Equal("QF_Empty", script.Script);
        Assert.Empty(data.ScriptProperties);
    }

    [Fact]
    public void IdenticalScriptsOnDifferentAliasesStaySeparate()
    {
        var rows = new[]
        {
            "alias\t0",
            "alias-script\t0\tQF_Alias",
            "alias\t1",
            "alias-script\t1\tQF_Alias",
        };
        var properties = new[]
        {
            AliasScriptProperty(0, "QF_Alias", "Target", 1),
            AliasScriptProperty(1, "QF_Alias", "Target", 1),
        };

        var data = QuestRuntime.DecodeRows(rows, properties, Quest);

        Assert.Equal(new[] { 0, 1 }, data.Aliases);
        Assert.Equal(2, data.AliasScripts.Count);
        Assert.Contains(new QuestRuntimeScript(0, "QF_Alias"), data.AliasScripts);
        Assert.Contains(new QuestRuntimeScript(1, "QF_Alias"), data.AliasScripts);
        Assert.Equal(2, data.AliasScriptProperties.Count);
        Assert.Equal(0, data.AliasScriptProperties[0].Alias);
        Assert.Equal(1, data.AliasScriptProperties[1].Alias);
    }

    [Fact]
    public void AliasScriptPropertiesKeepTheirAlias()
    {
        var properties = new[] { AliasScriptProperty(7, "QF_Alias", "Count", 3) };

        var property = Assert.Single(
            QuestRuntime.DecodeRows(Array.Empty<string>(), properties, Quest).AliasScriptProperties);

        Assert.Equal(7, property.Alias);
        Assert.Equal("QF_Alias", property.Script);
        Assert.Equal("Count", property.Name);
        Assert.Equal(3, property.Type);
    }

    [Fact]
    public void EveryEmittedRowKindIsRepresented()
    {
        var rows = new[]
        {
            "quest\t000800:Quests.esp\t1",
            "script\tQF_TestQuest",
            "fragment-script\tQF_TestQuest",
            "fragment\t10\t0\tQF_TestQuest\tFragment_Stage_0010",
            "alias\t0",
            "alias-script\t0\tQF_TestQuest_Alias",
            "unsupported\talias fill",
        };
        var properties = new[]
        {
            ScriptProperty("QF_TestQuest", "Count", 3, 7),
            new QuestRuntimePropertyEntry(QuestPropertyOwner.Fragment, -1, string.Empty, "Stage", 3, 0, default),
            AliasScriptProperty(0, "QF_TestQuest_Alias", "Target", 1),
        };

        var data = QuestRuntime.DecodeRows(rows, properties, Quest);

        Assert.True(data.StartGameEnabled);
        Assert.Single(data.Scripts);
        Assert.Single(data.ScriptProperties);
        Assert.Equal("QF_TestQuest", data.FragmentScript);
        Assert.Single(data.FragmentProperties);
        Assert.Single(data.Fragments);
        Assert.Single(data.Aliases);
        Assert.Single(data.AliasScripts);
        Assert.Single(data.AliasScriptProperties);
        Assert.Single(data.Unsupported);
    }

    [Fact]
    public void ScriptPropertiesKeepTheirOwningScript()
    {
        var properties = new[]
        {
            ScriptProperty("QF_A", "Count", 3, 7),
            ScriptProperty("QF_B", "Count", 3, 7),
        };

        var data = QuestRuntime.DecodeRows(Array.Empty<string>(), properties, Quest);

        Assert.Equal(2, data.ScriptProperties.Count);
        Assert.Contains(new QuestRuntimeProperty("QF_A", "Count", 3, 0, new QuestRuntimeValue { Integer = 7 }),
            data.ScriptProperties);
        Assert.Contains(new QuestRuntimeProperty("QF_B", "Count", 3, 0, new QuestRuntimeValue { Integer = 7 }),
            data.ScriptProperties);
    }

    [Fact]
    public void FragmentPropertiesKeepTheirOwnScriptField()
    {
        var rows = new[] { "fragment-script\tQF_TestQuest" };
        var emptyScript = new[]
        {
            new QuestRuntimePropertyEntry(QuestPropertyOwner.Fragment, -1, string.Empty, "Stage", 3, 0,
                new QuestRuntimeValue { Integer = 10 }),
        };
        var namedScript = new[]
        {
            new QuestRuntimePropertyEntry(QuestPropertyOwner.Fragment, -1, "QF_Other", "Stage", 3, 0,
                new QuestRuntimeValue { Integer = 10 }),
        };

        var data = QuestRuntime.DecodeRows(rows, emptyScript, Quest);

        Assert.Equal("QF_TestQuest", data.FragmentScript);
        var property = Assert.Single(data.FragmentProperties);
        Assert.Equal(string.Empty, property.Script);
        Assert.Equal(10, property.Value.Integer);

        var other = QuestRuntime.DecodeRows(rows, namedScript, Quest);

        Assert.Equal("QF_TestQuest", other.FragmentScript);
        Assert.Equal("QF_Other", Assert.Single(other.FragmentProperties).Script);
    }

    [Fact]
    public void EmbeddedNulBytesSurviveTheRowMapping()
    {
        var textBytes = new byte[] { 0x47, 0x61, 0x72, 0x64, 0x65, 0x6E, 0x00, 0x50, 0x61, 0x74, 0x63, 0x68 };
        var properties = new[]
        {
            new QuestRuntimePropertyEntry(QuestPropertyOwner.Script, -1, "QF_A", "Embedded", 2, 0,
                new QuestRuntimeValue { Text = Encoding.Latin1.GetString(textBytes) }),
            new QuestRuntimePropertyEntry(QuestPropertyOwner.Script, -1, "QF_A", "Empty", 2, 0,
                new QuestRuntimeValue { Text = string.Empty }),
            new QuestRuntimePropertyEntry(QuestPropertyOwner.Script, -1, "QF_A", "Count", 3, 0,
                new QuestRuntimeValue { Integer = 7 }),
        };

        var data = QuestRuntime.DecodeRows(Array.Empty<string>(), properties, Quest);

        Assert.Equal(3, data.ScriptProperties.Count);
        Assert.True(textBytes.AsSpan().SequenceEqual(data.ScriptProperties[0].Value.TextBytes!));
        Assert.Equal("Garden\0Patch", data.ScriptProperties[0].Value.Text);
        Assert.Equal(12, data.ScriptProperties[0].Value.Text!.Length);
        Assert.NotNull(data.ScriptProperties[1].Value.TextBytes);
        Assert.Empty(data.ScriptProperties[1].Value.TextBytes!);
        Assert.Equal(string.Empty, data.ScriptProperties[1].Value.Text);
        Assert.Null(data.ScriptProperties[2].Value.Text);
        Assert.Null(data.ScriptProperties[2].Value.TextBytes);
    }

    [Fact]
    public void Utf8AndInvalidBytesSurviveTheRowMapping()
    {
        var textBytes = new byte[] { 0x43, 0x61, 0x66, 0xC3, 0xA9, 0xFF };
        var properties = new[]
        {
            new QuestRuntimePropertyEntry(QuestPropertyOwner.Script, -1, "QF_A", "Label", 2, 0,
                new QuestRuntimeValue { Text = Encoding.Latin1.GetString(textBytes) }),
        };

        var data = QuestRuntime.DecodeRows(Array.Empty<string>(), properties, Quest);
        var value = Assert.Single(data.ScriptProperties).Value;

        Assert.True(textBytes.AsSpan().SequenceEqual(value.TextBytes!));
        Assert.Equal("Café", Encoding.UTF8.GetString(value.TextBytes!, 0, textBytes.Length - 1));
    }

    [Fact]
    public void ValuesSurviveTheRowMapping()
    {
        var properties = new[]
        {
            new QuestRuntimePropertyEntry(QuestPropertyOwner.Script, -1, "QF_A", "Ratio", 4, 1,
                new QuestRuntimeValue { Real = 1.5f }),
            new QuestRuntimePropertyEntry(QuestPropertyOwner.Script, -1, "QF_A", "Enabled", 5, 0,
                new QuestRuntimeValue { Boolean = true }),
            new QuestRuntimePropertyEntry(QuestPropertyOwner.Script, -1, "QF_A", "Label", 2, 0,
                new QuestRuntimeValue { Text = "Garden\tPatch" }),
            new QuestRuntimePropertyEntry(QuestPropertyOwner.Script, -1, "QF_A", "Target", 1, 0,
                new QuestRuntimeValue { ObjectFormId = 0x00012345, Alias = 5 }),
            new QuestRuntimePropertyEntry(QuestPropertyOwner.Script, -1, "QF_A", "Stages", 13, 0,
                new QuestRuntimeValue { ElementCount = 3 }),
        };

        var data = QuestRuntime.DecodeRows(Array.Empty<string>(), properties, Quest);

        Assert.Equal(5, data.ScriptProperties.Count);
        Assert.Equal(1.5f, data.ScriptProperties[0].Value.Real);
        Assert.Equal(1, data.ScriptProperties[0].Flags);
        Assert.True(data.ScriptProperties[1].Value.Boolean);
        Assert.Equal("Garden\tPatch", data.ScriptProperties[2].Value.Text);
        Assert.Equal(0x00012345u, data.ScriptProperties[3].Value.ObjectFormId);
        Assert.Equal((short)5, data.ScriptProperties[3].Value.Alias);
        Assert.Equal(3u, data.ScriptProperties[4].Value.ElementCount);
    }
}
