using System;
using System.IO;
using FluentAssertions;
using FO4RecordEditor.Services;
using Mutagen.Bethesda.Fallout4;
using Mutagen.Bethesda.Plugins;
using Xunit;

namespace FO4RecordEditor.Core.Tests;

public sealed class WriteServiceCreatePluginTests
{
    [Theory]
    [InlineData("Fallout4.esm")]
    [InlineData("fallout4.esm")]
    [InlineData("DLCRobot.esm")]
    [InlineData("DLCworkshop01.esm")]
    [InlineData("DLCNukaWorld.esm")]
    [InlineData("DLCUltraHighResolution.esm")]
    [InlineData("/abs/Fallout4.esm")]
    public void CreatePlugin_ProtectedMaster_RefusedAndNotRegistered(string name)
    {
        var fileName = Path.GetFileName(name);
        try
        {
            var result = WriteService.CreatePlugin(name);

            ToolError.IsMarked(result).Should().BeTrue();
            result.Should().Contain("write-protected");
            WriteService.GetMutable(fileName).Should().BeNull();
            MutagenLoader.EditableMods.ContainsKey(fileName).Should().BeFalse();
        }
        finally
        {
            WriteService.DiscardScriptPatch(fileName);
        }
    }

    [Fact]
    public void CreatePlugin_AlreadyOpen_RefusedAndOriginalGraphKept()
    {
        var name = $"Collision_{Guid.NewGuid():N}.esp";
        try
        {
            WriteService.CreatePlugin(name).Should().Contain("Created new plugin");
            var original = WriteService.GetMutable(name);
            original.Should().NotBeNull();

            var second = WriteService.CreatePlugin(name);
            ToolError.IsMarked(second).Should().BeTrue();
            second.Should().Contain("already open");

            WriteService.GetMutable(name).Should().BeSameAs(original);
        }
        finally
        {
            WriteService.DiscardScriptPatch(name);
        }
    }

    [Fact]
    public void CreatePlugin_AlreadyOpen_DifferentCase_Refused()
    {
        var name = $"CaseInsensitive_{Guid.NewGuid():N}.esp";
        try
        {
            WriteService.CreatePlugin(name).Should().Contain("Created new plugin");
            var original = WriteService.GetMutable(name);

            var second = WriteService.CreatePlugin(name.ToUpperInvariant());
            ToolError.IsMarked(second).Should().BeTrue();
            second.Should().Contain("already open");
            WriteService.GetMutable(name).Should().BeSameAs(original);
        }
        finally
        {
            WriteService.DiscardScriptPatch(name);
        }
    }

    [Fact]
    public void CreatePlugin_ExtensionAppendedName_Collides()
    {
        var name = $"NoExt_{Guid.NewGuid():N}.esp";
        var bare = name[..^4];
        try
        {
            WriteService.CreatePlugin(name).Should().Contain("Created new plugin");

            var second = WriteService.CreatePlugin(bare);
            ToolError.IsMarked(second).Should().BeTrue();
            second.Should().Contain("already open");
        }
        finally
        {
            WriteService.DiscardScriptPatch(name);
        }
    }

    [Fact]
    public void CreatePlugin_AfterRefusedRecreate_SaveDoesNotOverwriteOriginalFile()
    {
        var dir = Path.Combine(Path.GetTempPath(), "fo4re-create-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(dir);
        var fileName = "DataLoss.esp";
        var filePath = Path.Combine(dir, fileName);
        try
        {
            WriteService.CreatePlugin(fileName).Should().Contain("Created new plugin");
            WriteService.CreateRecord(fileName, "WEAP", "DataLossWeapon", null).Should().Contain("Created WEAP");
            WriteService.SavePlugin(fileName, filePath, null).Should().Contain("Saved");
            WriteService.DiscardScriptPatch(fileName);

            var originalBytes = File.ReadAllBytes(filePath);
            originalBytes.Should().NotBeEmpty();

            WriteService.OpenPlugin(filePath, null).Should().Contain("Opened");

            var recreated = WriteService.CreatePlugin(fileName);
            ToolError.IsMarked(recreated).Should().BeTrue();
            recreated.Should().Contain("already open");

            WriteService.TryGetSourcePath(fileName, out var sourcePath).Should().BeTrue();
            sourcePath.Should().Be(filePath);

            WriteService.SavePlugin(fileName, null, null).Should().Contain("Saved");

            using var reload = Fallout4Mod.CreateFromBinaryOverlay(
                ModPath.FromPath(filePath), Fallout4Release.Fallout4);
            reload.Weapons.Should().ContainSingle(w => w.EditorID == "DataLossWeapon");
        }
        finally
        {
            WriteService.DiscardScriptPatch(fileName);
            try { Directory.Delete(dir, recursive: true); } catch { }
        }
    }

    [Fact]
    public void CreateForScript_ProtectedName_RefusedAndNotRegistered()
    {
        WriteService.CreateForScript("Fallout4.esm", null, "WEAP", "ScriptRec").Should().BeNull();
        MutagenLoader.EditableMods.ContainsKey("Fallout4.esm").Should().BeFalse();
        WriteService.GetMutable("Fallout4.esm").Should().BeNull();
    }

    [Fact]
    public void CreateForScript_NormalName_CreatesRecord()
    {
        var name = $"ScriptPatch_{Guid.NewGuid():N}.esp";
        try
        {
            var rec = WriteService.CreateForScript(name, null, "WEAP", "ScriptRec");
            rec.Should().NotBeNull();
            WriteService.GetMutable(name).Should().NotBeNull();
        }
        finally
        {
            WriteService.DiscardScriptPatch(name);
        }
    }
}
