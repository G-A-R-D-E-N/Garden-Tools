using System;
using System.Linq;
using Xunit;
using static Radicle.Interop.Tests.Bytes;
using static Radicle.Interop.Tests.PluginWriter;

namespace Radicle.Interop.Tests;

public sealed class QuestScriptElementTests
{
    private static byte[] Cat(params byte[][] parts) => parts.SelectMany(p => p).ToArray();

    private static byte[] U16(params ushort[] values) => values.SelectMany(BitConverter.GetBytes).ToArray();

    private static byte[] WString(string text) => Cat(U16((ushort)text.Length), System.Text.Encoding.ASCII.GetBytes(text));

    private static byte[] IntArray(string name, params uint[] values) =>
        Cat(WString(name), new byte[] { 13, 0 }, U32((uint)values.Length), U32(values));

    private static byte[] StringArray(string name, params string[] values) =>
        Cat(WString(name), new byte[] { 12, 0 }, U32((uint)values.Length), Cat(values.Select(WString).ToArray()));

    private static byte[] ObjectArray(string name, params uint[] formIds) =>
        Cat(WString(name), new byte[] { 11, 0 }, U32((uint)formIds.Length),
            Cat(formIds.Select(id => Cat(U16(0, 0xFFFF), U32(id))).ToArray()));

    private static byte[] Vmad() => Cat(
        U16(5, 2, 1),
        WString("ArrayScript"), new byte[] { 0 }, U16(4),
        IntArray("Ints", 4, 5, 6),
        StringArray("Words", "Alpha", "", "Garden"),
        ObjectArray("Targets", 0x805, 0x806),
        WString("Structs"), new byte[] { 17, 0 }, U32(1), U32(0),
        new byte[] { 0 }, U16(0), WString("QF_Arrays"), new byte[] { 0 }, U16(3),
        IntArray("FragInts", 9),
        StringArray("FragWords", "Stage"),
        ObjectArray("FragTargets", 0x807),
        U16(1), U16(0), U16(5), U32(0), U16(0), U16(2), U16(1),
        WString("AliasArrays"), new byte[] { 0 }, U16(1),
        ObjectArray("AliasTargets", 0x808));

    private static QuestRuntimeValue ScriptValue(QuestRuntimeData data, string name) =>
        data.ScriptProperties.Single(p => p.Name == name).Value;

    [Fact]
    public void ExposesDecodedArrayElementsForScriptFragmentAndAliasOwners()
    {
        using var directory = new TempDirectory();
        var path = directory.Write("Quests.esp", Build(0, Array.Empty<string>(),
            Top("QUST", new Rec("QUST", 0x800, new[] { ("VMAD", Vmad()) }))));
        using var plugin = PluginFile.Read(path);
        var data = QuestRuntime.Decode(plugin, new FormKey("Quests.esp", 0x800));

        var ints = ScriptValue(data, "Ints").Elements;
        Assert.NotNull(ints);
        Assert.Equal(new[] { 4, 5, 6 }, ints!.Select(e => e.Integer));
        Assert.All(ints, e => Assert.Null(e.Object));

        var words = ScriptValue(data, "Words").Elements;
        Assert.NotNull(words);
        Assert.Equal(new string?[] { "Alpha", "", "Garden" }, words!.Select(e => e.Text));

        var targets = ScriptValue(data, "Targets").Elements;
        Assert.NotNull(targets);
        Assert.Equal(new FormKey?[] { new FormKey("Quests.esp", 0x805), new FormKey("Quests.esp", 0x806) },
                     targets!.Select(e => e.Object));
        Assert.Equal(new short[] { -1, -1 }, targets.Select(e => e.Alias));

        var structs = ScriptValue(data, "Structs");
        Assert.Equal(1u, structs.ElementCount);
        Assert.Null(structs.Elements);

        var fragmentInts = data.FragmentProperties.Single(p => p.Name == "FragInts").Value.Elements;
        Assert.NotNull(fragmentInts);
        Assert.Equal(new[] { 9 }, fragmentInts!.Select(e => e.Integer));

        var fragmentWords = data.FragmentProperties.Single(p => p.Name == "FragWords").Value.Elements;
        Assert.NotNull(fragmentWords);
        Assert.Equal(new string?[] { "Stage" }, fragmentWords!.Select(e => e.Text));

        var fragmentTargets = data.FragmentProperties.Single(p => p.Name == "FragTargets").Value.Elements;
        Assert.NotNull(fragmentTargets);
        Assert.Equal(new FormKey?[] { new FormKey("Quests.esp", 0x807) }, fragmentTargets!.Select(e => e.Object));

        var alias = data.AliasScriptProperties.Single(p => p.Name == "AliasTargets");
        Assert.Equal(5, alias.Alias);
        Assert.NotNull(alias.Value.Elements);
        Assert.Equal(new FormKey?[] { new FormKey("Quests.esp", 0x808) }, alias.Value.Elements!.Select(e => e.Object));
    }

    private static byte[] StructVmad() => Cat(
        U16(5, 2, 1),
        WString("StructScript"), new byte[] { 0 }, U16(2),
        WString("Point"), new byte[] { 7, 0 }, U32(2),
        WString("Target"), new byte[] { 1, 0 }, U16(0, 0xFFFF), U32(0x805),
        WString("Label"), new byte[] { 2, 0 }, WString("Alpha"),
        WString("Pairs"), new byte[] { 17, 0 }, U32(2), U32(1), WString("Flag"), new byte[] { 5, 0, 1 }, U32(0),
        new byte[] { 0 }, U16(0), WString("QF_Structs"), new byte[] { 0 }, U16(1),
        WString("FragPoint"), new byte[] { 7, 0 }, U32(1), WString("Stage"), new byte[] { 3, 0 }, U32(9),
        U16(1), U16(0), U16(5), U32(0), U16(0), U16(2), U16(1),
        WString("AliasStructs"), new byte[] { 0 }, U16(1),
        WString("AliasPoint"), new byte[] { 7, 0 }, U32(1), WString("Target"), new byte[] { 1, 0 }, U16(0, 5), U32(0x806));

    [Fact]
    public void ExposesDecodedStructMembersForScriptFragmentAndAliasOwners()
    {
        using var directory = new TempDirectory();
        var path = directory.Write("StructQuests.esp", Build(0, Array.Empty<string>(),
            Top("QUST", new Rec("QUST", 0x800, new[] { ("VMAD", StructVmad()) }))));
        using var plugin = PluginFile.Read(path);
        var data = QuestRuntime.Decode(plugin, new FormKey("StructQuests.esp", 0x800));

        var point = ScriptValue(data, "Point").Structs!.Single();
        Assert.Equal(new[] { "Target", "Label" }, point.Members.Select(m => m.Name));
        Assert.Equal(new FormKey?[] { new FormKey("StructQuests.esp", 0x805), null },
                     point.Members.Select(m => m.Value.Object));
        Assert.Equal("Alpha", point.Members[1].Value.Text);
        Assert.Null(ScriptValue(data, "Point").Elements);

        var pairs = ScriptValue(data, "Pairs").Structs!;
        Assert.Equal(2, pairs.Count);
        Assert.True(pairs[0].Members.Single().Value.Boolean);
        Assert.Empty(pairs[1].Members);

        var stage = data.FragmentProperties.Single(p => p.Name == "FragPoint").Value.Structs!.Single();
        Assert.Equal(new[] { "Stage" }, stage.Members.Select(m => m.Name));
        Assert.Equal(9, stage.Members.Single().Value.Integer);

        var alias = data.AliasScriptProperties.Single(p => p.Name == "AliasPoint");
        Assert.Equal(5, alias.Alias);
        Assert.Equal(new FormKey?[] { new FormKey("StructQuests.esp", 0x806) },
                     alias.Value.Structs!.Single().Members.Select(m => m.Value.Object));
    }
}
