using System;
using System.Collections.Generic;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;

namespace BehaviourStudio.App;

public sealed record OpencodeTurn(
    string Text, IReadOnlyList<string> Tools, string Error, string StopReason)
{
    public bool Failed => Error.Length > 0;
}

public sealed record OpencodeServerDiscovery(bool Ok, IReadOnlyList<string> Servers, string Error)
{
    public static readonly OpencodeServerDiscovery Failed = new(false, Array.Empty<string>(),
        "The configured opencode MCP servers could not be read, so BGS refused to start the session.");
}

public sealed record OpencodeConfigVerification(bool Ok, string Error)
{
    public static readonly OpencodeConfigVerification Refused = new(false,
        "The opencode permission boundary could not be verified, so BGS refused to run the turn.");
}

public static class OpencodeCli
{
    public const int MaximumAnswerCharacters = 8000;
    public const string ServerName = "bgs";
    public const string AgentName = "bgs";
    public const string AgentDescription = "BGS assistant agent. Only BGS tools are allowed.";
    public static readonly TimeSpan Timeout = TimeSpan.FromSeconds(20);

    internal static readonly string[] DeniedTools =
    {
        "bash", "edit", "webfetch", "websearch",
        "read", "glob", "grep", "task", "todowrite", "skill",
    };

    internal static readonly string[] AgentDeniedPermissions =
    {
        "read", "edit", "glob", "grep", "list", "bash", "task",
        "todowrite", "webfetch", "websearch", "lsp", "skill", "question",
    };

    private static readonly JsonSerializerOptions Wire = new(JsonSerializerDefaults.Web);

    public static string BuildConfigJson(string bridgeUrl, IReadOnlyList<string> otherServers)
    {
        var permissions = new Dictionary<string, object?>(StringComparer.Ordinal);
        foreach (string tool in DeniedTools) permissions[tool] = "deny";
        permissions["bgs*"] = "allow";

        var agentPermission = new Dictionary<string, object?>(StringComparer.Ordinal)
        {
            ["*"] = "deny",
        };
        foreach (string tool in AgentDeniedPermissions) agentPermission[tool] = "deny";
        agentPermission["bgs_*"] = "allow";

        var agents = new Dictionary<string, object?>(StringComparer.Ordinal)
        {
            [AgentName] = new Dictionary<string, object?>(StringComparer.Ordinal)
            {
                ["mode"] = "primary",
                ["description"] = AgentDescription,
                ["permission"] = agentPermission,
            },
        };

        var servers = new Dictionary<string, object?>(StringComparer.Ordinal);
        foreach (string name in otherServers)
        {
            if (name.Length == 0 || string.Equals(name, ServerName, StringComparison.Ordinal)) continue;
            servers[name] = new Dictionary<string, object?>(StringComparer.Ordinal) { ["enabled"] = false };
        }
        servers[ServerName] = new Dictionary<string, object?>(StringComparer.Ordinal)
        {
            ["type"] = "remote",
            ["url"] = bridgeUrl,
            ["enabled"] = true,
        };

        var config = new Dictionary<string, object?>(StringComparer.Ordinal)
        {
            ["permission"] = permissions,
            ["agent"] = agents,
            ["mcp"] = servers,
        };
        return JsonSerializer.Serialize(config, Wire);
    }

    public static async Task<OpencodeConfigVerification> VerifyEffectiveConfigAsync(
        AssistantCli cli, string configPath, string bridgeUrl,
        CancellationToken cancellationToken = default)
    {
        AssistantCommandResult result = await AssistantCommandRunner.RunAsync(
            cli,
            new[] { "debug", "config" },
            new Dictionary<string, string>(StringComparer.Ordinal) { ["OPENCODE_CONFIG"] = configPath },
            Timeout, cancellationToken).ConfigureAwait(false);
        if (!result.Succeeded) return OpencodeConfigVerification.Refused;
        return VerifyResolvedConfig(result.StandardOutput, bridgeUrl)
            ? new OpencodeConfigVerification(true, "")
            : OpencodeConfigVerification.Refused;
    }

    internal static bool VerifyResolvedConfig(string resolvedConfigJson, string bridgeUrl)
    {
        if (string.IsNullOrWhiteSpace(resolvedConfigJson)) return false;

        JsonDocument document;
        try
        {
            document = JsonDocument.Parse(resolvedConfigJson);
        }
        catch (JsonException)
        {
            return false;
        }

        using (document)
        {
            if (document.RootElement.ValueKind != JsonValueKind.Object) return false;
            if (CodexProtocol.Property(document.RootElement, "agent") is not
                { ValueKind: JsonValueKind.Object } agents) return false;
            if (CodexProtocol.Property(agents, AgentName) is not
                { ValueKind: JsonValueKind.Object } agent) return false;
            if (CodexProtocol.String(agent, "mode") != "primary") return false;
            if (CodexProtocol.Property(agent, "permission") is not
                { ValueKind: JsonValueKind.Object } permission) return false;
            if (!AgentPermissionIsDenyAllExceptBgs(permission)) return false;
            if (CodexProtocol.Property(document.RootElement, "mcp") is not
                { ValueKind: JsonValueKind.Object } servers) return false;
            return McpBoundaryHolds(servers, bridgeUrl);
        }
    }

    private static bool AgentPermissionIsDenyAllExceptBgs(JsonElement permission)
    {
        foreach (JsonProperty property in permission.EnumerateObject())
        {
            if (string.Equals(property.Name, "bgs_*", StringComparison.Ordinal))
            {
                if (property.Value.ValueKind != JsonValueKind.String ||
                    property.Value.GetString() != "allow") return false;
                continue;
            }
            if (!SubtreeDeniesAll(property.Value)) return false;
        }
        return true;
    }

    private static bool SubtreeDeniesAll(JsonElement node)
    {
        if (node.ValueKind == JsonValueKind.String) return node.GetString() == "deny";
        if (node.ValueKind != JsonValueKind.Object) return false;
        foreach (JsonProperty property in node.EnumerateObject())
            if (!SubtreeDeniesAll(property.Value)) return false;
        return true;
    }

    private static bool McpBoundaryHolds(JsonElement servers, string bridgeUrl)
    {
        foreach (JsonProperty property in servers.EnumerateObject())
        {
            if (string.Equals(property.Name, ServerName, StringComparison.Ordinal))
            {
                if (property.Value.ValueKind != JsonValueKind.Object) return false;
                if (CodexProtocol.String(property.Value, "type") != "remote") return false;
                if (CodexProtocol.String(property.Value, "url") != bridgeUrl) return false;
                if (CodexProtocol.Property(property.Value, "enabled") is not
                    { ValueKind: JsonValueKind.True }) return false;
                continue;
            }
            if (property.Value.ValueKind != JsonValueKind.Object) return false;
            if (CodexProtocol.Property(property.Value, "enabled") is not
                { ValueKind: JsonValueKind.False }) return false;
        }
        return true;
    }

    public static bool TryParseConfiguredServers(string resolvedConfigJson, out IReadOnlyList<string> servers)
    {
        servers = Array.Empty<string>();
        if (string.IsNullOrWhiteSpace(resolvedConfigJson)) return false;

        JsonDocument document;
        try
        {
            document = JsonDocument.Parse(resolvedConfigJson);
        }
        catch (JsonException)
        {
            return false;
        }

        using (document)
        {
            if (document.RootElement.ValueKind != JsonValueKind.Object) return false;
            JsonElement? map = CodexProtocol.Property(document.RootElement, "mcp");
            if (map is null) return true;
            if (map is not { ValueKind: JsonValueKind.Object } serversMap) return false;

            var names = new List<string>();
            foreach (JsonProperty property in serversMap.EnumerateObject())
            {
                string name = CodexProtocol.Scrub(property.Name, 120);
                if (name.Length > 0) names.Add(name);
            }
            servers = names.AsReadOnly();
            return true;
        }
    }

    public static async Task<OpencodeServerDiscovery> ReadConfiguredServersAsync(
        AssistantCli cli, CancellationToken cancellationToken = default)
    {
        AssistantCommandResult result = await AssistantCommandRunner
            .RunAsync(cli, new[] { "debug", "config" }, Timeout, cancellationToken).ConfigureAwait(false);
        if (!result.Succeeded) return OpencodeServerDiscovery.Failed;
        return TryParseConfiguredServers(result.StandardOutput, out IReadOnlyList<string> servers)
            ? new OpencodeServerDiscovery(true, servers, "")
            : OpencodeServerDiscovery.Failed;
    }

    public static OpencodeTurn Parse(string output)
    {
        var text = new StringBuilder();
        var tools = new List<string>();
        string error = "";
        string stopReason = "";

        foreach (string line in output.Split('\n'))
        {
            string trimmed = line.Trim();
            if (trimmed.Length == 0 || trimmed[0] != '{') continue;

            JsonDocument document;
            try
            {
                document = JsonDocument.Parse(trimmed);
            }
            catch (JsonException)
            {
                continue;
            }

            using (document)
            {
                JsonElement root = document.RootElement;
                if (root.ValueKind != JsonValueKind.Object) continue;
                string type = CodexProtocol.String(root, "type");
                JsonElement? part = CodexProtocol.Property(root, "part");
                switch (type)
                {
                    case "text":
                        text.Append(CodexProtocol.String(part, "text"));
                        break;
                    case "tool_use":
                        string tool = CodexProtocol.String(part, "tool");
                        if (tool.Length > 0 && !tools.Contains(tool)) tools.Add(tool);
                        break;
                    case "step_finish":
                        stopReason = CodexProtocol.String(part, "reason");
                        break;
                    case "error":
                        JsonElement? errorData =
                            CodexProtocol.Property(CodexProtocol.Property(root, "error"), "data");
                        error = CodexProtocol.Scrub(CodexProtocol.String(errorData, "message"), 300);
                        if (error.Length == 0) error = "opencode reported an error.";
                        break;
                }
            }
        }

        return new OpencodeTurn(
            CodexProtocol.Scrub(text.ToString().Trim(), MaximumAnswerCharacters),
            tools.AsReadOnly(), error, stopReason);
    }
}
