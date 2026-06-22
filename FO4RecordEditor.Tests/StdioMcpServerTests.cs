using FluentAssertions;
using FO4RecordEditor.Services;
using Xunit;

namespace FO4RecordEditor.Tests;

// Unit tests for the headless stdio MCP dispatch (the --mcp transport). The executor is built
// with () => null (no env) exactly like ClaudeCodeMcpE2E, so tool calls return "No plugins loaded."
public class StdioMcpServerTests
{
    private static PluginToolExecutor NoEnvExecutor() => new(() => null);

    [Fact]
    public void Initialize_ReturnsProtocolAndServerInfo()
    {
        var resp = StdioMcpServer.HandleLine(
            "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\"}", NoEnvExecutor());

        resp.Should().NotBeNull();
        resp!.Should().Contain("\"protocolVersion\"");
        resp.Should().Contain("fo4editor");
        resp.Should().Contain("\"id\":1");
    }

    [Fact]
    public void ToolsList_ReturnsTheRegisteredTools()
    {
        var resp = StdioMcpServer.HandleLine(
            "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\"}", NoEnvExecutor());

        resp.Should().NotBeNull();
        resp!.Should().Contain("open_plugin");
        resp.Should().Contain("set_field");
        resp.Should().Contain("scan_conflicts");
    }

    [Fact]
    public void ToolsCall_InvokesExecutor_AndWrapsResultAsTextContent()
    {
        // Asserts the MCP envelope (the stdio dispatch's job). The specific tool text is not
        // asserted: list_plugins reads MutagenLoader's process-wide loose-mod state, which other
        // tests in the suite mutate, so the text is not deterministic across the run.
        var resp = StdioMcpServer.HandleLine(
            "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/call\"," +
            "\"params\":{\"name\":\"list_plugins\",\"arguments\":{}}}", NoEnvExecutor());

        resp.Should().NotBeNull();
        resp!.Should().Contain("\"type\":\"text\"");
        resp.Should().Contain("\"isError\":false");
        resp.Should().Contain("\"id\":3");
    }

    [Fact]
    public void Notification_WithoutId_ProducesNoResponse()
    {
        var resp = StdioMcpServer.HandleLine(
            "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}", NoEnvExecutor());

        resp.Should().BeNull();
    }

    [Fact]
    public void UnknownMethod_ReturnsMethodNotFoundError()
    {
        var resp = StdioMcpServer.HandleLine(
            "{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"bogus\"}", NoEnvExecutor());

        resp.Should().NotBeNull();
        resp!.Should().Contain("-32601");
    }

    [Fact]
    public void GarbageLine_IsIgnored()
    {
        StdioMcpServer.HandleLine("not json", NoEnvExecutor()).Should().BeNull();
    }
}
