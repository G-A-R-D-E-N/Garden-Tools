using FluentAssertions;
using FO4RecordEditor.Services;

namespace FO4RecordEditor.Tests;

public class ClaudeCodeStreamTests
{
    [Fact]
    public void AssistantText_IsStreamedAsIs()
    {
        var line = """{"type":"assistant","message":{"content":[{"type":"text","text":"Hello there"}]}}""";
        ClaudeCodeProvider.ParseStreamLine(line).Should().ContainSingle().Which.Should().Be("Hello there");
    }

    [Fact]
    public void ToolUse_BecomesAStatusLine()
    {
        var line = """{"type":"assistant","message":{"content":[{"type":"tool_use","name":"mcp__fo4editor__get_record","input":{}}]}}""";
        var chunks = ClaudeCodeProvider.ParseStreamLine(line).ToList();
        chunks.Should().ContainSingle();
        chunks[0].Should().Contain("mcp__fo4editor__get_record").And.Contain("🔧");
    }

    [Fact]
    public void InitAndSuccessResult_ProduceNoUserText()
    {
        ClaudeCodeProvider.ParseStreamLine("""{"type":"system","subtype":"init","tools":[]}""").Should().BeEmpty();
        ClaudeCodeProvider.ParseStreamLine("""{"type":"result","subtype":"success","is_error":false,"result":"done"}""").Should().BeEmpty();
    }

    [Fact]
    public void ErrorResult_IsSurfaced()
    {
        var line = """{"type":"result","is_error":true,"result":"MCP connection failed"}""";
        ClaudeCodeProvider.ParseStreamLine(line).Should().ContainSingle()
            .Which.Should().Contain("MCP connection failed");
    }

    [Fact]
    public void NonJsonLine_PassesThroughVerbatim()
    {
        ClaudeCodeProvider.ParseStreamLine("warning: something").Should().ContainSingle()
            .Which.Should().Contain("warning: something");
        ClaudeCodeProvider.ParseStreamLine("   ").Should().BeEmpty();
    }
}
