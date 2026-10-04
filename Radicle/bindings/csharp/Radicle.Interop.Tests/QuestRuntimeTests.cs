using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using Xunit;

namespace Radicle.Interop.Tests;

public class QuestRuntimeTests
{
    // A synthetic plugin with one QUST record carrying DNAM flags and a VMAD whose extras hold a
    // stage fragment and an alias script. Built here so nothing comes from a shipped or
    // third-party file.
    private static byte[] MakeQuestPlugin(ushort dnamFlags)
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
        Sub("VMAD", VmadBytes());
        PatchU32(bytes, rec + 4, (uint)(bytes.Count - (rec + 24)));

        PatchU32(bytes, grup + 4, (uint)(bytes.Count - grup));
        return bytes.ToArray();
    }

    private static List<byte> VmadBytes()
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

        // The leading script list: version 5, object format 2, one script with one property.
        U16(5);
        U16(2);
        U16(1);
        Wstring("QuestScript");
        U8(0);
        U16(1);
        Wstring("Count");
        U8(3);
        U8(0);
        U32(7);

        // The quest extras: fragment script, one stage fragment, one alias script.
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

    private static PluginFile LoadQuestPlugin(ushort dnamFlags, string root)
    {
        Directory.CreateDirectory(root);
        var path = Path.Combine(root, "Quests.esp");
        File.WriteAllBytes(path, MakeQuestPlugin(dnamFlags));
        return PluginFile.Read(path);
    }

    [Fact]
    public void DecodesScriptsFragmentsAndAliasScripts()
    {
        var root = Path.Combine(Path.GetTempPath(), "radicle_quest_runtime_test");
        try
        {
            using var plugin = LoadQuestPlugin(1, root);
            var data = QuestRuntime.Decode(plugin, new FormKey("Quests.esp", 0x000800));

            Assert.True(data.StartGameEnabled);
            Assert.Empty(data.Unsupported);
            Assert.Contains(new QuestRuntimeProperty("QuestScript", "Count", 3), data.ScriptProperties);
            Assert.Equal("QF_TestQuest", data.FragmentScript);
            Assert.Contains(new QuestRuntimeProperty(string.Empty, "Stage", 3), data.FragmentProperties);
            Assert.Contains(new QuestRuntimeFragment(10, 0, "QF_TestQuest", "Fragment_Stage_0010"), data.Fragments);
            Assert.Contains(5, data.Aliases);
            Assert.Contains(new QuestRuntimeProperty("AliasScript", "Target", 1), data.AliasScriptProperties);
        }
        finally
        {
            Directory.Delete(root, recursive: true);
        }
    }

    [Fact]
    public void StartGameFlagComesFromDnam()
    {
        var root = Path.Combine(Path.GetTempPath(), "radicle_quest_runtime_flag_test");
        try
        {
            using var plugin = LoadQuestPlugin(0, root);
            var data = QuestRuntime.Decode(plugin, new FormKey("Quests.esp", 0x000800));

            Assert.False(data.StartGameEnabled);
            Assert.Empty(data.Unsupported);
        }
        finally
        {
            Directory.Delete(root, recursive: true);
        }
    }

    [Fact]
    public void MissingQuestIsReportedAsUnsupported()
    {
        var root = Path.Combine(Path.GetTempPath(), "radicle_quest_runtime_missing_test");
        try
        {
            using var plugin = LoadQuestPlugin(1, root);
            var data = QuestRuntime.Decode(plugin, new FormKey("Quests.esp", 0x000900));

            Assert.NotEmpty(data.Unsupported);
            Assert.Empty(data.Fragments);
            Assert.Empty(data.ScriptProperties);
        }
        finally
        {
            Directory.Delete(root, recursive: true);
        }
    }

    [Fact]
    public void RowsAreTabSeparatedAndReleasable()
    {
        var root = Path.Combine(Path.GetTempPath(), "radicle_quest_runtime_rows_test");
        try
        {
            using var plugin = LoadQuestPlugin(1, root);
            var rows = QuestRuntime.Rows(plugin, new FormKey("Quests.esp", 0x000800));

            Assert.Equal(9, rows.Length);
            Assert.Contains("quest\t000800:Quests.esp\t1", rows);
            Assert.Contains("fragment\t10\t0\tQF_TestQuest\tFragment_Stage_0010", rows);
        }
        finally
        {
            Directory.Delete(root, recursive: true);
        }
    }
}
