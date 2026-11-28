using System;
using System.IO;
using System.Linq;
using System.Text;
using Radicle.Interop.Native;
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

    [Fact]
    public void ScenePhaseFragmentRoundTripsThroughTheReparsedPlugin()
    {
        using var temp = new TempDirectory();
        var path = temp.Write("ScenePhase.esp", PluginWriter.Build(0, Array.Empty<string>(),
            PluginWriter.Top("SCEN", new Rec("SCEN", 0x800, new[] { ("VMAD", Vmad()) }))));
        var key = new FormKey("ScenePhase.esp", 0x800);
        using (var plugin = PluginFile.Read(path))
        {
            // Constraint: SCEN has no authoring schema, so the scene comes from the fixture and only its fragment section is authored here.
            plugin.BootstrapFragmentSection(key, "PF_Scene");
            plugin.SetScenePhaseFragment(key, 0, VmadPhaseFlag.OnStart, "PF_Scene", "Phase_0");
            plugin.SetScenePhaseFragment(key, 1, VmadPhaseFlag.OnCompletion, "PF_Scene", "Phase_1");
            plugin.SetScenePhaseFragment(key, 1, VmadPhaseFlag.OnCompletion, "PF_Scene", "Phase_1b");
            plugin.Write(path);
        }

        using var reparsed = PluginFile.Read(path);
        var phases = reparsed.ScenePhaseFragments(key);
        Assert.Equal(2, phases.Count);
        Assert.Equal(0u, phases[0].PhaseIndex);
        Assert.Equal(VmadPhaseFlag.OnStart, phases[0].Flag);
        Assert.Equal("PF_Scene", phases[0].Script);
        Assert.Equal("Phase_0", phases[0].Function);
        Assert.Equal(1u, phases[1].PhaseIndex);
        Assert.Equal(VmadPhaseFlag.OnCompletion, phases[1].Flag);
        Assert.Equal("Phase_1b", phases[1].Function);
        Assert.Throws<ArgumentOutOfRangeException>(() => reparsed.SetScenePhaseFragment(key, 0, (VmadPhaseFlag)4, "PF_Scene", "Phase_X"));
        Assert.Empty(reparsed.ScenePhaseFragments(new FormKey("ScenePhase.esp", 0x999)));

        // Regression: the remove export had no managed wrapper, so a consumer could bind a phase fragment and never drop it; a refused or absent remove must leave the bytes alone.
        var bound = reparsed.ToBytes();
        Assert.Throws<ArgumentOutOfRangeException>(() => reparsed.RemoveScenePhaseFragment(key, 0, (VmadPhaseFlag)3));
        Assert.False(reparsed.RemoveScenePhaseFragment(key, 0, VmadPhaseFlag.OnCompletion));
        Assert.False(reparsed.RemoveScenePhaseFragment(new FormKey("ScenePhase.esp", 0x999), 0, VmadPhaseFlag.OnStart));
        Assert.Equal(bound, reparsed.ToBytes());
        Assert.True(reparsed.RemoveScenePhaseFragment(key, 0, VmadPhaseFlag.OnStart));
        var left = Assert.Single(reparsed.ScenePhaseFragments(key));
        Assert.Equal(1u, left.PhaseIndex);
        Assert.Equal("Phase_1b", left.Function);
    }

    // Constraint: a TERM fragment section cannot be created by the writer, so the fixture carries one with entries 1 and 5 and the test authors the rest.
    private static byte[] TerminalVmad()
    {
        using var stream = new MemoryStream();
        using var writer = new BinaryWriter(stream, Encoding.UTF8, leaveOpen: true);
        void Name(string text) { writer.Write((ushort)text.Length); writer.Write(Encoding.ASCII.GetBytes(text)); }
        void Entry(uint index, string function) { writer.Write(index); writer.Write((byte)0); Name("PF_Term"); Name(function); }
        writer.Write((short)6); writer.Write((short)2); writer.Write((ushort)0);
        writer.Write((byte)3);
        Name("PF_Term"); writer.Write((byte)0); writer.Write((ushort)1);
        Name("Label"); writer.Write((byte)2); writer.Write((byte)1); Name("Menu");
        writer.Write((ushort)2);
        Entry(1, "Fragment_Terminal_01");
        Entry(5, "Fragment_Terminal_05");
        writer.Flush();
        return stream.ToArray();
    }

    // Constraint: a binding written by AddTerminalFragment must read back from a loaded TERM; the regression caught is a write-only fragment the engine could not name.
    [Fact]
    public void TerminalFragmentsReadBackAuthoredBindingsThroughTheReparsedEnvironment()
    {
        using var temp = new TempDirectory();
        var path = temp.Write("Terminal.esp", PluginWriter.Build(0, Array.Empty<string>(),
            PluginWriter.Top("TERM",
                new Rec("TERM", 0x800, new[] { ("VMAD", TerminalVmad()) }),
                new Rec("TERM", 0x801, new[] { ("VMAD", Vmad()) }),
                new Rec("TERM", 0x802, Array.Empty<(string, byte[])>())),
            PluginWriter.Top("MISC", new Rec("MISC", 0x803, new[] { ("ZZZZ", new byte[] { 1 }) }))));
        var key = new FormKey("Terminal.esp", 0x800);
        using (var plugin = PluginFile.Read(path))
        {
            plugin.AddTerminalFragment(key, 3, "PF_Term", "Fragment_Terminal_03");
            Assert.Throws<RadicleException>(() => plugin.AddTerminalFragment(key, 3, "PF_Term", "Fragment_Again"));
            plugin.RemoveTerminalFragment(key, 1);
            plugin.Write(path);
        }

        using var env = RadicleEnvironment.Load(new[] { path });
        TerminalFragmentsResult Read(uint id) => env.TerminalFragments(env.Winner(new FormKey("Terminal.esp", id))!.Value);
        var present = Read(0x800);
        Assert.Equal(TermFragmentState.Present, present.FragmentState);
        Assert.Null(present.FragmentFailure);
        Assert.Equal(3, present.FragmentVersion);
        Assert.Equal("PF_Term", present.FragmentScriptName);
        Assert.Equal(new uint[] { 3, 5 }, present.Fragments.Select(f => f.Index));
        Assert.Equal(new[] { "Fragment_Terminal_03", "Fragment_Terminal_05" }, present.Fragments.Select(f => f.Function));
        Assert.All(present.Fragments, f => Assert.Equal("PF_Term", f.Script));
        Assert.Equal("PF_Term", present.FragmentScript!.Name);
        var label = Assert.Single(present.FragmentScript.Properties);
        Assert.Equal("Label", label.Name);
        Assert.Equal("Menu", label.Value.Text);

        var bare = Read(0x801);
        Assert.Equal(TermFragmentState.NoSection, bare.FragmentState);
        Assert.Empty(bare.Fragments);
        Assert.Null(bare.FragmentScript);
        var none = Read(0x802);
        Assert.Equal(TermFragmentState.NoVmad, none.FragmentState);
        Assert.Empty(none.Fragments);
        Assert.Throws<RadicleException>(() => Read(0x803));
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

    private static byte[] QuestFragments()
    {
        using var stream = new MemoryStream();
        using var writer = new BinaryWriter(stream, Encoding.UTF8, leaveOpen: true);
        void Name(string text) { writer.Write((ushort)text.Length); writer.Write(Encoding.ASCII.GetBytes(text)); }
        void Binding(ushort stage, int index, string function)
        {
            writer.Write(stage); writer.Write((ushort)0); writer.Write(index); writer.Write((byte)0);
            Name("QF_Test"); Name(function);
        }
        writer.Write((ushort)6); writer.Write((ushort)2); writer.Write((ushort)0);
        writer.Write((byte)3);
        writer.Write((ushort)2);
        writer.Write((ushort)0);
        Binding(10, 0, "Fragment_10");
        Binding(10, 1, "Fragment_TenB");
        writer.Write((ushort)0);
        writer.Flush();
        return stream.ToArray();
    }

    // Constraint: a quest binds one stage twice here, so a stage-only edit would address the wrong binding or refuse an addressable one.
    [Fact]
    public void FragmentShapeEditsAddressOnlyTheQuestStagePair()
    {
        using var temp = new TempDirectory();
        var bytes = PluginWriter.Build(0, Array.Empty<string>(),
            PluginWriter.Top("QUST", new Rec("QUST", 0x810, new[] { ("VMAD", QuestFragments()) })),
            PluginWriter.Top("REFR", new Rec("REFR", 0x800, new[] { ("VMAD", Vmad()) })));
        using var plugin = PluginFile.Read(temp.Write("Shapes.esp", bytes));
        var quest = new FormKey("Shapes.esp", 0x810);
        var refr = new FormKey("Shapes.esp", 0x800);

        Assert.True(plugin.SetQuestFragment(quest, 10, 1, "QF_Test", "Fragment_Renamed"));
        var renamed = Encoding.ASCII.GetString(plugin.ToBytes());
        Assert.Contains("Fragment_Renamed", renamed);
        Assert.Contains("Fragment_10", renamed);
        Assert.DoesNotContain("Fragment_TenB", renamed);
        // Constraint: an unchanged write reports found, so the pair binding returns true for an identical edit and false only for an absent pair.
        Assert.True(plugin.SetQuestFragment(quest, 10, 1, "QF_Test", "Fragment_Renamed"));
        Assert.False(plugin.SetQuestFragment(quest, 11, 0, "QF_Test", "Fragment_Eleven"));
        Assert.Throws<RadicleException>(() => plugin.SetQuestFragment(refr, 10, 0, "QF_Test", "Fragment_10"));
        Assert.Throws<ArgumentException>(() => plugin.SetQuestFragment(quest, 10, 0, "", "Fragment_10"));
        Assert.Throws<ArgumentException>(() => plugin.SetQuestFragment(quest, 10, 0, "QF_Test", "Fragment\0_10"));
        Assert.Throws<ArgumentNullException>(() => plugin.SetQuestFragment(quest, 10, 0, null!, "Fragment_10"));

        Assert.True(plugin.RemoveQuestFragment(quest, 10, 1));
        var removed = Encoding.ASCII.GetString(plugin.ToBytes());
        Assert.Contains("Fragment_10", removed);
        Assert.DoesNotContain("Fragment_Renamed", removed);
        Assert.False(plugin.RemoveQuestFragment(quest, 10, 1));
    }

    private static byte[] NamedQuestFragments()
    {
        using var stream = new MemoryStream();
        using var writer = new BinaryWriter(stream, Encoding.UTF8, leaveOpen: true);
        void Name(string text) { writer.Write((ushort)text.Length); writer.Write(Encoding.ASCII.GetBytes(text)); }
        writer.Write((ushort)6); writer.Write((ushort)2); writer.Write((ushort)0);
        writer.Write((byte)3); writer.Write((ushort)1);
        Name("QF_Test"); writer.Write((byte)0); writer.Write((ushort)0);
        writer.Write((ushort)10); writer.Write((ushort)0); writer.Write(0); writer.Write((byte)1);
        Name("QF_Test"); Name("Fragment_Stage_0010_Item_00");
        writer.Write((ushort)0);
        writer.Flush();
        return stream.ToArray();
    }

    // Detects: a log entry appended to a stage that already has a fragment binding gets no binding of its own through the managed API, so the new entry stays silent when the stage is set; the add must keep the existing binding, bind the new (stage, index) pair with the given names, and survive a save and re-read.
    [Fact]
    public void AddQuestFragmentBindsAnAppendedLogEntryAndKeepsTheExistingBinding()
    {
        using var temp = new TempDirectory();
        var bytes = PluginWriter.Build(0, Array.Empty<string>(),
            PluginWriter.Top("QUST", new Rec("QUST", 0x810, new[]
            {
                ("EDID", Bytes.Z("BoundQuest")), ("VMAD", NamedQuestFragments()),
                ("INDX", new byte[] { 10, 0, 2, 7 }), ("QSDT", new byte[] { 1 }), ("CNAM", Bytes.Z("First")),
            })),
            PluginWriter.Top("REFR", new Rec("REFR", 0x800, new[] { ("VMAD", Vmad()) })));
        var path = temp.Write("Bound.esp", bytes);
        var quest = new FormKey("Bound.esp", 0x810);
        using (var plugin = PluginFile.Read(path))
        {
            Assert.True(plugin.AddQuestLogEntry(quest, 10, 1, "Second"));
            Assert.Equal(new[] { new QuestRuntimeFragment(10, 0, "QF_Test", "Fragment_Stage_0010_Item_00") },
                QuestRuntime.Decode(plugin, quest).Fragments);

            Assert.True(plugin.AddQuestFragment(quest, 10, 1, "QF_Test", "Fragment_Stage_0010_Item_01"));
            Assert.Throws<RadicleException>(() => plugin.AddQuestFragment(quest, 10, 1, "QF_Test", "Fragment_Again"));
            Assert.Throws<RadicleException>(() => plugin.AddQuestFragment(new FormKey("Bound.esp", 0x800), 10, 1, "QF_Test", "Fragment_Refr"));
            Assert.False(plugin.AddQuestFragment(new FormKey("Bound.esp", 0x811), 10, 1, "QF_Test", "Fragment_Absent"));
            Assert.Throws<ArgumentException>(() => plugin.AddQuestFragment(quest, 10, 2, "", "Fragment_10"));
            Assert.Throws<ArgumentException>(() => plugin.AddQuestFragment(quest, 10, 2, "QF_Test", "Fragment\0_10"));
            Assert.Throws<ArgumentNullException>(() => plugin.AddQuestFragment(quest, 10, 2, null!, "Fragment_10"));
            Directory.CreateDirectory(Path.Combine(temp.Path, "Saved"));
            plugin.Write(Path.Combine(temp.Path, "Saved", "Bound.esp"));
        }

        using var reread = PluginFile.Read(Path.Combine(temp.Path, "Saved", "Bound.esp"));
        var data = QuestRuntime.Decode(reread, quest);
        Assert.Empty(data.Unsupported);
        Assert.Equal("QF_Test", data.FragmentScript);
        Assert.Equal(new[]
        {
            new QuestRuntimeFragment(10, 0, "QF_Test", "Fragment_Stage_0010_Item_00"),
            new QuestRuntimeFragment(10, 1, "QF_Test", "Fragment_Stage_0010_Item_01"),
        }, data.Fragments);
    }

    // Constraint: PACK owns three flag bits while INFO and SCEN own two, so OnChange must be reachable on PACK only and never become a key the writer applies to a two-bit record.
    [Fact]
    public void FragmentFlagShapesRefuseBitsTheRecordDoesNotCarry()
    {
        using var temp = new TempDirectory();
        var bytes = PluginWriter.Build(0, Array.Empty<string>(),
            PluginWriter.Top("SCEN", new Rec("SCEN", 0x801, new[] { ("VMAD", SceneVmad()) })));
        using var plugin = PluginFile.Read(temp.Write("Flags.esp", bytes));
        var scene = new FormKey("Flags.esp", 0x801);

        Assert.Throws<ArgumentOutOfRangeException>(() => plugin.AddSceneFragment(scene, VmadFragmentFlag.OnChange, "PF_Frag", "Fragment_Two"));
        Assert.Throws<ArgumentOutOfRangeException>(() => plugin.AddInfoFragment(scene, VmadFragmentFlag.OnChange, "PF_Frag", "Fragment_Two"));
        Assert.Throws<ArgumentOutOfRangeException>(() => plugin.RemoveSceneFragment(scene, VmadFragmentFlag.OnChange));
        plugin.AddSceneFragment(scene, VmadFragmentFlag.OnBegin, "PF_Frag", "Fragment_0");
        Assert.Contains("Fragment_0", Encoding.ASCII.GetString(plugin.ToBytes()));
        Assert.Throws<RadicleException>(() => plugin.AddSceneFragment(scene, VmadFragmentFlag.OnBegin, "PF_Frag", "Fragment_0"));
        Assert.Throws<RadicleException>(() => plugin.AddPackageFragment(scene, VmadFragmentFlag.OnChange, "PF_Frag", "Fragment_Two"));
        Assert.Throws<ArgumentException>(() => plugin.AddSceneFragment(scene, VmadFragmentFlag.OnEnd, "", "Fragment_1"));
        Assert.Throws<ArgumentException>(() => plugin.AddSceneFragment(scene, VmadFragmentFlag.OnEnd, "PF\0Frag", "Fragment_1"));
        plugin.AddSceneFragment(scene, VmadFragmentFlag.OnEnd, "PF_Frag", "Fragment_1");
        plugin.RemoveSceneFragment(scene, VmadFragmentFlag.OnBegin);
        Assert.Throws<RadicleException>(() => plugin.RemoveSceneFragment(scene, VmadFragmentFlag.OnBegin));
        plugin.AddSceneFragment(scene, VmadFragmentFlag.OnBegin, "PF_Frag", "Fragment_0b");
        Assert.Contains("Fragment_0b", Encoding.ASCII.GetString(plugin.ToBytes()));
    }
}
