using System;
using System.Collections.Generic;
using System.Text.Json;
using System.Threading.Tasks;
using BehaviourStudio.App;
using Xunit;

namespace BehaviourStudio.Tests;

public sealed class OpencodeIsolationTests
{
    [Fact]
    public async Task EveryTurnRediscoversServersSoANewForeignServerCannotSurvive()
    {
        var previous = AssistantCommandRunner.RunForTest;
        var written = new List<string>();
        int discoveryCalls = 0;
        try
        {
            AssistantCommandRunner.RunForTest = (_, arguments, environment, _, _) =>
            {
                if (arguments.Count > 0 && arguments[0] == "debug")
                {
                    discoveryCalls++;
                    string config = discoveryCalls == 1
                        ? "{\"mcp\":{}}"
                        : "{\"mcp\":{\"f4-re-mcp\":{\"type\":\"local\"}}}";
                    return Task.FromResult(new AssistantCommandResult(0, config, ""));
                }
                if (environment is not null &&
                    environment.TryGetValue("OPENCODE_CONFIG", out string? path) &&
                    System.IO.File.Exists(path))
                    written.Add(System.IO.File.ReadAllText(path));
                return Task.FromResult(new AssistantCommandResult(0,
                    "{\"type\":\"text\",\"part\":{\"type\":\"text\",\"text\":\"ok\"}}", ""));
            };

            using BgsMcpBridge bridge = BgsMcpBridge.Start(BgsMcpProtocolTests.Tools());
            using var session = new OpencodeAssistantSession(
                new AssistantCli("opencode", "detected"), bridge, "");

            await session.SendAsync("first", CodexSessionFixture.Context());
            await session.SendAsync("second", CodexSessionFixture.Context());

            Assert.Equal(2, discoveryCalls);
            Assert.Equal(2, written.Count);
            Assert.DoesNotContain("f4-re-mcp", written[0], StringComparison.Ordinal);

            using JsonDocument document = JsonDocument.Parse(written[1]);
            Assert.False(document.RootElement.GetProperty("mcp")
                .GetProperty("f4-re-mcp").GetProperty("enabled").GetBoolean());
        }
        finally
        {
            AssistantCommandRunner.RunForTest = previous;
        }
    }

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
