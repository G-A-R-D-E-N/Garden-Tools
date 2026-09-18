using System;
using System.IO;
using System.Text.Json;
using BehaviourStudio.App;

namespace BehaviourStudio.Tests;

internal static class OpencodeStubConfig
{
    internal static string Resolved(string configPath)
    {
        using JsonDocument document = JsonDocument.Parse(File.ReadAllText(configPath));
        JsonElement root = document.RootElement;
        return "{\"agent\":{\"bgs\":{\"mode\":\"primary\",\"permission\":" +
            root.GetProperty("agent").GetProperty("bgs").GetProperty("permission").GetRawText() +
            "}},\"mcp\":" + root.GetProperty("mcp").GetRawText() + "}";
    }

    internal static string AgentPermissionForTest()
    {
        using JsonDocument document = JsonDocument.Parse(
            OpencodeCli.BuildConfigJson("http://127.0.0.1:9/mcp", Array.Empty<string>()));
        return document.RootElement.GetProperty("agent").GetProperty("bgs")
            .GetProperty("permission").GetRawText();
    }
}
