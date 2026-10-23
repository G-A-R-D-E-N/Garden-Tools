using System;
using System.Diagnostics;
using System.IO;
using System.Security.Principal;
using System.Text;
using BehaviourStudio.App;
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
    public void PlaybackSkeletonLookupReportsNativeLoadFailureButSkipsCorruptArchives()
    {
        var native = new InvalidDataException("the Radicle native library could not be loaded",
            new DllNotFoundException("radicle_c"));
        Assert.Same(native, Assert.Throws<InvalidDataException>(
            () => MainWindow.SkeletonBytesOrNull(() => throw native)));
        Assert.Null(MainWindow.SkeletonBytesOrNull(() => throw new InvalidDataException("corrupt archive")));
        Assert.Null(MainWindow.SkeletonBytesOrNull(() => throw new IOException("truncated archive")));
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
        Directory.CreateDirectory(Path.Combine(dataFolder, "meshes", "xy"));
        try
        {
            File.WriteAllBytes(Path.Combine(dataFolder, "meshes", "x", "shared.hkx"), Encoding.ASCII.GetBytes("loose"));
            File.WriteAllBytes(Path.Combine(dataFolder, "meshes", "x", "shared.txt"), Encoding.ASCII.GetBytes("sibling"));
            File.WriteAllBytes(Path.Combine(dataFolder, "meshes", "y", "other.hkx"), Encoding.ASCII.GetBytes("elsewhere"));
            File.WriteAllBytes(Path.Combine(dataFolder, "meshes", "xy", "other.hkx"), Encoding.ASCII.GetBytes("xy"));
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
    public void ModArchiveOutranksBaseArchivesWithTheSamePath()
    {
        string root = TempRoot();
        string dataFolder = Path.Combine(root, "Data");
        string modRoot = Path.Combine(root, "mods", "Zed");
        Directory.CreateDirectory(dataFolder);
        Directory.CreateDirectory(modRoot);
        try
        {
            WriteGeneralBa2(Path.Combine(dataFolder, "A.ba2"), "meshes/y/only.hkx", Encoding.ASCII.GetBytes("base A"));
            WriteGeneralBa2(Path.Combine(dataFolder, "B.ba2"), "meshes/y/only.hkx", Encoding.ASCII.GetBytes("base B"));
            string zed = Path.Combine(modRoot, "Zed.ba2");
            WriteGeneralBa2(zed, "meshes/y/only.hkx", Encoding.ASCII.GetBytes("mod Zed"));

            using var data = GameData.Discover(dataFolder, Path.Combine(root, "plugins.txt"), new[] { modRoot });

            var winner = Assert.Single(data.EnumerateWinners("meshes/y/", ".hkx"));
            Assert.Equal(Path.GetFullPath(zed), winner.ArchivePath);
            Assert.Equal("Zed.ba2", winner.Source);
            Assert.Equal(Path.GetFullPath(zed), winner.Location);
            Assert.Equal(new[] { "A.ba2", "B.ba2" }, winner.Shadowed.Select(c => c.Source));
            Assert.Equal(Encoding.ASCII.GetBytes("mod Zed"), data.TryReadEntry(zed, "meshes/y/only.hkx"));
        }
        finally { Directory.Delete(root, recursive: true); }
    }

    [Fact]
    public void LaterPluginsBaseArchiveWinsOverEarlierOne()
    {
        string root = TempRoot();
        string dataFolder = Path.Combine(root, "Data");
        Directory.CreateDirectory(dataFolder);
        try
        {
            string archiveA = Path.Combine(dataFolder, "A - Animations.ba2");
            string archiveB = Path.Combine(dataFolder, "B - Animations.ba2");
            WriteGeneralBa2(archiveA, "Meshes/X/anim.hkx", Encoding.ASCII.GetBytes("archive A"));
            WriteGeneralBa2(archiveB, "Meshes/X/anim.hkx", Encoding.ASCII.GetBytes("archive B"));
            WriteGeneralBa2(Path.Combine(dataFolder, "Fallout4 - Animations.ba2"), "Meshes/X/anim.hkx",
                            Encoding.ASCII.GetBytes("archive Fallout4"));
            string plugins = Path.Combine(root, "plugins.txt");
            File.WriteAllText(plugins, "*A.esm\n*B.esm\n");

            using var data = GameData.Discover(dataFolder, plugins);

            Assert.Equal(
                new[] { "B - Animations.ba2", "A - Animations.ba2", "Fallout4 - Animations.ba2" },
                data.ArchivePaths.Select(path => Path.GetFileName(path)));
            var winner = Assert.Single(data.EnumerateWinners("meshes/x/", ".hkx"));
            Assert.Equal("B - Animations.ba2", Path.GetFileName(winner.ArchivePath));
            Assert.Equal(new[] { "A - Animations.ba2", "Fallout4 - Animations.ba2" },
                         winner.Shadowed.Select(c => c.Source));
            Assert.Equal(Encoding.ASCII.GetBytes("archive B"), data.ReadAnimation(dataFolder, "Meshes/X/anim.hkx")?.Bytes);
        }
        finally { Directory.Delete(root, recursive: true); }
    }

    [Fact]
    public void HigherModRootWinsAndLowerRootAndDataCopiesAreListedInPrecedenceOrder()
    {
        string root = TempRoot();
        string dataFolder = Path.Combine(root, "Data");
        string low = Path.Combine(root, "mods", "Low");
        string high = Path.Combine(root, "mods", "High");
        Directory.CreateDirectory(Path.Combine(dataFolder, "meshes", "x"));
        try
        {
            File.WriteAllBytes(Path.Combine(dataFolder, "meshes", "x", "shared.hkx"), Encoding.ASCII.GetBytes("data"));
            foreach (string mod in new[] { low, high })
            {
                Directory.CreateDirectory(Path.Combine(mod, "meshes", "x"));
                File.WriteAllBytes(Path.Combine(mod, "meshes", "x", "shared.hkx"), Encoding.ASCII.GetBytes(Path.GetFileName(mod)));
            }

            using var data = GameData.Discover(dataFolder, Path.Combine(root, "plugins.txt"), new[] { low, high });

            var winner = Assert.Single(data.EnumerateWinners("meshes/x/", ".hkx"));
            Assert.Equal("loose:High", winner.Source);
            Assert.Equal(Path.GetFullPath(high), winner.Location);
            Assert.Equal(
                new[] { new GameData.AssetCopy("loose:Low", Path.GetFullPath(low)), new GameData.AssetCopy("loose", dataFolder) },
                winner.Shadowed);
        }
        finally { Directory.Delete(root, recursive: true); }
    }

    [Fact]
    public void EnumerateWinnersNormalizesPrefixAndExtensionSpellings()
    {
        string root = TempRoot();
        string dataFolder = Path.Combine(root, "Data");
        Directory.CreateDirectory(Path.Combine(dataFolder, "meshes", "n"));
        try
        {
            File.WriteAllBytes(Path.Combine(dataFolder, "meshes", "n", "a.hkx"), Encoding.ASCII.GetBytes("hkx"));
            File.WriteAllBytes(Path.Combine(dataFolder, "meshes", "n", "notahkx"), Encoding.ASCII.GetBytes("plain"));

            using var data = GameData.Discover(dataFolder, Path.Combine(root, "plugins.txt"));

            Assert.Equal(new[] { "meshes/n/a.hkx" }, Paths(data, "Meshes\\N", ".hkx"));
            Assert.Equal(new[] { "meshes/n/a.hkx" }, Paths(data, "./Meshes/N", "hkx"));
            Assert.Equal(new[] { "meshes/n/a.hkx" }, Paths(data, ".", ".hkx"));
            Assert.Equal(new[] { "meshes/n/a.hkx" }, Paths(data, "./.", ".hkx"));
            Assert.Equal(new[] { "meshes/n/a.hkx" }, Paths(data, "././Meshes/N", ".hkx"));
            Assert.Equal(new[] { "meshes/n/a.hkx", "meshes/n/notahkx" }, Paths(data, "", ""));
            Assert.Equal(new[] { "meshes/n/a.hkx" }, Paths(data, "/./Meshes/N", ".hkx"));
            Assert.Equal(new[] { "meshes/n/a.hkx" }, Paths(data, ".//Meshes/N", ".hkx"));
            Assert.Equal(new[] { "meshes/n/a.hkx", "meshes/n/notahkx" }, Paths(data, "", "."));
        }
        finally { Directory.Delete(root, recursive: true); }
    }

    [Fact]
    public void EnumerateWinnersListsHiddenLooseFiles()
    {
        string root = TempRoot();
        string dataFolder = Path.Combine(root, "Data");
        string folder = Path.Combine(dataFolder, "meshes", "h");
        Directory.CreateDirectory(folder);
        try
        {
            string name = OperatingSystem.IsWindows() ? "hidden.hkx" : ".hidden.hkx";
            string file = Path.Combine(folder, name);
            File.WriteAllBytes(file, Encoding.ASCII.GetBytes("hkx"));
            if (OperatingSystem.IsWindows()) File.SetAttributes(file, FileAttributes.Hidden);

            using var data = GameData.Discover(dataFolder, Path.Combine(root, "plugins.txt"));

            Assert.Equal(new[] { "meshes/h/" + name }, Paths(data, "meshes/h/", ".hkx"));
        }
        finally { Directory.Delete(root, recursive: true); }
    }

    [Fact]
    public void ModLooseIndexFindsFilesUnderLowercaseMeshesRoot()
    {
        string root = TempRoot();
        string dataFolder = Path.Combine(root, "Data");
        string modRoot = Path.Combine(root, "mods", "Lower");
        string folder = Path.Combine(modRoot, "meshes", "h");
        Directory.CreateDirectory(dataFolder);
        Directory.CreateDirectory(folder);
        try
        {
            File.WriteAllBytes(Path.Combine(folder, "pose.hkx"), Encoding.ASCII.GetBytes("hkx"));

            using var data = GameData.Discover(dataFolder, Path.Combine(root, "plugins.txt"), new[] { modRoot });

            Assert.Equal(Encoding.ASCII.GetBytes("hkx"), data.ReadAnimation(dataFolder, "meshes/h/pose.hkx")?.Bytes);
        }
        finally { Directory.Delete(root, recursive: true); }
    }

    [Fact]
    public void SubgraphIndexFindsLooseManifestUnderLowercaseAnimTextData()
    {
        string root = TempRoot();
        string dataFolder = Path.Combine(root, "Data");
        string modRoot = Path.Combine(root, "mods", "LowerAnim");
        const ulong id = 1234567890123;
        string fileData = Path.Combine(modRoot, "meshes", "animtextdata", "animationfiledata");
        string offsets = Path.Combine(modRoot, "meshes", "animtextdata", "animationoffsets");
        Directory.CreateDirectory(dataFolder);
        Directory.CreateDirectory(fileData);
        Directory.CreateDirectory(offsets);
        try
        {
            File.WriteAllText(Path.Combine(fileData, $"{id}.txt"),
                "V1\nx\n1234567890123\nx\nActors\\Test\\Behaviors\\Test.hkx\n");
            File.WriteAllText(Path.Combine(offsets, $"{id}.txt"), "V4\n");

            using var data = GameData.Discover(dataFolder, Path.Combine(root, "plugins.txt"), new[] { modRoot });
            var index = SubgraphIndex.Discover(data);

            Assert.Equal("Actors\\Test\\Behaviors\\Test.hkx", index.Find(id)?.PrimaryBehavior);
            Assert.NotNull(index.FindOffsetData(id));
        }
        finally { Directory.Delete(root, recursive: true); }
    }

    [Fact]
    public void SubgraphIndexKeepsReadableLooseManifestsWhenAModRootListingIsDenied()
    {
        string root = TempRoot();
        string dataFolder = Path.Combine(root, "Data");
        string modRoot = Path.Combine(root, "mods", "Denied");
        const ulong id = 1234567890123;
        string dataAnim = Path.Combine(dataFolder, "meshes", "animtextdata");
        string deniedFileData = Path.Combine(modRoot, "meshes", "animtextdata", "animationfiledata");
        Directory.CreateDirectory(Path.Combine(dataAnim, "animationfiledata"));
        Directory.CreateDirectory(Path.Combine(dataAnim, "animationoffsets"));
        Directory.CreateDirectory(deniedFileData);
        try
        {
            File.WriteAllText(Path.Combine(dataAnim, "animationfiledata", $"{id}.txt"),
                "V1\nx\n1234567890123\nx\nActors\\Test\\Behaviors\\Test.hkx\n");
            File.WriteAllText(Path.Combine(dataAnim, "animationoffsets", $"{id}.txt"), "V4\n");
            if (!DenyListing(deniedFileData)) return;

            using var data = GameData.Discover(dataFolder, Path.Combine(root, "plugins.txt"), new[] { modRoot });
            var index = SubgraphIndex.Discover(data);

            Assert.Equal("Actors\\Test\\Behaviors\\Test.hkx", index.Find(id)?.PrimaryBehavior);
            Assert.NotNull(index.FindOffsetData(id));
        }
        finally
        {
            RestoreListing(deniedFileData);
            Directory.Delete(root, recursive: true);
        }
    }

    [Fact]
    public void DiscoverSkipsAModRootWhoseTopLevelListingIsDeniedAndKeepsTheOtherArchives()
    {
        string root = TempRoot();
        string dataFolder = Path.Combine(root, "Data");
        string deniedRoot = Path.Combine(root, "mods", "Denied");
        string readableRoot = Path.Combine(root, "mods", "Readable");
        Directory.CreateDirectory(dataFolder);
        Directory.CreateDirectory(deniedRoot);
        Directory.CreateDirectory(readableRoot);
        try
        {
            string archive = Path.Combine(readableRoot, "Readable.ba2");
            WriteGeneralBa2(archive, "meshes/y/only.hkx", Encoding.ASCII.GetBytes("readable"));
            if (!DenyListing(deniedRoot)) return;
            try
            {
                using var data = GameData.Discover(dataFolder, Path.Combine(root, "plugins.txt"),
                                                   new[] { deniedRoot, readableRoot });

                var winner = Assert.Single(data.EnumerateWinners("meshes/y/", ".hkx"));
                Assert.Equal(Path.GetFullPath(archive), winner.ArchivePath);
                Assert.Equal("Readable.ba2", winner.Source);
            }
            finally
            {
                RestoreListing(deniedRoot);
            }
        }
        finally { Directory.Delete(root, recursive: true); }
    }

    [Fact]
    public void ModlistProfileNamesTheProfileUnlessTheProfilesListingIsDenied()
    {
        string root = TempRoot();
        string mods = Path.Combine(root, "mods");
        string profiles = Path.Combine(mods, "profiles");
        Directory.CreateDirectory(Path.Combine(profiles, "Default"));
        try
        {
            File.WriteAllText(Path.Combine(profiles, "Default", "modlist.txt"), "+Readable\n");
            Assert.Equal("Default", GameData.ModlistProfile(mods));
            if (!DenyListing(profiles)) return;
            try
            {
                Assert.Null(GameData.ModlistProfile(mods));
            }
            finally
            {
                RestoreListing(profiles);
            }
        }
        finally { Directory.Delete(root, recursive: true); }
    }

    [Fact]
    public void ModlistPathUsesTheNamedProfileAndNeverFallsBackToTheRootModlist()
    {
        string root = TempRoot();
        string mods = Path.Combine(root, "mods");
        Directory.CreateDirectory(Path.Combine(mods, "profiles", "Default"));
        try
        {
            File.WriteAllText(Path.Combine(mods, "modlist.txt"), "+Root\n");
            File.WriteAllText(Path.Combine(mods, "profiles", "Default", "modlist.txt"), "+Readable\n");

            Assert.Equal(Path.Combine(Path.GetFullPath(mods), "profiles", "Default", "modlist.txt"),
                         GameData.ModlistPath(mods, "Default"));
            Assert.Null(GameData.ModlistPath(mods, "Missing"));
            Assert.Equal(Path.Combine(Path.GetFullPath(mods), "modlist.txt"), GameData.ModlistPath(mods, null));
        }
        finally { Directory.Delete(root, recursive: true); }
    }

    [Fact]
    public void DiscoverModdedFallsBackToPlainArchivesWhenTheModlistCannotBeRead()
    {
        string root = TempRoot();
        string dataFolder = Path.Combine(root, "Data");
        string mods = Path.Combine(root, "mods");
        Directory.CreateDirectory(dataFolder);
        Directory.CreateDirectory(Path.Combine(mods, "Readable"));
        string modlist = Path.Combine(mods, "modlist.txt");
        try
        {
            WriteGeneralBa2(Path.Combine(dataFolder, "Base.ba2"), "meshes/y/only.hkx", Encoding.ASCII.GetBytes("base"));
            WriteGeneralBa2(Path.Combine(mods, "Readable", "Readable.ba2"), "meshes/y/only.hkx", Encoding.ASCII.GetBytes("mod"));
            File.WriteAllText(modlist, "+Readable\n");
            if (!DenyReading(modlist)) return;
            try
            {
                using var modded = GameData.DiscoverModded(dataFolder, mods, null, Path.Combine(root, "plugins.txt"));
                using var plain = GameData.Discover(dataFolder, Path.Combine(root, "plugins.txt"));

                Assert.Equal(plain.ArchivePaths, modded.ArchivePaths);
                Assert.Equal(plain.ModRoots, modded.ModRoots);
            }
            finally
            {
                RestoreListing(modlist);
            }
        }
        finally { Directory.Delete(root, recursive: true); }
    }

    [Theory]
    [InlineData("Meshes\\X\\CharacterAssets\\a.HKX", true)]
    [InlineData("Meshes/X/CharacterAssets/a.txt", false)]
    [InlineData("CharacterAssets.hkx", false)]
    [InlineData("x.hkt", false)]
    public void IsSkeletonPathAcceptsOnlyHkxUnderCharacterAssets(string name, bool expected) =>
        Assert.Equal(expected, GameData.IsSkeletonPath(name));

    private static string[] Paths(GameData data, string prefix, string extension) =>
        data.EnumerateWinners(prefix, extension).Select(w => w.Path).ToArray();

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

    private static bool DenyListing(string folder) => DenyAccess(folder, () => Directory.EnumerateFiles(folder).ToList());

    private static bool DenyReading(string file) => DenyAccess(file, () => File.ReadAllBytes(file));

    private static bool DenyAccess(string path, Action probe)
    {
        if (OperatingSystem.IsWindows()) Icacls(path, "/deny", $"{WindowsIdentity.GetCurrent().Name}:(RD)");
        else File.SetUnixFileMode(path, UnixFileMode.None);
        try { probe(); }
        catch (UnauthorizedAccessException) { return true; }
        if (Environment.GetEnvironmentVariable("BGS_REQUIRE_DENY_TESTS") == "1")
            Assert.Fail($"denying access to {path} had no effect, so the denied-access test cannot prove its point");
        return false;
    }

    private static void RestoreListing(string folder)
    {
        if (OperatingSystem.IsWindows()) Icacls(folder, "/remove:d", WindowsIdentity.GetCurrent().Name);
        else File.SetUnixFileMode(folder, UnixFileMode.UserRead | UnixFileMode.UserWrite | UnixFileMode.UserExecute);
    }

    private static void Icacls(string folder, params string[] args)
    {
        var start = new ProcessStartInfo("icacls") { UseShellExecute = false, RedirectStandardOutput = true, RedirectStandardError = true };
        start.ArgumentList.Add(folder);
        foreach (string arg in args) start.ArgumentList.Add(arg);
        using var process = Process.Start(start)!;
        process.StandardOutput.ReadToEnd();
        string error = process.StandardError.ReadToEnd();
        process.WaitForExit();
        if (process.ExitCode != 0) throw new IOException($"icacls failed: {error}");
    }

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
