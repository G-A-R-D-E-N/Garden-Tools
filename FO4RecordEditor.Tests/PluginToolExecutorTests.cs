using FluentAssertions;
using FO4RecordEditor.Services;
using Xunit;

namespace FO4RecordEditor.Tests;

public class PluginToolExecutorTests
{
    // With no env and no loose mods loaded, the executor must respond gracefully
    // (not throw) so the agent loop can feed a sensible result back to the model.
    [Fact]
    public void ListPlugins_NoneLoaded_ReturnsFriendlyMessage()
    {
        // Reset process-global registries so other tests' created plugins don't leak in.
        FO4RecordEditor.Services.MutagenLoader.LooseMods.Clear();
        FO4RecordEditor.Services.MutagenLoader.EditableMods.Clear();
        var exec = new PluginToolExecutor(() => null);
        exec.Execute("list_plugins", "{}").Should().Be("No plugins loaded.");
    }

    [Fact]
    public void UnknownTool_ReturnsUnknownMessage()
    {
        var exec = new PluginToolExecutor(() => null);
        exec.Execute("does_not_exist", "{}").Should().StartWith("Unknown tool");
    }

    [Fact]
    public void Execute_ParsesArgs_WithoutThrowing()
    {
        // Reset process-global registries so other tests' created plugins don't leak in.
        FO4RecordEditor.Services.MutagenLoader.LooseMods.Clear();
        FO4RecordEditor.Services.MutagenLoader.EditableMods.Clear();
        var exec = new PluginToolExecutor(() => null);
        // plugin not loaded -> friendly message, but arg parsing must not throw
        exec.Execute("list_record_types", "{\"plugin\":\"Missing.esp\"}")
            .Should().Contain("not loaded");
        // With nothing loaded, get_record now returns a diagnostic listing what IS loaded
        // (instead of a terse "plugin not loaded") so the AI can self-correct.
        exec.Execute("get_record", "{\"plugin\":\"Missing.esp\",\"id\":\"001234:Missing.esp\"}")
            .Should().Contain("not found").And.Contain("Loaded plugins: none");
    }

    [Fact]
    public void ToolDefinitions_ExposesReadAndWriteTools()
    {
        // 9 read/diagnose tools (incl. get_conflicts, get_winning_record, search_robco_configs)
        // + 16 write tools (incl. copy_as_override, set_components, set_conditions, revert_overrides).
        PluginToolExecutor.ToolDefinitions().Should().HaveCount(25);
        PluginToolExecutor.McpToolDefinitions().Should().HaveCount(25);

        var names = PluginToolExecutor.ToolDefinitions()
            .Select(t => (string)t.GetType().GetProperty("name")!.GetValue(t)!).ToList();
        names.Should().Contain(new[]
        {
            "get_conflicts", "get_winning_record", "search_robco_configs",
            "copy_as_override", "delete_record", "remove_list_item", "set_components", "set_conditions",
            "revert_overrides",
        });
    }
}
