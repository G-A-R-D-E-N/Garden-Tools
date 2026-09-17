using System;
using System.Collections.Generic;
using System.IO;

namespace BehaviourStudio.App;

public static class AssistantCommandCatalog
{
    internal static Func<string, IReadOnlyList<AssistantCommand>> ProviderCommandsForTest =
        ReadProviderCommands;

    public static IReadOnlyList<AssistantCommand> For(AssistantBackend backend)
    {
        IReadOnlyList<AssistantCommand> provider = ProviderCommands(backend);
        if (provider.Count == 0) return AssistantCommands.All;
        var all = new List<AssistantCommand>(AssistantCommands.All.Count + provider.Count);
        all.AddRange(AssistantCommands.All);
        all.AddRange(provider);
        return all.AsReadOnly();
    }

    public static IReadOnlyList<AssistantCommand> ProviderCommands(AssistantBackend backend) =>
        backend == AssistantBackend.Opencode
            ? ProviderCommandsForTest(OpencodeCommandFolder())
            : Array.Empty<AssistantCommand>();

    public static string OpencodeCommandFolder()
    {
        string home = Environment.GetFolderPath(Environment.SpecialFolder.UserProfile);
        if (home.Length == 0) return "";
        string configured = Environment.GetEnvironmentVariable("XDG_CONFIG_HOME") ?? "";
        string root = configured.Length > 0 ? configured : Path.Combine(home, ".config");
        return Path.Combine(root, "opencode", "command");
    }

    public static IReadOnlyList<AssistantCommand> ReadProviderCommands(string folder)
    {
        if (folder.Length == 0 || !Directory.Exists(folder)) return Array.Empty<AssistantCommand>();
        var commands = new List<AssistantCommand>();
        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        try
        {
            var files = new List<string>(Directory.GetFiles(folder, "*.md"));
            files.Sort(StringComparer.OrdinalIgnoreCase);
            foreach (string file in files)
            {
                string name = Path.GetFileNameWithoutExtension(file).Trim();
                if (name.Length == 0 || !seen.Add(name)) continue;
                commands.Add(new AssistantCommand(name, ReadDescription(file)));
            }
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            return Array.Empty<AssistantCommand>();
        }
        return commands.AsReadOnly();
    }

    private static string ReadDescription(string file)
    {
        try
        {
            int index = 0;
            foreach (string line in File.ReadLines(file))
            {
                if (++index > 12) break;
                string trimmed = line.Trim();
                if (!trimmed.StartsWith("description:", StringComparison.OrdinalIgnoreCase)) continue;
                return trimmed["description:".Length..].Trim().Trim('"', '\'');
            }
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
        }
        return "";
    }
}
