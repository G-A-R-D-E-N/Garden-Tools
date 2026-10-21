using System;
using System.IO;
using System.Text;
using OpenCommonwealth.Services.Archive;
using Radicle.Interop;
using Xunit;

namespace BehaviourStudio.Tests;

[Collection("ScanCli")]
public sealed class Ba2RadicleTests
{
    [Fact]
    public void NativeLoadFailureIncludesRadicleAbiMismatch()
    {
        var mismatch = new RadicleAbiMismatchException("stale radicle.dll");
        Assert.True(Ba2.IsNativeLoadFailure(mismatch));
        Assert.True(Ba2.IsNativeLoadFailure(new TypeInitializationException("Ba2Archive", mismatch)));
        Assert.False(Ba2.IsNativeLoadFailure(new InvalidOperationException("unrelated")));
    }

    [Fact]
    public void GameDataArchiveFilterPropagatesWrappedRadicleAbiMismatch()
    {
        var wrapped = new InvalidDataException("wrapped", new RadicleAbiMismatchException("stale radicle.dll"));
        Assert.False(GameData.IsSkippableArchiveFailure(wrapped));
        Assert.True(GameData.IsSkippableArchiveFailure(new InvalidDataException("unreadable archive")));
    }

    [Fact]
    public void Ba2WrapperReadsGeneralArchivesThroughRadicleAndRefusesTextureArchives()
    {
        string root = Path.Combine(Path.GetTempPath(), "bgs-ba2-radicle-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(root);
        try
        {
            byte[] payload = Encoding.ASCII.GetBytes("tiny hkx");
            string archive = Path.Combine(root, "tiny.ba2");
            WriteGeneralBa2(archive, "meshes\\actors/tiny.hkx", payload);

            using (var ba2 = Ba2.Open(archive))
            {
                var entry = Assert.Single(ba2.Entries);
                Assert.Equal("meshes/actors/tiny.hkx", entry.Name);
                Assert.Equal("tiny.hkx", entry.FileName);
                Assert.Equal(payload, ba2.Read(entry));
            }

            string output = Path.Combine(root, "out");
            Assert.Equal(1, Ba2.ExtractMatching(archive, "tiny", output, ".hkx", _ => { }, keepFolders: true));
            Assert.Equal(payload, File.ReadAllBytes(Path.Combine(output, "meshes", "actors", "tiny.hkx")));

            string textures = Path.Combine(root, "textures.ba2");
            using (var stream = File.Create(textures))
            using (var writer = new BinaryWriter(stream))
            {
                writer.Write(Encoding.ASCII.GetBytes("BTDX"));
                writer.Write((uint)1);
                writer.Write(Encoding.ASCII.GetBytes("DX10"));
                writer.Write((uint)0);
                writer.Write((ulong)0);
            }
            var error = Assert.Throws<InvalidDataException>(() => Ba2.Open(textures));
            Assert.Equal("textures.ba2 is a DX10 archive, not GNRL. Textures are stored differently and nothing here reads them.",
                         error.Message);
        }
        finally { Directory.Delete(root, recursive: true); }
    }

    [Fact]
    public void EnumerateWinnersShadowsArchiveCopiesUnderLooseOverrideAndSkipsSiblingsAndMissingEntryIsNull()
    {
        string root = TempRoot();
        string dataFolder = Path.Combine(root, "Data");
        Directory.CreateDirectory(Path.Combine(dataFolder, "meshes", "x"));
        Directory.CreateDirectory(Path.Combine(dataFolder, "meshes", "y"));
        try
        {
            File.WriteAllBytes(Path.Combine(dataFolder, "meshes", "x", "shared.hkx"), Encoding.ASCII.GetBytes("loose"));
            File.WriteAllBytes(Path.Combine(dataFolder, "meshes", "x", "shared.txt"), Encoding.ASCII.GetBytes("sibling"));
            File.WriteAllBytes(Path.Combine(dataFolder, "meshes", "y", "other.hkx"), Encoding.ASCII.GetBytes("elsewhere"));
            string archiveA = Path.Combine(dataFolder, "A.ba2");
            string archiveB = Path.Combine(dataFolder, "B.ba2");
            WriteGeneralBa2(archiveA, "meshes/x/shared.hkx", Encoding.ASCII.GetBytes("archive A"));
            WriteGeneralBa2(archiveB, "meshes/x/shared.hkx", Encoding.ASCII.GetBytes("archive B"));

            using var data = GameData.Discover(dataFolder, Path.Combine(root, "plugins.txt"));

            var winner = Assert.Single(data.EnumerateWinners("Meshes/X", ".hkx"));
            Assert.Equal("meshes/x/shared.hkx", winner.Path);
            Assert.Equal("loose", winner.Source);
            Assert.Equal(dataFolder, winner.Location);
            Assert.Null(winner.ArchivePath);
            Assert.Equal(
                new[] { new GameData.AssetCopy("A.ba2", archiveA), new GameData.AssetCopy("B.ba2", archiveB) },
                winner.Shadowed.OrderBy(c => c.Source, StringComparer.Ordinal));

            Assert.Equal(Encoding.ASCII.GetBytes("archive B"), data.TryReadEntry(archiveB, "Meshes\\X\\shared.hkx"));
            Assert.Null(data.TryReadEntry(archiveB, "meshes/x/missing.hkx"));
            Assert.Throws<FileNotFoundException>(() => data.ReadEntry(archiveB, "meshes/x/missing.hkx"));
        }
        finally { Directory.Delete(root, recursive: true); }
    }

    [Fact]
    public void ArchiveOnlyWinnerFollowsArchivePathsOrderNotFileNameOrder()
    {
        string root = TempRoot();
        string dataFolder = Path.Combine(root, "Data");
        Directory.CreateDirectory(dataFolder);
        try
        {
            WriteGeneralBa2(Path.Combine(dataFolder, "A.ba2"), "meshes/y/only.hkx", Encoding.ASCII.GetBytes("archive A"));
            WriteGeneralBa2(Path.Combine(dataFolder, "B.ba2"), "meshes/y/only.hkx", Encoding.ASCII.GetBytes("archive B"));

            using var data = GameData.Discover(dataFolder, Path.Combine(root, "plugins.txt"));

            var winner = Assert.Single(data.EnumerateWinners("meshes/y/", ".hkx"));
            Assert.Equal(data.ArchivePaths[0], winner.ArchivePath);
            Assert.Equal(Path.GetFileName(data.ArchivePaths[0]), winner.Source);
            Assert.Equal(data.ArchivePaths[0], winner.Location);
            Assert.Equal(Path.GetFileName(data.ArchivePaths[1]), Assert.Single(winner.Shadowed).Source);
        }
        finally { Directory.Delete(root, recursive: true); }
    }

    [Fact]
    public void ModRootLooseOverrideBeatsDataLooseCopyAndIsLocatedByItsRoot()
    {
        string root = TempRoot();
        string dataFolder = Path.Combine(root, "Data");
        string modRoot = Path.Combine(root, "mods", "Override");
        Directory.CreateDirectory(Path.Combine(dataFolder, "meshes", "x"));
        Directory.CreateDirectory(Path.Combine(modRoot, "meshes", "x"));
        try
        {
            File.WriteAllBytes(Path.Combine(dataFolder, "meshes", "x", "shared.hkx"), Encoding.ASCII.GetBytes("data"));
            File.WriteAllBytes(Path.Combine(modRoot, "meshes", "x", "shared.hkx"), Encoding.ASCII.GetBytes("mod"));

            using var data = GameData.Discover(dataFolder, Path.Combine(root, "plugins.txt"), new[] { modRoot });

            var winner = Assert.Single(data.EnumerateWinners("meshes/x/", ".hkx"));
            Assert.Equal("loose:Override", winner.Source);
            Assert.Equal(Path.GetFullPath(modRoot), winner.Location);
            Assert.Equal(new[] { new GameData.AssetCopy("loose", dataFolder) }, winner.Shadowed);
        }
        finally { Directory.Delete(root, recursive: true); }
    }

    [Fact]
    public void ModRootWithoutMeshesFolderStillWinsItsSoundPrefix()
    {
        string root = TempRoot();
        string dataFolder = Path.Combine(root, "Data");
        string modRoot = Path.Combine(root, "mods", "Voice");
        Directory.CreateDirectory(dataFolder);
        Directory.CreateDirectory(Path.Combine(modRoot, "sound"));
        try
        {
            File.WriteAllBytes(Path.Combine(modRoot, "sound", "line.wav"), Encoding.ASCII.GetBytes("wav"));

            using var data = GameData.Discover(dataFolder, Path.Combine(root, "plugins.txt"), new[] { modRoot });

            var winner = Assert.Single(data.EnumerateWinners("Sound/", ".wav"));
            Assert.Equal("sound/line.wav", winner.Path);
            Assert.Equal("loose:Voice", winner.Source);
            Assert.Equal(Path.GetFullPath(modRoot), winner.Location);
        }
        finally { Directory.Delete(root, recursive: true); }
    }

    [Fact]
    public void TryReadEntryReusesTheOpenArchiveHandleInsteadOfReopening()
    {
        string root = TempRoot();
        string dataFolder = Path.Combine(root, "Data");
        Directory.CreateDirectory(dataFolder);
        try
        {
            string archive = Path.Combine(dataFolder, "A.ba2");
            WriteGeneralBa2(archive, "meshes/x/shared.hkx", Encoding.ASCII.GetBytes("archive A"));

            using var data = GameData.Discover(dataFolder, Path.Combine(root, "plugins.txt"));
            Assert.Equal(Encoding.ASCII.GetBytes("archive A"), data.TryReadEntry(archive, "meshes/x/shared.hkx"));

            Ba2.OpenCountForTest = 0;
            Assert.Equal(Encoding.ASCII.GetBytes("archive A"), data.TryReadEntry(archive, "meshes/x/shared.hkx"));
            Assert.Equal(Encoding.ASCII.GetBytes("archive A"), data.TryReadEntry(archive, "Meshes\\X\\shared.hkx"));
            Assert.Equal(0, Ba2.OpenCountForTest);
        }
        finally { Directory.Delete(root, recursive: true); }
    }

    [Fact]
    public void TryReadEntryReturnsNullForAMissingArchiveFile()
    {
        string root = TempRoot();
        string dataFolder = Path.Combine(root, "Data");
        Directory.CreateDirectory(dataFolder);
        try
        {
            using var data = GameData.Discover(dataFolder, Path.Combine(root, "plugins.txt"));

            Assert.Null(data.TryReadEntry(Path.Combine(dataFolder, "absent.ba2"), "meshes/x/shared.hkx"));
        }
        finally { Directory.Delete(root, recursive: true); }
    }

    private static string TempRoot() => Path.Combine(Path.GetTempPath(), "bgs-winners-" + Guid.NewGuid().ToString("N"));

    private static void WriteGeneralBa2(string path, string entryName, byte[] payload)
    {
        byte[] name = Encoding.UTF8.GetBytes(entryName);
        const int headerBytes = 24;
        const int recordBytes = 36;
        ulong nameTable = headerBytes + recordBytes;
        ulong dataOffset = nameTable + 2u + (uint)name.Length;

        using var stream = File.Create(path);
        using var writer = new BinaryWriter(stream);
        writer.Write(Encoding.ASCII.GetBytes("BTDX"));
        writer.Write((uint)1);
        writer.Write(Encoding.ASCII.GetBytes("GNRL"));
        writer.Write((uint)1);
        writer.Write(nameTable);
        writer.Write((uint)0);
        writer.Write((uint)0);
        writer.Write((uint)0);
        writer.Write((uint)0x00100100);
        writer.Write(dataOffset);
        writer.Write((uint)0);
        writer.Write((uint)payload.Length);
        writer.Write(0xBAADF00Du);
        writer.Write((ushort)name.Length);
        writer.Write(name);
        writer.Write(payload);
    }
}
