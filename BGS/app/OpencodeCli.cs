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

public static class OpencodeCli
{
    public const int MaximumAnswerCharacters = 8000;
    public const string ServerName = "bgs";
    public static readonly TimeSpan Timeout = TimeSpan.FromSeconds(20);

    internal static readonly string[] DeniedTools =
    {
        "bash", "edit", "webfetch", "websearch",
        "read", "glob", "grep", "task", "todowrite", "skill",
    };

    private static readonly JsonSerializerOptions Wire = new(JsonSerializerDefaults.Web);

    public static string BuildConfigJson(string bridgeUrl, IReadOnlyList<string> otherServers)
    {
        var permissions = new Dictionary<string, object?>(StringComparer.Ordinal);
        foreach (string tool in DeniedTools) permissions[tool] = "deny";
        permissions["bgs*"] = "allow";

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
            ["mcp"] = servers,
        };
        return JsonSerializer.Serialize(config, Wire);
    }

    public static IReadOnlyList<string> ParseConfiguredServers(string resolvedConfigJson)
    {
        JsonDocument document;
        try
        {
            document = JsonDocument.Parse(resolvedConfigJson);
        }
        catch (JsonException)
        {
            return Array.Empty<string>();
        }

        using (document)
        {
            JsonElement? servers = CodexProtocol.Property(document.RootElement, "mcp");
            if (servers is not { ValueKind: JsonValueKind.Object } map) return Array.Empty<string>();
            var names = new List<string>();
            foreach (JsonProperty property in map.EnumerateObject())
            {
                string name = CodexProtocol.Scrub(property.Name, 120);
                if (name.Length > 0) names.Add(name);
            }
            return names.AsReadOnly();
        }
    }

    public static async Task<IReadOnlyList<string>> ReadConfiguredServersAsync(
        AssistantCli cli, CancellationToken cancellationToken = default)
    {
        AssistantCommandResult result = await AssistantCommandRunner
            .RunAsync(cli, new[] { "debug", "config" }, Timeout, cancellationToken).ConfigureAwait(false);
        return result.Succeeded ? ParseConfiguredServers(result.StandardOutput) : Array.Empty<string>();
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
