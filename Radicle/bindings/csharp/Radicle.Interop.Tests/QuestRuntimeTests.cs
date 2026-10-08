using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using Xunit;

namespace Radicle.Interop.Tests;

public class QuestRuntimeTests
{
    private static byte[] MakeQuestPlugin(ushort dnamFlags, int count)
    {
        var bytes = new List<byte>();
        void U8(byte value) => bytes.Add(value);
        void U16(ushort value)
        {
            U8((byte)(value & 0xFF));
            U8((byte)(value >> 8));
        }
        void U32(uint value)
        {
            U16((ushort)(value & 0xFFFF));
            U16((ushort)(value >> 16));
        }
        void Sig(string text)
        {
            for (var i = 0; i < 4; ++i) U8(i < text.Length ? (byte)text[i] : (byte)0);
        }
        void Sub(string signature, IReadOnlyList<byte> data)
        {
            Sig(signature);
            U16((ushort)data.Count);
            bytes.AddRange(data);
        }

        var tes4 = bytes.Count;
        Sig("TES4");
        U32(0);
        U32(0);
        U32(0);
        U16(0);
        U16(0);
        U16(131);
        U16(0);
        var hedr = new List<byte>();
        hedr.AddRange(BitConverter.GetBytes(1.0f));
        hedr.AddRange(BitConverter.GetBytes(2u));
        hedr.AddRange(BitConverter.GetBytes(0x900u));
        Sub("HEDR", hedr);
        PatchU32(bytes, tes4 + 4, (uint)(bytes.Count - (tes4 + 24)));

        var grup = bytes.Count;
        Sig("GRUP");
        U32(0);
        Sig("QUST");
        U32(0);
        U16(0);
        U16(0);
        U16(0);
        U16(0);

        var rec = bytes.Count;
        Sig("QUST");
        U32(0);
        U32(0);
        U32(0x00000800);
        U16(0);
        U16(0);
        U16(131);
        U16(0);
        Sub("EDID", Encoding.ASCII.GetBytes("TestQuest\0"));
        var dnam = new List<byte>();
        dnam.AddRange(BitConverter.GetBytes(dnamFlags));
        dnam.Add(0);
        dnam.Add(0);
        dnam.AddRange(BitConverter.GetBytes(0u));
        dnam.AddRange(BitConverter.GetBytes(0u));
        Sub("DNAM", dnam);
        Sub("VMAD", VmadBytes(count));
        PatchU32(bytes, rec + 4, (uint)(bytes.Count - (rec + 24)));

        PatchU32(bytes, grup + 4, (uint)(bytes.Count - grup));
        return bytes.ToArray();
    }

    private static List<byte> VmadBytes(int count)
    {
        var bytes = new List<byte>();
        void U8(byte value) => bytes.Add(value);
        void U16(ushort value)
        {
            U8((byte)(value & 0xFF));
            U8((byte)(value >> 8));
        }
        void U32(uint value)
        {
            U16((ushort)(value & 0xFFFF));
            U16((ushort)(value >> 16));
        }
        void Wstring(string text)
        {
            U16((ushort)text.Length);
            foreach (var character in text) U8((byte)character);
        }

        U16(5);
        U16(2);
        U16(1);
        Wstring("QuestScript");
        U8(0);
        U16(7);
        Wstring("Count");
        U8(3);
        U8(0);
        U32((uint)count);
        Wstring("Ratio");
        U8(4);
        U8(1);
        U32(BitConverter.ToUInt32(BitConverter.GetBytes(1.5f), 0));
        Wstring("Enabled");
        U8(5);
        U8(0);
        U8(1);
        Wstring("Label");
        U8(2);
        U8(0);
        Wstring("Garden\tPatch");
        Wstring("Stages");
        U8(13);
        U8(0);
        U32(3);
        U32(1);
        U32(2);
        U32(3);
        Wstring("Embedded");
        U8(2);
        U8(0);
        Wstring("Garden\0Patch");
        Wstring("Empty");
        U8(2);
        U8(0);
        Wstring("");

        U8(0);
        U16(1);
        Wstring("QF_TestQuest");
        U8(0);
        U16(1);
        Wstring("Stage");
        U8(3);
        U8(0);
        U32(10);

        U16(10);
        U16(0);
        U32(0);
        U8(0);
        Wstring("QF_TestQuest");
        Wstring("Fragment_Stage_0010");

        U16(1);
        U16(0);
        U16(5);
        U32(0);
        U16(0);
        U16(2);
        U16(1);
        Wstring("AliasScript");
        U8(0);
        U16(1);
        Wstring("Target");
        U8(1);
        U8(0);
        U16(0);
        U16(5);
        U32(0x00012345);
        return bytes;
    }

    private static void PatchU32(List<byte> bytes, int offset, uint value)
    {
        bytes[offset] = (byte)(value & 0xFF);
        bytes[offset + 1] = (byte)((value >> 8) & 0xFF);
        bytes[offset + 2] = (byte)((value >> 16) & 0xFF);
        bytes[offset + 3] = (byte)((value >> 24) & 0xFF);
    }

    private static PluginFile LoadQuestPlugin(ushort dnamFlags, string root, int count = 7)
    {
        Directory.CreateDirectory(root);
        var path = Path.Combine(root, "Quests.esp");
        File.WriteAllBytes(path, MakeQuestPlugin(dnamFlags, count));
        return PluginFile.Read(path);
    }

    private static QuestRuntimeProperty Property(QuestRuntimeData data, string name)
    {
        foreach (var property in data.ScriptProperties)
            if (property.Name == name) return property;
        foreach (var property in data.FragmentProperties)
            if (property.Name == name) return property;
        throw new InvalidOperationException("no property named " + name);
    }

    private static QuestRuntimeData DecodeCount(int count, string root)
    {
        using var plugin = LoadQuestPlugin(1, root, count);
        return QuestRuntime.Decode(plugin, new FormKey("Quests.esp", 0x000800));
    }

    [Fact]
    public void DecodesScriptsFragmentsAndAliasScripts()
    {
        using var temp = new TempDirectory();
        var root = temp.Path;
        using var plugin = LoadQuestPlugin(1, root);
        var data = QuestRuntime.Decode(plugin, new FormKey("Quests.esp", 0x000800));

        Assert.True(data.StartGameEnabled);
        Assert.Empty(data.Unsupported);
        Assert.Contains(new QuestRuntimeScript(QuestRuntime.QuestOwnedAlias, "QuestScript"), data.Scripts);
        Assert.Equal(7, data.ScriptProperties.Count);
        Assert.Equal("QF_TestQuest", data.FragmentScript);
        Assert.Single(data.FragmentProperties);
        Assert.Contains(new QuestRuntimeFragment(10, 0, "QF_TestQuest", "Fragment_Stage_0010"), data.Fragments);
        Assert.Contains(5, data.Aliases);
        Assert.Contains(new QuestRuntimeScript(5, "AliasScript"), data.AliasScripts);
        Assert.Single(data.AliasScriptProperties);
    }

    [Fact]
    public void EveryValueKindSurvives()
    {
        using var temp = new TempDirectory();
        var root = temp.Path;
        using var plugin = LoadQuestPlugin(1, root);
        var data = QuestRuntime.Decode(plugin, new FormKey("Quests.esp", 0x000800));

        var integer = Property(data, "Count");
        Assert.Equal("QuestScript", integer.Script);
        Assert.Equal(3, integer.Type);
        Assert.Equal(0, integer.Flags);
        Assert.Equal(7, integer.Value.Integer);
        Assert.Null(integer.Value.Real);

        var real = Property(data, "Ratio");
        Assert.Equal(1, real.Flags);
        Assert.Equal(1.5f, real.Value.Real);
        Assert.Null(real.Value.Integer);

        var boolean = Property(data, "Enabled");
        Assert.True(boolean.Value.Boolean);

        var text = Property(data, "Label");
        Assert.Equal("Garden\tPatch", text.Value.Text);

        var embedded = Property(data, "Embedded");
        var expectedBytes = new byte[] { 0x47, 0x61, 0x72, 0x64, 0x65, 0x6E, 0x00, 0x50, 0x61, 0x74, 0x63, 0x68 };
        Assert.NotNull(embedded.Value.TextBytes);
        Assert.True(expectedBytes.AsSpan().SequenceEqual(embedded.Value.TextBytes));
        Assert.Equal("Garden\0Patch", embedded.Value.Text);
        Assert.Equal(12, embedded.Value.Text!.Length);

        var empty = Property(data, "Empty");
        Assert.NotNull(empty.Value.TextBytes);
        Assert.Empty(empty.Value.TextBytes!);
        Assert.Equal(string.Empty, empty.Value.Text);

        var noString = Property(data, "Count");
        Assert.Null(noString.Value.Text);
        Assert.Null(noString.Value.TextBytes);

        var elements = Property(data, "Stages");
        Assert.Equal(3u, elements.Value.ElementCount);

        var fragment = Property(data, "Stage");
        Assert.Equal(string.Empty, fragment.Script);
        Assert.Equal(10, fragment.Value.Integer);
        Assert.Equal("QF_TestQuest", data.FragmentScript);

        var alias = Assert.Single(data.AliasScriptProperties);
        Assert.Equal(5, alias.Alias);
        Assert.Equal("AliasScript", alias.Script);
        Assert.Equal("Target", alias.Name);
        Assert.Equal(1, alias.Type);
        Assert.Equal(0x00012345u, alias.Value.ObjectFormId);
        Assert.Equal((short)5, alias.Value.Alias);
    }

    [Fact]
    public void StartGameFlagComesFromDnam()
    {
        using var temp = new TempDirectory();
        var root = temp.Path;
        using var plugin = LoadQuestPlugin(0, root);
        var data = QuestRuntime.Decode(plugin, new FormKey("Quests.esp", 0x000800));

        Assert.False(data.StartGameEnabled);
        Assert.Empty(data.Unsupported);
    }

    [Fact]
    public void MissingQuestIsReportedAsUnsupported()
    {
        using var temp = new TempDirectory();
        var root = temp.Path;
        using var plugin = LoadQuestPlugin(1, root);
        var data = QuestRuntime.Decode(plugin, new FormKey("Quests.esp", 0x000900));

        Assert.NotEmpty(data.Unsupported);
        Assert.Empty(data.Fragments);
        Assert.Empty(data.ScriptProperties);
    }

    [Fact]
    public void RowsAreTabSeparatedAndReleasable()
    {
        using var temp = new TempDirectory();
        var root = temp.Path;
        using var plugin = LoadQuestPlugin(1, root);
        var rows = QuestRuntime.Rows(plugin, new FormKey("Quests.esp", 0x000800));

        Assert.Equal(6, rows.Length);
        Assert.Contains("quest\t000800:Quests.esp\t1", rows);
        Assert.Contains("fragment\t10\t0\tQF_TestQuest\tFragment_Stage_0010", rows);
        Assert.DoesNotContain(rows, row => row.Contains("property", StringComparison.Ordinal));
    }

    [Fact]
    public void TheRowApiCarriesNoPropertyValues()
    {
        using var temp = new TempDirectory();
        var root = temp.Path;
        using var seven = LoadQuestPlugin(1, Path.Combine(root, "seven"), 7);
        using var ninetyNine = LoadQuestPlugin(1, Path.Combine(root, "ninety_nine"), 99);

        Assert.Equal(
            QuestRuntime.Rows(seven, new FormKey("Quests.esp", 0x000800)),
            QuestRuntime.Rows(ninetyNine, new FormKey("Quests.esp", 0x000800)));
    }

    [Fact]
    public void IntegerPropertyValuesAreDistinguishable()
    {
        using var temp = new TempDirectory();
        var root = temp.Path;
        var seven = DecodeCount(7, Path.Combine(root, "seven"));
        var ninetyNine = DecodeCount(99, Path.Combine(root, "ninety_nine"));

        Assert.Equal(7, Property(seven, "Count").Value.Integer);
        Assert.Equal(99, Property(ninetyNine, "Count").Value.Integer);
        Assert.NotEqual(seven.ScriptProperties, ninetyNine.ScriptProperties);
    }
}
