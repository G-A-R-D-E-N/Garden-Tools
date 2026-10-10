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
}
