using System;
using System.IO;
using System.Linq;
using System.Text;
using OpenCommonwealth.Services.Archive;
using Xunit;

namespace BehaviourStudio.Tests;

public sealed class GameDataPluginOrderTests
{
    [Fact]
    public void PluginListOrdersBaseArchivesWithRadicleRules()
    {
        string root = Path.Combine(Path.GetTempPath(), "bgs-plugin-order-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(root);
        try
        {
            WriteArchives(root);
            string plugins = Path.Combine(root, "plugins.txt");
            File.WriteAllText(plugins, "# comment\n*C.esm\nB.esm\n*C.esm\n*A.esm\n");

            using var data = GameData.Discover(root, plugins);

            Assert.Equal(
                new[] { "A - Animations.ba2", "B - Animations.ba2", "C - Animations.ba2" },
                data.ArchivePaths.Select(path => Path.GetFileName(path)));
        }
        finally { Directory.Delete(root, recursive: true); }
    }

    [Fact]
    public void PluginListBomOnFirstLineIsNotAStar()
    {
        string root = Path.Combine(Path.GetTempPath(), "bgs-plugin-order-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(root);
        try
        {
            WriteArchives(root);
            string plugins = Path.Combine(root, "plugins.txt");
            File.WriteAllText(plugins, "*B.esp\r\n*A.esp\r\n", new UTF8Encoding(encoderShouldEmitUTF8Identifier: true));

            using var data = GameData.Discover(root, plugins);

            Assert.Equal(
                new[] { "A - Animations.ba2", "B - Animations.ba2", "C - Animations.ba2" },
                data.ArchivePaths.Select(path => Path.GetFileName(path)));
        }
        finally { Directory.Delete(root, recursive: true); }
    }

    [Fact]
    public void UnreadablePluginListFallsBackToTheUnorderedArchiveList()
    {
        if (!OperatingSystem.IsWindows()) return;

        string root = Path.Combine(Path.GetTempPath(), "bgs-plugin-order-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(root);
        try
        {
            WriteArchives(root);
            string plugins = Path.Combine(root, "plugins.txt");
            File.WriteAllText(plugins, "*C.esm\n*B.esm\n*A.esm\n");

            using var held = new FileStream(plugins, FileMode.Open, FileAccess.Read, FileShare.None);
            using var data = GameData.Discover(root, plugins);

            Assert.Equal(
                new[] { "A - Animations.ba2", "B - Animations.ba2", "C - Animations.ba2" },
                data.ArchivePaths.Select(path => Path.GetFileName(path)));
        }
        finally { Directory.Delete(root, recursive: true); }
    }

    private static void WriteArchives(string root)
    {
        foreach (string name in new[] { "A - Animations.ba2", "B - Animations.ba2", "C - Animations.ba2" })
            File.WriteAllBytes(Path.Combine(root, name), Array.Empty<byte>());
    }
}
