using System;
using System.Collections.Generic;
using System.Linq;
using System.Text.Json;
using BehaviourStudio.App;
using Xunit;

namespace BehaviourStudio.Tests;

public sealed class OpencodeCliTests
{
    [Fact]
    public void TheConfigDeniesEveryBuiltInToolAndEnablesOnlyTheBridge()
    {
        string json = OpencodeCli.BuildConfigJson("http://127.0.0.1:1234/mcp",
            new[] { "f4-re-mcp", "prisma-mcp" });
        using JsonDocument document = JsonDocument.Parse(json);
        JsonElement root = document.RootElement;

        JsonElement permissions = root.GetProperty("permission");
        foreach (string tool in OpencodeCli.DeniedTools)
            Assert.Equal("deny", permissions.GetProperty(tool).GetString());

        JsonElement servers = root.GetProperty("mcp");
        Assert.False(servers.GetProperty("f4-re-mcp").GetProperty("enabled").GetBoolean());
        Assert.False(servers.GetProperty("prisma-mcp").GetProperty("enabled").GetBoolean());

        JsonElement bridge = servers.GetProperty(OpencodeCli.ServerName);
        Assert.Equal("remote", bridge.GetProperty("type").GetString());
        Assert.Equal("http://127.0.0.1:1234/mcp", bridge.GetProperty("url").GetString());
        Assert.True(bridge.GetProperty("enabled").GetBoolean());
    }

    [Fact]
    public void TheBridgesOwnNameIsNeverDisabledByTheCallersList()
    {
        string json = OpencodeCli.BuildConfigJson("http://127.0.0.1:1/mcp",
            new[] { OpencodeCli.ServerName, "other" });
        using JsonDocument document = JsonDocument.Parse(json);
        JsonElement bridge = document.RootElement.GetProperty("mcp").GetProperty(OpencodeCli.ServerName);

        Assert.True(bridge.GetProperty("enabled").GetBoolean());
        Assert.Equal("remote", bridge.GetProperty("type").GetString());
    }

    [Fact]
    public void TheConfigSelectsADenyAllBgsAgent()
    {
        string json = OpencodeCli.BuildConfigJson(
            "http://127.0.0.1:1234/mcp", Array.Empty<string>());
        using JsonDocument document = JsonDocument.Parse(json);
        JsonElement agent = document.RootElement.GetProperty("agent")
            .GetProperty(OpencodeCli.AgentName);

        Assert.Equal("primary", agent.GetProperty("mode").GetString());
        JsonElement permission = agent.GetProperty("permission");
        Assert.Equal("deny", permission.GetProperty("*").GetString());
        Assert.Equal("allow", permission.GetProperty("bgs_*").GetString());
        foreach (string tool in OpencodeCli.AgentDeniedPermissions)
            Assert.Equal("deny", permission.GetProperty(tool).GetString());
    }

    [Fact]
    public void TheResolvedBoundaryAcceptsADenyAllBgsAgent()
    {
        string resolved = Resolved(
            OpencodeStubConfig.AgentPermissionForTest(),
            "{\"bgs\":{\"type\":\"remote\",\"url\":\"" + BridgeUrl +
            "\",\"enabled\":true},\"evil\":{\"type\":\"local\",\"enabled\":false}}");

        Assert.True(OpencodeCli.VerifyResolvedConfig(resolved, BridgeUrl));
    }

    [Fact]
    public void TheResolvedBoundaryRejectsAnAgentAllowOutsideBgs()
    {
        string resolved = Resolved(
            "{\"*\":\"deny\",\"question\":\"allow\",\"bgs_*\":\"allow\"}",
            "{\"bgs\":{\"type\":\"remote\",\"url\":\"" + BridgeUrl + "\",\"enabled\":true}}");

        Assert.False(OpencodeCli.VerifyResolvedConfig(resolved, BridgeUrl));
    }

    [Fact]
    public void TheResolvedBoundaryRejectsAReenabledForeignServer()
    {
        string resolved = Resolved(
            OpencodeStubConfig.AgentPermissionForTest(),
            "{\"bgs\":{\"type\":\"remote\",\"url\":\"" + BridgeUrl +
            "\",\"enabled\":true},\"evil\":{\"type\":\"local\",\"enabled\":true}}");

        Assert.False(OpencodeCli.VerifyResolvedConfig(resolved, BridgeUrl));
    }

    [Fact]
    public void TheResolvedBoundaryRejectsAMissingBgsAgent()
    {
        Assert.False(OpencodeCli.VerifyResolvedConfig(
            "{\"mcp\":{\"bgs\":{\"type\":\"remote\",\"url\":\"" + BridgeUrl + "\",\"enabled\":true}}}",
            BridgeUrl));
    }

    [Fact]
    public void TheResolvedBoundaryRejectsABridgeUrlMismatch()
    {
        string resolved = Resolved(
            OpencodeStubConfig.AgentPermissionForTest(),
            "{\"bgs\":{\"type\":\"remote\",\"url\":\"http://127.0.0.1:9/other\",\"enabled\":true}}");

        Assert.False(OpencodeCli.VerifyResolvedConfig(resolved, BridgeUrl));
    }

    [Fact]
    public void TheResolvedBoundaryRejectsUnreadableOutput()
    {
        Assert.False(OpencodeCli.VerifyResolvedConfig("not json", BridgeUrl));
        Assert.False(OpencodeCli.VerifyResolvedConfig("", BridgeUrl));
    }

    private const string BridgeUrl = "http://127.0.0.1:9/mcp";

    private static string Resolved(string agentPermission, string servers) =>
        "{\"agent\":{\"bgs\":{\"mode\":\"primary\",\"permission\":" + agentPermission +
        "}},\"mcp\":" + servers + "}";

    [Fact]
    public void ConfiguredServersAreReadFromTheResolvedConfiguration()
    {
        const string resolved =
            "{\"$schema\":\"https://opencode.ai/config.json\",\"permission\":{\"*\":\"allow\"}," +
            "\"model\":\"opencode/x\",\"mcp\":{\"ccassist\":{\"type\":\"remote\"}," +
            "\"f4-re-mcp\":{\"type\":\"local\"},\"prisma-mcp\":{\"type\":\"local\"}}}";

        Assert.True(OpencodeCli.TryParseConfiguredServers(resolved, out IReadOnlyList<string> servers));

        Assert.Equal(new[] { "ccassist", "f4-re-mcp", "prisma-mcp" }, servers);
    }

    [Fact]
    public void AConfigurationWithoutServersIsValidButAnUnreadableOneIsNot()
    {
        Assert.True(OpencodeCli.TryParseConfiguredServers("{}", out IReadOnlyList<string> none));
        Assert.Empty(none);
        Assert.False(OpencodeCli.TryParseConfiguredServers("not json", out _));
        Assert.False(OpencodeCli.TryParseConfiguredServers("{\"mcp\":[]}", out _));
        Assert.False(OpencodeCli.TryParseConfiguredServers("", out _));
    }

    [Fact]
    public void ParsingReadsTheAnswerFromTheTextEvents()
    {
        const string output =
            "{\"type\":\"step_start\",\"part\":{\"type\":\"step-start\"}}\n" +
            "{\"type\":\"text\",\"part\":{\"type\":\"text\",\"text\":\"pong\"}}\n" +
            "{\"type\":\"step_finish\",\"part\":{\"reason\":\"stop\"}}\n";

        OpencodeTurn turn = OpencodeCli.Parse(output);

        Assert.Equal("pong", turn.Text);
        Assert.False(turn.Failed);
        Assert.Equal("stop", turn.StopReason);
        Assert.Empty(turn.Tools);
    }

    [Fact]
    public void ParsingJoinsTextAcrossStepsAndRecordsToolUse()
    {
        const string output =
            "{\"type\":\"step_start\",\"part\":{}}\n" +
            "{\"type\":\"tool_use\",\"part\":{\"type\":\"tool\",\"tool\":\"bgs_inspect_object\"," +
            "\"state\":{\"status\":\"completed\"}}}\n" +
            "{\"type\":\"step_finish\",\"part\":{\"reason\":\"tool-calls\"}}\n" +
            "{\"type\":\"text\",\"part\":{\"text\":\"first \"}}\n" +
            "{\"type\":\"text\",\"part\":{\"text\":\"second\"}}\n" +
            "{\"type\":\"tool_use\",\"part\":{\"tool\":\"bgs_inspect_object\"}}\n" +
            "{\"type\":\"step_finish\",\"part\":{\"reason\":\"stop\"}}\n";

        OpencodeTurn turn = OpencodeCli.Parse(output);

        Assert.Equal("first second", turn.Text);
        Assert.Equal(new[] { "bgs_inspect_object" }, turn.Tools);
        Assert.Equal("stop", turn.StopReason);
    }

    [Fact]
    public void AnErrorEventIsReportedRatherThanSilentlyReturningNoAnswer()
    {
        const string output =
            "{\"type\":\"error\",\"error\":{\"name\":\"UnknownError\"," +
            "\"data\":{\"message\":\"Unexpected server error.\",\"ref\":\"err_1\"}}}\n";

        OpencodeTurn turn = OpencodeCli.Parse(output);

        Assert.True(turn.Failed);
        Assert.Equal("Unexpected server error.", turn.Error);
        Assert.Empty(turn.Text);
    }

    [Fact]
    public void NoiseAndMalformedLinesAreIgnoredInsteadOfFailingTheTurn()
    {
        const string output =
            "not json at all\n" +
            "\n" +
            "{\"type\":\"text\",\"part\":{\"text\":\"kept\"}}\n" +
            "{\"broken\"\n" +
            "[]\n";

        OpencodeTurn turn = OpencodeCli.Parse(output);

        Assert.Equal("kept", turn.Text);
        Assert.False(turn.Failed);
    }

    [Fact]
    public void AnEmptyStreamYieldsNoTextAndNoError()
    {
        OpencodeTurn turn = OpencodeCli.Parse("");

        Assert.Empty(turn.Text);
        Assert.False(turn.Failed);
        Assert.Empty(turn.Tools);
    }

    [Fact]
    public void CredentialShapedTextIsRedactedBeforeItReachesTheTranscript()
    {
        const string output =
            "{\"type\":\"text\",\"part\":{\"text\":\"token sk-abcdefghijklmnopqrstuv leaked\"}}\n";

        OpencodeTurn turn = OpencodeCli.Parse(output);

        Assert.DoesNotContain("sk-abcdefghijklmnopqrstuv", turn.Text, StringComparison.Ordinal);
        Assert.Contains("redacted", turn.Text, StringComparison.Ordinal);
    }

    [Fact]
    public async Task TheBridgeReportsWhichToolsWereInvoked()
    {
        using BgsMcpBridge bridge = BgsMcpBridge.Start(BgsMcpProtocolTests.Tools());
        var invoked = new List<string>();
        bridge.ToolInvoked = name => invoked.Add(name);

        using var client = new System.Net.Http.HttpClient();
        using var content = new System.Net.Http.StringContent(
            "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\"," +
            "\"params\":{\"name\":\"bgs_inspect_object\",\"arguments\":{\"objectId\":\"1\"}}}",
            System.Text.Encoding.UTF8, "application/json");
        using var response = await client.PostAsync(bridge.TokenQueryUrl(), content);

        Assert.Equal(System.Net.HttpStatusCode.OK, response.StatusCode);
        Assert.Equal(new[] { "bgs.inspect_object" }, invoked);
    }
}
