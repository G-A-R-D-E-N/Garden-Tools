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
            new[] { "f4-re-mcp", "prisma-mcp" }, OpencodeStubConfig.Advertised);
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
            new[] { OpencodeCli.ServerName, "other" }, OpencodeStubConfig.Advertised);
        using JsonDocument document = JsonDocument.Parse(json);
        JsonElement bridge = document.RootElement.GetProperty("mcp").GetProperty(OpencodeCli.ServerName);

        Assert.True(bridge.GetProperty("enabled").GetBoolean());
        Assert.Equal("remote", bridge.GetProperty("type").GetString());
    }

    [Fact]
    public void TheConfigAllowsOnlyTheExactCallableBgsTools()
    {
        string json = OpencodeCli.BuildConfigJson(
            "http://127.0.0.1:1234/mcp", Array.Empty<string>(), OpencodeStubConfig.Advertised);
        using JsonDocument document = JsonDocument.Parse(json);
        JsonElement agent = document.RootElement.GetProperty("agent")
            .GetProperty(OpencodeCli.AgentName);

        Assert.Equal("primary", agent.GetProperty("mode").GetString());
        JsonElement permission = agent.GetProperty("permission");
        Assert.Equal("deny", permission.GetProperty("*").GetString());
        foreach (string tool in OpencodeCli.AgentDeniedPermissions)
            Assert.Equal("deny", permission.GetProperty(tool).GetString());

        string[] allowed = permission.EnumerateObject()
            .Where(property => property.Value.ValueKind == JsonValueKind.String &&
                property.Value.GetString() == "allow")
            .Select(property => property.Name)
            .ToArray();
        Assert.Equal(OpencodeCli.CallableToolNames(OpencodeStubConfig.Advertised), allowed);
        foreach (string tool in allowed)
        {
            Assert.StartsWith(OpencodeCli.ServerName + "_", tool, StringComparison.Ordinal);
            Assert.DoesNotContain("*", tool, StringComparison.Ordinal);
        }
    }

    [Fact]
    public void TheCallableNamesPrefixTheServerKeyOntoTheAdvertisedNames()
    {
        Assert.Equal(new[]
            {
                "bgs_bgs_inspect_behavior", "bgs_bgs_resolve_project_chain", "bgs_bgs_check_project",
                "bgs_bgs_search_project", "bgs_bgs_inspect_animation", "bgs_bgs_inspect_object",
                "bgs_bgs_set_clip_animation",
            },
            OpencodeCli.CallableToolNames(OpencodeStubConfig.Advertised));
    }

    [Fact]
    public void TheResolvedBoundaryAcceptsADenyAllBgsAgent()
    {
        string resolved = Resolved(
            OpencodeStubConfig.AgentPermissionForTest(),
            "{\"bgs\":{\"type\":\"remote\",\"url\":\"" + BridgeUrl +
            "\",\"enabled\":true},\"evil\":{\"type\":\"local\",\"enabled\":false}}");

        Assert.True(OpencodeCli.VerifyResolvedConfig(resolved, BridgeUrl, OpencodeStubConfig.Advertised));
    }

    private const string BridgeUrl = "http://127.0.0.1:9/mcp";
    private const string BgsServers = "{\"bgs\":{\"type\":\"remote\",\"url\":\"" + BridgeUrl + "\",\"enabled\":true}}";

    public static TheoryData<string, string> UnsafeResolvedBoundaries() => new()
    {
        { "{\"*\":\"deny\",\"question\":\"allow\",\"bgs_bgs_inspect_object\":\"allow\"}", BgsServers },
        { "{\"*\":\"deny\",\"bgs_*\":\"allow\"}", BgsServers },
        { OpencodeStubConfig.AgentPermissionForTest().Replace("\"*\":\"deny\",", ""), BgsServers },
        { OpencodeStubConfig.AgentPermissionForTest().Replace("\"*\":\"deny\"", "\"*\":\"deny\",\"bgs_evil\":\"allow\""), BgsServers },
        { OpencodeStubConfig.AgentPermissionForTest().Replace("\"bgs_bgs_inspect_object\":\"allow\",", ""), BgsServers },
        { OpencodeStubConfig.AgentPermissionForTest(), "{\"bgs\":{\"type\":\"remote\",\"url\":\"" + BridgeUrl + "\",\"enabled\":true},\"evil\":{\"type\":\"local\",\"enabled\":true}}" },
        { OpencodeStubConfig.AgentPermissionForTest(), "{\"bgs\":{\"type\":\"remote\",\"url\":\"http://127.0.0.1:9/other\",\"enabled\":true}}" },
        { OpencodeStubConfig.AgentPermissionForTest(), "{}" },
        { OpencodeStubConfig.AgentPermissionForTest(), "{\"other\":{\"type\":\"local\",\"enabled\":false}}" },
    };

    [Theory]
    [MemberData(nameof(UnsafeResolvedBoundaries))]
    public void TheResolvedBoundaryRejectsAnUnsafeConfig(string agentPermission, string servers)
    {
        string resolved = Resolved(agentPermission, servers);

        Assert.False(OpencodeCli.VerifyResolvedConfig(resolved, BridgeUrl, OpencodeStubConfig.Advertised));
    }

    [Theory]
    [InlineData("not json")]
    [InlineData("")]
    [InlineData("{\"mcp\":{\"bgs\":{\"type\":\"remote\",\"url\":\"" + BridgeUrl + "\",\"enabled\":true}}}")]
    public void TheResolvedBoundaryRejectsOutputWithoutAUsableConfig(string output)
    {
        Assert.False(OpencodeCli.VerifyResolvedConfig(output, BridgeUrl, OpencodeStubConfig.Advertised));
    }

    private static string Resolved(string agentPermission, string servers) =>
        "{\"permission\":" + OpencodeStubConfig.TopLevelPermissionForTest() +
        ",\"agent\":{\"bgs\":{\"mode\":\"primary\",\"permission\":" + agentPermission +
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
