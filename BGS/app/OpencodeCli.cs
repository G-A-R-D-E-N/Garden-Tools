using System;
using System.Collections.Generic;
using System.Text;
using System.Text.Json;

namespace BehaviourStudio.App;

public sealed record OpencodeTurn(
    string Text, IReadOnlyList<string> Tools, string Error, string StopReason)
{
    public bool Failed => Error.Length > 0;
}

public static class OpencodeCli
{
    public const int MaximumAnswerCharacters = 8000;

    internal static readonly string[] DeniedTools =
    {
        "bash", "edit", "webfetch", "websearch",
        "read", "glob", "grep", "task", "todowrite", "skill",
    };

    private static readonly JsonSerializerOptions Wire = new(JsonSerializerDefaults.Web);

    public static string BuildConfigJson(string bridgeUrl)
    {
        var permissions = new Dictionary<string, object?>(StringComparer.Ordinal);
        foreach (string tool in DeniedTools) permissions[tool] = "deny";
        permissions["bgs*"] = "allow";

        var config = new Dictionary<string, object?>(StringComparer.Ordinal)
        {
            ["permission"] = permissions,
            ["mcp"] = new Dictionary<string, object?>(StringComparer.Ordinal)
            {
                ["bgs"] = new Dictionary<string, object?>(StringComparer.Ordinal)
                {
                    ["type"] = "remote",
                    ["url"] = bridgeUrl,
                    ["enabled"] = true,
                },
            },
        };
        return JsonSerializer.Serialize(config, Wire);
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
