using System.Collections.Generic;
using System.Text.Json;
using System.Threading.Tasks;
using BehaviourStudio.App;
using Xunit;

namespace BehaviourStudio.Tests;

public sealed class OpencodeIsolationTests
{
    [Fact]
    public void TheSessionConfigDisablesEveryForeignServerAndEnablesOnlyBgs()
    {
        string json = OpencodeCli.BuildConfigJson(
            "http://127.0.0.1:1234/mcp?token=t",
            new[] { "f4-re-mcp", "fo4editor", "bgs" });

        using JsonDocument document = JsonDocument.Parse(json);
        JsonElement servers = document.RootElement.GetProperty("mcp");
        Assert.False(servers.GetProperty("f4-re-mcp").GetProperty("enabled").GetBoolean());
        Assert.False(servers.GetProperty("fo4editor").GetProperty("enabled").GetBoolean());
        Assert.True(servers.GetProperty("bgs").GetProperty("enabled").GetBoolean());
    }

    [Fact]
    public async Task SessionRefusesToRunWhenForeignServerDiscoveryFails()
    {
        var previous = AssistantCommandRunner.RunForTest;
        int runs = 0;
        try
        {
            AssistantCommandRunner.RunForTest = (_, _, _, _, _) =>
            {
                runs++;
                return Task.FromResult(new AssistantCommandResult(1, "", "discovery failed"));
            };
            using BgsMcpBridge bridge = BgsMcpBridge.Start(BgsMcpProtocolTests.Tools());
            using var session = new OpencodeAssistantSession(
                new AssistantCli("opencode", "detected"), bridge, "");

            AssistantReply reply = await session.SendAsync("hello", CodexSessionFixture.Context());

            Assert.Equal("provider_error", reply.Status);
            Assert.Equal(1, runs);
        }
        finally
        {
            AssistantCommandRunner.RunForTest = previous;
        }
    }
}
