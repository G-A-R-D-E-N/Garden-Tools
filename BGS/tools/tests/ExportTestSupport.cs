using System;
using System.IO;
using System.Linq;
using System.Text.Json;
using Xunit;

namespace BehaviourStudio.Tests;

internal static class ExportTestSupport
{
    public static string VanillaRoot => Path.Combine(AppContext.BaseDirectory, "fixtures/vanilla");

    public static string Temp(string label)
    {
        string directory = Path.Combine(Path.GetTempPath(), $"bgs-export-{label}-{Guid.NewGuid():N}");
        Directory.CreateDirectory(directory);
        return directory;
    }

    public static string[] Files(string root) =>
        Directory.GetFiles(root, "*", SearchOption.AllDirectories)
            .Select(path => Path.GetRelativePath(root, path).Replace('\\', '/'))
            .OrderBy(path => path, StringComparer.Ordinal)
            .ToArray();

    public static (string Path, string Sha256, string Fidelity)[] Manifest()
    {
        string text = File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "app.Config", "manifest.json"));
        Assert.Equal(text, File.ReadAllText(Path.Combine(VanillaRoot, "manifest.json")));

        using var document = JsonDocument.Parse(text);
        return document.RootElement.GetProperty("files").EnumerateArray()
            .Select(entry => (
                entry.GetProperty("path").GetString()!,
                entry.GetProperty("sha256").GetString()!,
                entry.GetProperty("fidelity").GetString()!))
            .ToArray();
    }
}
