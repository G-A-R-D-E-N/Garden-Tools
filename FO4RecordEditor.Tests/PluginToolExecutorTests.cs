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
        exec.Execute("get_record", "{\"plugin\":\"Missing.esp\",\"id\":\"001234:Missing.esp\"}")
            .Should().Be("(plugin not loaded)");
    }

    [Fact]
    public void ToolDefinitions_ExposesTheFiveTools()
    {
        var defs = PluginToolExecutor.ToolDefinitions();
        defs.Should().HaveCount(5);
    }
}
