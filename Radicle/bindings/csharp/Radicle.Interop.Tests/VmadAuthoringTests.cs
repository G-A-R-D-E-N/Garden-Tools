using System;
using System.IO;
using System.Text;
using Xunit;

namespace Radicle.Interop.Tests;

public sealed class VmadAuthoringTests
{
    private static byte[] Vmad()
    {
        using var stream = new MemoryStream();
        using var writer = new BinaryWriter(stream, Encoding.UTF8, leaveOpen: true);
        void Name(string text) { writer.Write((ushort)text.Length); writer.Write(Encoding.ASCII.GetBytes(text)); }
        writer.Write((short)6); writer.Write((short)2); writer.Write((ushort)1);
        Name("Script"); writer.Write((byte)3); writer.Write((ushort)6);
        for (byte type = 1; type <= 5; ++type)
        {
            Name("Property" + type); writer.Write(type); writer.Write((byte)7);
            if (type == 1) { writer.Write((ushort)0); writer.Write((short)-1); writer.Write(0x801u); }
            if (type == 2) Name("old");
            if (type == 3) writer.Write(-10);
            if (type == 4) writer.Write(1.5f);
            if (type == 5) writer.Write((byte)0);
        }
        Name("Ints"); writer.Write((byte)13); writer.Write((byte)7); writer.Write(2u); writer.Write(10); writer.Write(20);
        writer.Flush();
        return stream.ToArray();
    }

    [Fact]
    public void TypedPropertiesAndArrayElementSaveAndExpose()
    {
        using var temp = new TempDirectory();
        var bytes = PluginWriter.Build(0, Array.Empty<string>(),
            PluginWriter.Top("REFR", new Rec("REFR", 0x800, new[] { ("VMAD", Vmad()), ("ZZZZ", new byte[] { 7, 8, 9 }) })),
            PluginWriter.Top("MISC", new Rec("MISC", 0x801, new[] { ("ZZZZ", new byte[] { 11 }) })));
        using var plugin = PluginFile.Read(temp.Write("Vmad.esp", bytes));
        var key = new FormKey("Vmad.esp", 0x800);
        Assert.True(plugin.SetVmadObject(key, 0, 0, new FormKey("Vmad.esp", 0x801), 9));
        Assert.True(plugin.SetVmadString(key, 0, 1, "café longer"));
        Assert.True(plugin.SetVmadInt(key, 0, 2, -42));
        Assert.True(plugin.SetVmadFloat(key, 0, 3, 7.5f));
        Assert.True(plugin.SetVmadBool(key, 0, 4, true));
        Assert.True(plugin.SetVmadInt(key, 0, 5, -123, element: 1));
        Assert.False(plugin.SetVmadInt(key, 0, 5, 1, element: 2));
        var path = Path.Combine(temp.Path, "Saved.esp"); plugin.Write(path);
        using var env = RadicleEnvironment.Load(new[] { path });
        var record = env.Winner(new FormKey("Saved.esp", 0x800))!.Value;
        var scripts = env.PlacedReference(record).Scripts;
        var script = Assert.Single(scripts);
        Assert.Equal((byte)3, script.Flags);
        Assert.Equal(new FormKey("Saved.esp", 0x801), script.Properties[0].Object);
        Assert.Equal((short)9, script.Properties[0].Alias);
        Assert.Equal("café longer", script.Properties[1].Text);
        Assert.Equal(-42, script.Properties[2].Integer);
        Assert.Equal(7.5f, script.Properties[3].Real);
        Assert.True(script.Properties[4].Boolean);
        Assert.Equal(2u, script.Properties[5].ElementCount);
        Assert.All(script.Properties, p => Assert.Equal((byte)7, p.Flags));
        Assert.True(plugin.SetVmadObject(key, 0, 0, null));
    }

    [Fact]
    public void InvalidIndicesTypesAndTextPreserveInput()
    {
        using var temp = new TempDirectory();
        var bytes = PluginWriter.Build(0, Array.Empty<string>(),
            PluginWriter.Top("REFR", new Rec("REFR", 0x800, new[] { ("VMAD", Vmad()) })));
        using var plugin = PluginFile.Read(temp.Write("Vmad.esp", bytes));
        var key = new FormKey("Vmad.esp", 0x800);
        Assert.Throws<ArgumentOutOfRangeException>(() => plugin.SetVmadInt(key, -1, 2, 1));
        Assert.Throws<ArgumentOutOfRangeException>(() => plugin.SetVmadInt(key, 0, -1, 1));
        Assert.Throws<ArgumentOutOfRangeException>(() => plugin.SetVmadInt(key, 0, 2, 1, -1));
        Assert.Throws<ArgumentException>(() => plugin.SetVmadString(key, 0, 1, "a\0b"));
        Assert.Throws<ArgumentNullException>(() => plugin.SetVmadString(key, 0, 1, null!));
        Assert.Throws<RadicleException>(() => plugin.SetVmadString(key, 0, 1, new string('a', 65536)));
        Assert.Throws<RadicleException>(() => plugin.SetVmadFloat(key, 0, 3, float.NaN));
        Assert.Throws<RadicleException>(() => plugin.SetVmadInt(key, 0, 1, 1));
        Assert.False(plugin.SetVmadInt(key, 1, 2, 1));
        Assert.Equal(bytes, plugin.ToBytes());
        Assert.True(plugin.SetVmadString(key, 0, 1, "\u0100"));
        var utf8 = Path.Combine(temp.Path, "Utf8.esp"); plugin.Write(utf8);
        using var env = RadicleEnvironment.Load(new[] { utf8 });
        var script = Assert.Single(env.PlacedReference(env.Winner(new FormKey("Utf8.esp", 0x800))!.Value).Scripts);
        Assert.Equal("\u0100", script.Properties[1].Text);
    }

    [Fact]
    public void QuestFragmentEditsRefuseOtherRecordsAndMissingKeysWithoutTouchingBytes()
    {
        using var temp = new TempDirectory();
        var bytes = PluginWriter.Build(0, Array.Empty<string>(),
            PluginWriter.Top("REFR", new Rec("REFR", 0x800, new[] { ("VMAD", Vmad()) })));
        using var plugin = PluginFile.Read(temp.Write("Vmad.esp", bytes));
        var key = new FormKey("Vmad.esp", 0x800);
        var absent = new FormKey("Vmad.esp", 0x999);
        Assert.Throws<RadicleException>(() => plugin.SetQuestVmadFragment(key, 10, 0, "QF_Test", "Fragment_10"));
        Assert.Throws<RadicleException>(() => plugin.RemoveQuestVmadFragment(key, 10, 0));
        Assert.False(plugin.SetQuestVmadFragment(absent, 10, 0, "QF_Test", "Fragment_10"));
        Assert.False(plugin.RemoveQuestVmadFragment(absent, 10, 0));
        Assert.Throws<ArgumentNullException>(() => plugin.SetQuestVmadFragment(key, 10, 0, null!, "Fragment_10"));
        Assert.Throws<ArgumentException>(() => plugin.SetQuestVmadFragment(key, 10, 0, "QF\0Test", "Fragment_10"));
        Assert.Throws<ArgumentException>(() => plugin.SetQuestVmadFragment(key, 10, 0, "QF_Test", "Fragment\0_10"));
        Assert.Equal(bytes, plugin.ToBytes());
    }

    private static byte[] SceneVmad()
    {
        using var stream = new MemoryStream();
        using var writer = new BinaryWriter(stream, Encoding.UTF8, leaveOpen: true);
        void Name(string text) { writer.Write((ushort)text.Length); writer.Write(Encoding.ASCII.GetBytes(text)); }
        writer.Write((short)6); writer.Write((short)2); writer.Write((ushort)1);
        Name("Script"); writer.Write((byte)3); writer.Write((ushort)0);
        writer.Write((byte)3);
        writer.Write((byte)0);
        Name("PF_Frag"); writer.Write((byte)0); writer.Write((ushort)0);
        writer.Write((ushort)0);
        writer.Flush();
        return stream.ToArray();
    }

    [Fact]
    public void StructureEditsAddScriptsPropertiesAndFragments()
    {
        using var temp = new TempDirectory();
        var bytes = PluginWriter.Build(0, Array.Empty<string>(),
            PluginWriter.Top("REFR", new Rec("REFR", 0x800, new[] { ("ZZZZ", new byte[] { 7, 8 }) })),
            PluginWriter.Top("SCEN", new Rec("SCEN", 0x801, new[] { ("VMAD", SceneVmad()) })));
        using var plugin = PluginFile.Read(temp.Write("Vmad.esp", bytes));
        var key = new FormKey("Vmad.esp", 0x800);
        var scene = new FormKey("Vmad.esp", 0x801);
        plugin.AddVmad(key);
        Assert.Throws<RadicleException>(() => plugin.AddVmad(key));
        Assert.Equal(0, plugin.AddVmadScript(key, 0, "PF_Test", 1));
        var beforeRefusal = plugin.ToBytes();
        Assert.Throws<RadicleException>(() => plugin.AddVmadScript(key, 1, "pf_test", 1));
        Assert.Throws<RadicleException>(() => plugin.AddVmadScript(key, 9, "Other"));
        Assert.Throws<ArgumentException>(() => plugin.AddVmadScript(key, 1, ""));
        Assert.Throws<ArgumentNullException>(() => plugin.AddVmadScript(key, 1, null!));
        Assert.Equal(beforeRefusal, plugin.ToBytes());
        var types = new byte[] { 1, 2, 3, 4, 5, 7, 11, 12, 13, 14, 15, 17 };
        for (var index = 0; index < types.Length; ++index)
        {
            Assert.Equal(index, plugin.AddVmadProperty(key, 0, index, "P" + types[index], types[index]));
        }
        Assert.True(plugin.SetVmadInt(key, 0, 2, -7));
        Assert.True(plugin.SetVmadInt(key, 0, 8, 7));
        Assert.Throws<RadicleException>(() => plugin.AddVmadProperty(key, 0, types.Length, "Bad", 16));
        Assert.Throws<RadicleException>(() => plugin.RemoveVmadProperty(key, 0, 40));
        var path = Path.Combine(temp.Path, "Saved.esp");
        plugin.Write(path);
        using (var env = RadicleEnvironment.Load(new[] { path }))
        {
            var record = env.Winner(new FormKey("Saved.esp", 0x800))!.Value;
            var script = Assert.Single(env.PlacedReference(record).Scripts);
            Assert.Equal("PF_Test", script.Name);
            Assert.Equal((byte)1, script.Flags);
            Assert.Equal(-7, script.Properties[2].Integer);
            Assert.Equal(1u, script.Properties[8].ElementCount);
        }
        plugin.AddVmadFragment(scene, 0, "PF_Frag", "Fragment_0");
        Assert.Throws<RadicleException>(() => plugin.AddVmadFragment(scene, 0, "PF_Frag", "Fragment_0"));
        Assert.Throws<RadicleException>(() => plugin.AddVmadFragment(scene, 4, "PF_Frag", "Fragment_4"));
        plugin.RemoveVmadFragment(scene, 0);
        Assert.Throws<RadicleException>(() => plugin.RemoveVmadFragment(scene, 0));
        Assert.Throws<RadicleException>(() => plugin.AddVmadFragment(key, 0, "PF_Frag", "Fragment_0"));
        plugin.RemoveVmadProperty(key, 0, 8);
        plugin.RemoveVmadScript(key, 0);
        Assert.Throws<RadicleException>(() => plugin.RemoveVmadScript(key, 0));
    }
}
