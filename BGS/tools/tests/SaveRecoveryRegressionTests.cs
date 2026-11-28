using System;
using System.IO;
using System.Linq;
using OpenCommonwealth.Services.Hkx;
using Xunit;

namespace BehaviourStudio.Tests;

public sealed class SaveRecoveryRegressionTests
{
    [Fact]
    public void ConcurrentRecoveryDoesNotConsumeALivePreviousSidecar()
    {
        using var root = new TestArchives.TempDirectory("bgs-live-sidecar");
        string path = Path.Combine(root.Path, "graph.hkx");
        byte[] original = { 1, 2, 3, 4 };
        byte[] replacement = { 5, 6, 7, 8 };
        File.WriteAllBytes(path, original);
        File.SetLastWriteTimeUtc(path, DateTime.UtcNow - TimeSpan.FromDays(3));
        var stamp = DocumentSourceStamp.Capture(path);

        Exception? error = Record.Exception(() =>
            FileSafety.ReplaceChecked(
                path,
                replacement,
                stamp,
                afterReplace: () =>
                {
                    Assert.Single(Directory.EnumerateFiles(root.Path, "*.previous"));
                    Assert.Single(Directory.EnumerateFiles(root.Path, "*.saving"));
                    FileSafety.RecoverInterrupted(path);
                    Assert.Single(Directory.EnumerateFiles(root.Path, "*.previous"));
                }));

        Assert.Null(error);
        Assert.Equal(replacement, File.ReadAllBytes(path));
        Assert.Equal(original, File.ReadAllBytes(path + ".bak"));
        Assert.Empty(Directory.EnumerateFiles(root.Path, "*.previous"));
        Assert.Empty(Directory.EnumerateFiles(root.Path, "*.saving"));
    }

    [Fact]
    public void RecoveryRestoresTheNewestStalePreviousWhenTheTargetIsMissing()
    {
        using var root = new TestArchives.TempDirectory("bgs-missing-target");
        string path = Path.Combine(root.Path, "graph.hkx");
        string older = path + "." + new string('a', 64) + "." + new string('3', 32) + ".previous";
        string newer = path + "." + new string('b', 64) + "." + new string('2', 32) + ".previous";
        File.WriteAllBytes(older, new byte[] { 1, 2, 3, 4 });
        File.WriteAllBytes(newer, new byte[] { 5, 6, 7, 8 });
        File.SetLastWriteTimeUtc(older, DateTime.UtcNow - TimeSpan.FromMinutes(30));
        File.SetLastWriteTimeUtc(newer, DateTime.UtcNow - TimeSpan.FromMinutes(20));

        FileSafety.RecoverInterrupted(path);

        Assert.Equal(new byte[] { 5, 6, 7, 8 }, File.ReadAllBytes(path));
        Assert.True(File.Exists(older));
        Assert.False(File.Exists(newer));
    }

    [Fact]
    public void ABackupThatCannotBeRotatedDoesNotFailAnAlreadyWrittenSave()
    {
        using var root = new TestArchives.TempDirectory("bgs-rotation-failure");
        string path = Path.Combine(root.Path, "graph.hkx");
        byte[] original = { 1, 2, 3, 4 };
        byte[] replacement = { 5, 6, 7, 8 };
        File.WriteAllBytes(path, original);
        var stamp = DocumentSourceStamp.Capture(path);

        File.WriteAllBytes(path + ".bak", new byte[] { 9 });
        Directory.CreateDirectory(path + ".bak.1");

        Exception? error = Record.Exception(() =>
            FileSafety.ReplaceChecked(path, replacement, stamp));

        Assert.Null(error);
        Assert.Equal(replacement, File.ReadAllBytes(path));
        Assert.Single(Directory.EnumerateFiles(root.Path, "*.previous"));
    }

    [Fact]
    public void RecoveryLeavesSiblingsThatAreNotGeneratedArtifactsAlone()
    {
        using var root = new TestArchives.TempDirectory("bgs-foreign-siblings");
        string path = Path.Combine(root.Path, "graph.hkx");
        File.WriteAllBytes(path, new byte[] { 1, 2, 3, 4 });

        var foreign = new[]
        {
            path + ".manual.previous",
            path + ".manual.writing",
            path + ".manual.saving",
            path + "." + new string('z', 64) + "." + new string('z', 32) + ".previous",
            path + "." + new string('a', 63) + "." + new string('b', 32) + ".previous",
        };
        foreach (string file in foreign)
        {
            File.WriteAllBytes(file, new byte[] { 42 });
            File.SetLastWriteTimeUtc(file, DateTime.UtcNow - TimeSpan.FromDays(1));
        }

        FileSafety.RecoverInterrupted(path);

        foreach (string file in foreign)
            Assert.True(File.Exists(file), $"{Path.GetFileName(file)} was consumed by save recovery");
        Assert.False(File.Exists(path + ".bak"));
    }

    [Fact]
    public void RecoveryLeavesAForeignSiblingWhenTheTargetIsMissing()
    {
        using var root = new TestArchives.TempDirectory("bgs-foreign-missing-target");
        string path = Path.Combine(root.Path, "graph.hkx");
        string foreign = path + ".manual.previous";
        File.WriteAllBytes(foreign, new byte[] { 42 });
        File.SetLastWriteTimeUtc(foreign, DateTime.UtcNow - TimeSpan.FromDays(1));

        FileSafety.RecoverInterrupted(path);

        Assert.True(File.Exists(foreign));
        Assert.False(File.Exists(path));
    }

    [Fact]
    public void SavingAFileWhoseTargetVanishedRecoversItFirst()
    {
        using var root = new TestArchives.TempDirectory("bgs-missing-target-save");
        string path = Path.Combine(root.Path, "graph.hkx");
        string previous = path + "." + new string('c', 64) + "." + new string('d', 32) + ".previous";
        byte[] abandoned = { 1, 2, 3, 4 };
        File.WriteAllBytes(previous, abandoned);
        File.SetLastWriteTimeUtc(previous, DateTime.UtcNow - TimeSpan.FromMinutes(30));

        var result = DocumentSaveTransaction.Commit(path, "<hkpackfile/>", "<hkpackfile/>", null);

        Assert.False(result.Committed);
        Assert.Equal(abandoned, File.ReadAllBytes(path));
    }

    [Fact]
    public void RecoveryKeepsAnUnmatchedDisplacedFileWhenTheTargetChangedAgain()
    {
        using var root = new TestArchives.TempDirectory("bgs-displaced-later-edit");
        string path = Path.Combine(root.Path, "graph.hkx");
        byte[] displaced = { 9, 9, 9, 9 };
        byte[] later = { 1, 1, 1, 1 };
        byte[] replacement = { 5, 6, 7, 8 };
        File.WriteAllBytes(path, later);
        string previous = path + "." + DocumentSourceStamp.Capture(replacement).Token + "." +
                          new string('3', 32) + ".previous";
        File.WriteAllBytes(previous, displaced);
        File.SetLastWriteTimeUtc(previous, DateTime.UtcNow - TimeSpan.FromMinutes(30));

        string[] kept = FileSafety.RecoverInterrupted(path);

        Assert.Equal(displaced, File.ReadAllBytes(previous));
        Assert.Equal(later, File.ReadAllBytes(path));
        Assert.Single(kept);
        Assert.Equal(Path.GetFileName(previous), Path.GetFileName(kept[0]));
        Assert.False(File.Exists(path + ".bak"));
    }

    [Fact]
    public void CheckedRestoreRefusesWhenTheDestinationChangedAfterTheCheck()
    {
        using var root = new TestArchives.TempDirectory("bgs-restore-race");
        string path = Path.Combine(root.Path, "graph.hkx");
        byte[] original = { 1, 2, 3, 4 };
        byte[] displaced = { 9, 10, 11, 12 };
        byte[] replacement = { 5, 6, 7, 8 };
        byte[] later = { 13, 14, 15, 16 };
        File.WriteAllBytes(path, original);
        var stamp = DocumentSourceStamp.Capture(path);

        var error = Assert.Throws<IOException>(() =>
            FileSafety.ReplaceChecked(
                path,
                replacement,
                stamp,
                beforeReplace: () => File.WriteAllBytes(path, displaced),
                beforeRestore: () => File.WriteAllBytes(path, later)));

        Assert.Contains("changed on disk", error.Message, StringComparison.OrdinalIgnoreCase);
        Assert.Equal(later, File.ReadAllBytes(path));
        Assert.Equal(displaced, File.ReadAllBytes(path + ".bak"));
        Assert.Empty(Directory.EnumerateFiles(root.Path, "*.previous"));
        Assert.Empty(Directory.EnumerateFiles(root.Path, "*.displaced"));
    }

    [Fact]
    public void AnEditSessionOpenedLaterDoesNotDeleteAnEarlierSessionsFiles()
    {
        string first = HkxTextEdit.CreateSessionDirectory();
        string second = HkxTextEdit.CreateSessionDirectory();
        try
        {
            File.WriteAllText(Path.Combine(first, "graph.xml"), "<first/>");
            string third = HkxTextEdit.CreateSessionDirectory();
            try
            {
                Assert.NotEqual(first, second);
                Assert.NotEqual(first, third);
                Assert.Equal("<first/>", File.ReadAllText(Path.Combine(first, "graph.xml")));
            }
            finally
            {
                Directory.Delete(third, true);
            }
        }
        finally
        {
            Directory.Delete(first, true);
            Directory.Delete(second, true);
        }
    }

    [Fact]
    public void ARefusedSaveStillNamesTheKeptPreviousCopies()
    {
        using var root = new TestArchives.TempDirectory("bgs-refused-kept");
        string path = Path.Combine(root.Path, "graph.hkx");
        File.WriteAllBytes(path, new byte[] { 1, 2, 3, 4 });
        string previous = path + "." + new string('c', 64) + "." + new string('d', 32) + ".previous";
        File.WriteAllBytes(previous, new byte[] { 7, 7 });
        File.SetLastWriteTimeUtc(previous, DateTime.UtcNow - TimeSpan.FromMinutes(30));

        var result = DocumentSaveTransaction.Commit(
            path, "<a/>", "<b/>", DocumentSourceStamp.Capture(new byte[] { 9, 9, 9 }));

        Assert.False(result.Committed);
        Assert.Contains(Path.GetFileName(previous), result.Message, StringComparison.Ordinal);
        Assert.True(File.Exists(previous));
    }

    [Fact]
    public void AFalseReloadAfterASaveIsReportedAsAFailure()
    {
        Assert.Null(BehaviourStudio.App.MainWindow.ReloadFailure(loaded: true, error: null));

        string? refused = BehaviourStudio.App.MainWindow.ReloadFailure(loaded: false, error: null);
        Assert.NotNull(refused);
        Assert.Contains("could not reload", refused, StringComparison.Ordinal);

        string? thrown = BehaviourStudio.App.MainWindow.ReloadFailure(
            loaded: true, error: new InvalidOperationException("injected"));
        Assert.NotNull(thrown);
        Assert.Contains("injected", thrown, StringComparison.Ordinal);
    }

    [Fact]
    public void AFileChangedDuringOpenBecomesReadOnlyWithItsReason()
    {
        using var root = new TestArchives.TempDirectory("bgs-open-change");
        string path = Path.Combine(root.Path, "graph.hkx");
        File.WriteAllBytes(path, new byte[] { 1, 2, 3, 4 });
        var opened = DocumentSourceStamp.Capture(new byte[] { 1, 2, 3, 4 });
        Assert.Null(BehaviourStudio.App.MainWindow.OpenedSourceChange(opened, path));

        File.WriteAllBytes(path, new byte[] { 5, 6, 7, 8 });
        string? change = BehaviourStudio.App.MainWindow.OpenedSourceChange(opened, path);
        Assert.NotNull(change);
        Assert.Contains("changed on disk", change, StringComparison.OrdinalIgnoreCase);
    }
}
