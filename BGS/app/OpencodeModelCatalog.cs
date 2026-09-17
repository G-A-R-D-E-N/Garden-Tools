using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;

namespace BehaviourStudio.App;

public static class OpencodeModelCatalog
{
    public static readonly TimeSpan Timeout = TimeSpan.FromSeconds(20);

    public static async Task<IReadOnlyList<CodexModel>> ReadAsync(
        AssistantCli cli, CancellationToken cancellationToken = default)
    {
        AssistantCommandResult result = await AssistantCommandRunner
            .RunAsync(cli, new[] { "models" }, Timeout, cancellationToken).ConfigureAwait(false);
        if (!result.Succeeded) return Array.Empty<CodexModel>();
        return Parse(result.StandardOutput);
    }

    public static IReadOnlyList<CodexModel> Parse(string output)
    {
        var models = new List<CodexModel>();
        var seen = new HashSet<string>(StringComparer.Ordinal);
        foreach (string raw in output.Split('\n'))
        {
            string id = raw.Trim().TrimEnd('\r');
            if (!IsModelId(id) || !seen.Add(id)) continue;
            models.Add(new CodexModel(id, id, false, id));
        }
        return models.AsReadOnly();
    }

    internal static bool IsModelId(string value)
    {
        if (value.Length == 0 || value.Length > 200) return false;
        int separator = value.IndexOf('/');
        if (separator <= 0 || separator != value.LastIndexOf('/')) return false;
        if (separator == value.Length - 1) return false;
        foreach (char character in value)
        {
            if (char.IsControl(character) || char.IsWhiteSpace(character)) return false;
        }
        return true;
    }
}
