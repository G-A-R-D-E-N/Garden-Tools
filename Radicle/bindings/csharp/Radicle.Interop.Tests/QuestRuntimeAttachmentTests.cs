using System;
using System.Collections.Generic;
using Radicle.Interop;
using Xunit;

namespace Radicle.Interop.Tests;

/// <summary>
/// Regression tests for the row-to-model mapping, covering the attachment rows the decoder used to drop.
/// </summary>
public class QuestRuntimeAttachmentTests
{
    private static readonly FormKey Quest = new("Quests.esp", 0x000800);

    [Fact]
    public void AZeroPropertyScriptIsPreserved()
    {
        var rows = new[]
        {
            "quest\t000800:Quests.esp\t1",
            "script\tQF_Empty",
        };

        var data = QuestRuntime.DecodeRows(rows, Quest);

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
            "alias-script-property\t0\tQF_Alias\tTarget\t1",
            "alias\t1",
            "alias-script\t1\tQF_Alias",
            "alias-script-property\t1\tQF_Alias\tTarget\t1",
        };

        var data = QuestRuntime.DecodeRows(rows, Quest);

        Assert.Equal(new[] { 0, 1 }, data.Aliases);
        Assert.Equal(2, data.AliasScripts.Count);
        Assert.Contains(new QuestRuntimeScript(0, "QF_Alias"), data.AliasScripts);
        Assert.Contains(new QuestRuntimeScript(1, "QF_Alias"), data.AliasScripts);
        Assert.Equal(2, data.AliasScriptProperties.Count);
        Assert.Contains(new QuestRuntimeAliasScriptProperty(0, "QF_Alias", "Target", 1), data.AliasScriptProperties);
        Assert.Contains(new QuestRuntimeAliasScriptProperty(1, "QF_Alias", "Target", 1), data.AliasScriptProperties);
    }

    [Fact]
    public void AliasScriptPropertiesKeepTheirAlias()
    {
        var rows = new[]
        {
            "alias-script-property\t7\tQF_Alias\tCount\t3",
        };

        var property = Assert.Single(QuestRuntime.DecodeRows(rows, Quest).AliasScriptProperties);

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
            "script-property\tQF_TestQuest\tCount\t3",
            "fragment-script\tQF_TestQuest",
            "fragment-property\tStage\t3",
            "fragment\t10\t0\tQF_TestQuest\tFragment_Stage_0010",
            "alias\t0",
            "alias-script\t0\tQF_TestQuest_Alias",
            "alias-script-property\t0\tQF_TestQuest_Alias\tTarget\t1",
            "unsupported\talias fill",
        };

        var data = QuestRuntime.DecodeRows(rows, Quest);

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
        var rows = new[]
        {
            "script\tQF_A",
            "script-property\tQF_A\tCount\t3",
            "script\tQF_B",
            "script-property\tQF_B\tCount\t3",
        };

        var data = QuestRuntime.DecodeRows(rows, Quest);

        Assert.Equal(2, data.Scripts.Count);
        Assert.Contains(new QuestRuntimeProperty("QF_A", "Count", 3), data.ScriptProperties);
        Assert.Contains(new QuestRuntimeProperty("QF_B", "Count", 3), data.ScriptProperties);
    }
}
