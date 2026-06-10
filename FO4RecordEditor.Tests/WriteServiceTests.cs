using System.IO;
using FluentAssertions;
using FO4RecordEditor.Services;
using Xunit;

namespace FO4RecordEditor.Tests;

public class WriteServiceTests
{
    [Fact]
    public void Author_Plugin_AddHolotape_SetName_Save_ReadBack()
    {
        var plugin = $"WriteTest_{Guid.NewGuid():N}.esp";
        var exec = new PluginToolExecutor(() => null);

        // create_plugin
        exec.Execute("create_plugin", $"{{\"name\":\"{plugin}\"}}")
            .Should().Contain("Created new plugin");

        // it shows up in list_plugins (mutable mod mirrored into the read tools)
        exec.Execute("list_plugins", "{}").Should().Contain(plugin);

        // create_record BOOK
        var createResult = exec.Execute("create_record",
            $"{{\"plugin\":\"{plugin}\",\"type\":\"BOOK\",\"editorId\":\"Test_Holotape\"}}");
        createResult.Should().Contain("Created BOOK").And.Contain("Test_Holotape");

        // set_field Name
        exec.Execute("set_field",
            $"{{\"plugin\":\"{plugin}\",\"record\":\"Test_Holotape\",\"field\":\"Name\",\"value\":\"My Test Tape\"}}")
            .Should().Contain("Set Name");

        // read it back through get_record (proves the read tools see the new record + field)
        var dump = exec.Execute("get_record", $"{{\"plugin\":\"{plugin}\",\"id\":\"Test_Holotape\"}}");
        dump.Should().Contain("Test_Holotape").And.Contain("My Test Tape");

        // save_plugin writes a real ESP
        var outPath = Path.Combine(Path.GetTempPath(), plugin);
        var saveResult = exec.Execute("save_plugin",
            $"{{\"plugin\":\"{plugin}\",\"path\":\"{outPath.Replace("\\", "\\\\")}\"}}");
        saveResult.Should().Contain("Saved");
        File.Exists(outPath).Should().BeTrue();

        try { File.Delete(outPath); } catch { /* overlay may still map it briefly */ }
    }

    [Fact]
    public void CreateRecord_InEsl_AssignsFormIdWithinLightRange()
    {
        var plugin = $"EslTest_{Guid.NewGuid():N}.esl";   // light plugin
        var exec = new PluginToolExecutor(() => null);
        exec.Execute("create_plugin", $"{{\"name\":\"{plugin}\"}}");

        var result = exec.Execute("create_record",
            $"{{\"plugin\":\"{plugin}\",\"type\":\"BOOK\",\"editorId\":\"EslHolotape\"}}");
        result.Should().Contain("Created BOOK");

        // FormKey in the message must be within the ESL object range 0x000800-0x000FFF.
        var dump = exec.Execute("get_record", $"{{\"plugin\":\"{plugin}\",\"id\":\"EslHolotape\"}}");
        var formKey = dump.Split('\n').First(l => l.TrimStart().StartsWith("FormKey:")).Split(':', 2)[1].Trim();
        var idHex = formKey.Split(':')[0];
        var id = Convert.ToUInt32(idHex, 16);
        id.Should().BeInRange(0x800u, 0xFFFu);
    }

    [Fact]
    public void AddListItem_AppendsKeywordToRecord()
    {
        var plugin = $"ListTest_{Guid.NewGuid():N}.esp";
        var exec = new PluginToolExecutor(() => null);
        exec.Execute("create_plugin", $"{{\"name\":\"{plugin}\"}}");
        exec.Execute("create_record", $"{{\"plugin\":\"{plugin}\",\"type\":\"KYWD\",\"editorId\":\"List_Kw\"}}");
        exec.Execute("create_record", $"{{\"plugin\":\"{plugin}\",\"type\":\"WEAP\",\"editorId\":\"List_Gun\"}}");

        var kwDump = exec.Execute("get_record", $"{{\"plugin\":\"{plugin}\",\"id\":\"List_Kw\"}}");
        var kwKey = kwDump.Split('\n').First(l => l.TrimStart().StartsWith("FormKey:")).Split(':', 2)[1].Trim();

        var result = exec.Execute("add_list_item",
            $"{{\"plugin\":\"{plugin}\",\"record\":\"List_Gun\",\"field\":\"Keywords\",\"value\":\"{kwKey}\"}}");
        result.Should().Contain("Added").And.Contain("Keywords");

        // Verify the actual data: save and reload, check the weapon's Keywords via Mutagen.
        var outPath = System.IO.Path.Combine(System.IO.Path.GetTempPath(), plugin);
        exec.Execute("save_plugin",
            $"{{\"plugin\":\"{plugin}\",\"path\":\"{outPath.Replace("\\", "\\\\")}\"}}");

        using var reload = Mutagen.Bethesda.Fallout4.Fallout4Mod.CreateFromBinaryOverlay(
            Mutagen.Bethesda.Plugins.ModPath.FromPath(outPath), Mutagen.Bethesda.Fallout4.Fallout4Release.Fallout4);
        var gun = reload.Weapons.First(w => w.EditorID == "List_Gun");
        gun.Keywords.Should().NotBeNull();
        gun.Keywords!.Select(k => k.FormKey.ID).Should().Contain(0x800u);

        try { System.IO.File.Delete(outPath); } catch { }
    }

    [Fact]
    public void AttachScript_AndSetObjectProperty_WiresHolotapeToTerminal()
    {
        var plugin = $"VmadTest_{Guid.NewGuid():N}.esp";
        var exec = new PluginToolExecutor(() => null);
        exec.Execute("create_plugin", $"{{\"name\":\"{plugin}\"}}");
        exec.Execute("create_record", $"{{\"plugin\":\"{plugin}\",\"type\":\"BOOK\",\"editorId\":\"VmadTape\"}}");
        exec.Execute("create_record", $"{{\"plugin\":\"{plugin}\",\"type\":\"TERM\",\"editorId\":\"VmadTerminal\"}}");

        exec.Execute("attach_script",
            $"{{\"plugin\":\"{plugin}\",\"record\":\"VmadTape\",\"script\":\"MyHolotapeProgram\"}}")
            .Should().Contain("Attached script");

        var termDump = exec.Execute("get_record", $"{{\"plugin\":\"{plugin}\",\"id\":\"VmadTerminal\"}}");
        var termKey = termDump.Split('\n').First(l => l.TrimStart().StartsWith("FormKey:")).Split(':', 2)[1].Trim();

        exec.Execute("set_script_property",
            $"{{\"plugin\":\"{plugin}\",\"record\":\"VmadTape\",\"script\":\"MyHolotapeProgram\",\"name\":\"Terminal\",\"value\":\"{termKey}\",\"type\":\"object\"}}")
            .Should().Contain("Set script property");

        // Save + reload, verify the script + object property persisted via Mutagen.
        var outPath = System.IO.Path.Combine(System.IO.Path.GetTempPath(), plugin);
        exec.Execute("save_plugin", $"{{\"plugin\":\"{plugin}\",\"path\":\"{outPath.Replace("\\", "\\\\")}\"}}");

        using var reload = Mutagen.Bethesda.Fallout4.Fallout4Mod.CreateFromBinaryOverlay(
            Mutagen.Bethesda.Plugins.ModPath.FromPath(outPath), Mutagen.Bethesda.Fallout4.Fallout4Release.Fallout4);
        var tape = reload.Books.First(b => b.EditorID == "VmadTape");
        tape.VirtualMachineAdapter.Should().NotBeNull();
        var script = tape.VirtualMachineAdapter!.Scripts.First(s => s.Name == "MyHolotapeProgram");
        var objProp = script.Properties.OfType<Mutagen.Bethesda.Fallout4.IScriptObjectPropertyGetter>()
            .First(p => p.Name == "Terminal");
        objProp.Object.FormKey.ID.Should().Be(0x801u);   // the terminal (second record)

        try { System.IO.File.Delete(outPath); } catch { }
    }

    [Fact]
    public void CleanPlugin_UndeletesAndDisablesDeletedRecords()
    {
        var plugin = $"CleanTest_{Guid.NewGuid():N}.esp";
        WriteService.CreatePlugin(plugin);
        WriteService.CreateRecord(plugin, "MISC", "Clean_Widget", null);

        // Mark the record deleted, then clean.
        var mod = WriteService.GetMutable(plugin)!;
        var rec = (Mutagen.Bethesda.Fallout4.IFallout4MajorRecord)mod.EnumerateMajorRecords()
            .First(r => r.EditorID == "Clean_Widget");
        rec.IsDeleted = true;

        var exec = new PluginToolExecutor(() => null);
        exec.Execute("clean_plugin", $"{{\"plugin\":\"{plugin}\"}}")
            .Should().Contain("undeleted + disabled 1");

        var after = mod.EnumerateMajorRecords().First(r => r.EditorID == "Clean_Widget");
        after.IsDeleted.Should().BeFalse();
    }

    [Fact]
    public void CreateRecord_UnsupportedType_ReturnsHelpfulMessage()
    {
        var exec = new PluginToolExecutor(() => null);
        exec.Execute("create_plugin", "{\"name\":\"UnsupTest.esp\"}");
        exec.Execute("create_record", "{\"plugin\":\"UnsupTest.esp\",\"type\":\"ZZZZ\",\"editorId\":\"x\"}")
            .Should().Contain("not supported");
    }

    [Fact]
    public void SetField_Model_SetsNifPath()
    {
        var plugin = $"ModelTest_{Guid.NewGuid():N}.esp";
        var exec = new PluginToolExecutor(() => null);
        exec.Execute("create_plugin", $"{{\"name\":\"{plugin}\"}}");
        exec.Execute("create_record", $"{{\"plugin\":\"{plugin}\",\"type\":\"MISC\",\"editorId\":\"ModelWidget\"}}");

        var nif = "SetDressing\\\\Radio\\\\Radio1.nif";
        exec.Execute("set_field",
            $"{{\"plugin\":\"{plugin}\",\"record\":\"ModelWidget\",\"field\":\"Model\",\"value\":\"{nif}\"}}")
            .Should().Contain("Set Model");

        var dump = exec.Execute("get_record", $"{{\"plugin\":\"{plugin}\",\"id\":\"ModelWidget\"}}");
        dump.Should().Contain("Radio1.nif");
    }

    [Fact]
    public void SetField_FormLink_ByFormKey_WiresRecordsTogether()
    {
        var plugin = $"LinkTest_{Guid.NewGuid():N}.esp";
        var exec = new PluginToolExecutor(() => null);
        exec.Execute("create_plugin", $"{{\"name\":\"{plugin}\"}}");

        // A misc item to be the crafted output...
        exec.Execute("create_record", $"{{\"plugin\":\"{plugin}\",\"type\":\"MISC\",\"editorId\":\"Link_Widget\"}}");
        // ...and a constructible object whose CreatedObject FormLink points at it.
        exec.Execute("create_record", $"{{\"plugin\":\"{plugin}\",\"type\":\"COBJ\",\"editorId\":\"Link_Recipe\"}}");

        // Find the widget's FormKey from its dump.
        var widgetDump = exec.Execute("get_record", $"{{\"plugin\":\"{plugin}\",\"id\":\"Link_Widget\"}}");
        var formKey = widgetDump.Split('\n')
            .First(l => l.TrimStart().StartsWith("FormKey:")).Split(':', 2)[1].Trim();

        // Set the FormLink field by FormKey.
        var setResult = exec.Execute("set_field",
            $"{{\"plugin\":\"{plugin}\",\"record\":\"Link_Recipe\",\"field\":\"CreatedObject\",\"value\":\"{formKey}\"}}");
        setResult.Should().Contain("Set CreatedObject");

        // Confirm the link now resolves to the widget's FormKey in the recipe dump.
        var recipeDump = exec.Execute("get_record", $"{{\"plugin\":\"{plugin}\",\"id\":\"Link_Recipe\"}}");
        recipeDump.Should().Contain(formKey);
    }
}
