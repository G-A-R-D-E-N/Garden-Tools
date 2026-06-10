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
        // 5 read tools + 5 write tools (open/create_plugin/create_record/set_field/save_plugin).
        PluginToolExecutor.ToolDefinitions().Should().HaveCount(10);
        PluginToolExecutor.McpToolDefinitions().Should().HaveCount(10);
    }
}
