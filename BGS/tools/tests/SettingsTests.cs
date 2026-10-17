using System;
using System.Diagnostics;
using System.IO;
using System.Threading.Tasks;
using BehaviourStudio.App;
using Xunit;

namespace BehaviourStudio.Tests;

public sealed class SettingsTests
{
    [Fact]
    public void MissingGameDataFolderIsIgnored()
    {
        string path = Path.Combine(Path.GetTempPath(), $"bgs-settings-{Guid.NewGuid():N}.cfg");
        string missing = Path.Combine(Path.GetTempPath(), $"bgs-data-missing-{Guid.NewGuid():N}");
        string? previous = Settings.SettingsPathForTest;
        try
        {
            Settings.SettingsPathForTest = path;
            File.WriteAllText(path, "gameDataFolder=" + missing + Environment.NewLine);

            Assert.Equal("", Settings.Get("gameDataFolder"));
        }
        finally
        {
            Settings.SettingsPathForTest = previous;
            try { File.Delete(path); }
            catch (IOException) { }
            catch (UnauthorizedAccessException) { }
        }
    }

    [Fact]
    public void SaveRetriesWhileTheSettingsFileIsLocked()
    {
        if (!OperatingSystem.IsWindows()) return;

        string path = Path.Combine(Path.GetTempPath(), $"bgs-settings-{Guid.NewGuid():N}.cfg");
        string? previous = Settings.SettingsPathForTest;
        Func<string, string[]> previousReader = Settings.ReadAllLinesForTest;
        try
        {
            Settings.SettingsPathForTest = path;
            Assert.True(Settings.TrySet("first", "1", out _));

            Settings.ReadAllLinesForTest = readPath =>
            {
                string[] lines = File.ReadAllLines(readPath);
                var held = new FileStream(readPath, FileMode.Open, FileAccess.ReadWrite, FileShare.None);
                _ = Task.Delay(100).ContinueWith(_ => held.Dispose());
                return lines;
            };
            bool saved = Settings.TrySet("second", "2", out string failure);
            Settings.ReadAllLinesForTest = previousReader;

            Assert.True(saved, failure);
            Assert.Equal("2", Settings.Get("second"));
        }
        finally
        {
            Settings.ReadAllLinesForTest = previousReader;
            Settings.SettingsPathForTest = previous;
            try { File.Delete(path); }
            catch (IOException) { }
            catch (UnauthorizedAccessException) { }
        }
    }

    [Fact]
    public void SaveGivesUpAfterBoundedRetriesAndKeepsTheOldFile()
    {
        if (!OperatingSystem.IsWindows()) return;

        string path = Path.Combine(Path.GetTempPath(), $"bgs-settings-{Guid.NewGuid():N}.cfg");
        string? previous = Settings.SettingsPathForTest;
        try
        {
            Settings.SettingsPathForTest = path;
            Assert.True(Settings.TrySet("first", "1", out _));
            File.SetAttributes(path, FileAttributes.ReadOnly);

            var clock = Stopwatch.StartNew();
            bool saved = Settings.TrySet("second", "2", out string failure);
            clock.Stop();

            Assert.False(saved);
            Assert.False(string.IsNullOrEmpty(failure));
            Assert.InRange(clock.ElapsedMilliseconds, 300L, 5000L);
            string contents = File.ReadAllText(path);
            Assert.Contains("first=1", contents, StringComparison.Ordinal);
            Assert.DoesNotContain("second=2", contents, StringComparison.Ordinal);
            Assert.Empty(Directory.GetFiles(Path.GetDirectoryName(path)!, Path.GetFileName(path) + ".*.tmp"));
        }
        finally
        {
            Settings.SettingsPathForTest = previous;
            try
            {
                File.SetAttributes(path, FileAttributes.Normal);
                File.Delete(path);
            }
            catch (IOException) { }
            catch (UnauthorizedAccessException) { }
        }
    }
}
