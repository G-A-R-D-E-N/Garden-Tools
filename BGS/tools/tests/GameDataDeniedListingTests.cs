using System;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Security.Principal;
using System.Text;
using OpenCommonwealth.Services.Archive;
using Xunit;

namespace BehaviourStudio.Tests;

public sealed class GameDataDeniedListingTests
{
    [Fact]
    public void DiscoverSkipsAModRootWhoseTopLevelListingIsDeniedAndKeepsTheOtherArchives()
    {
        using var root = new TestArchives.TempDirectory("bgs-gamedata-denied-root");
        string dataFolder = Path.Combine(root.Path, "Data");
        string deniedRoot = Path.Combine(root.Path, "mods", "Denied");
        string readableRoot = Path.Combine(root.Path, "mods", "Readable");
        Directory.CreateDirectory(dataFolder);
        Directory.CreateDirectory(deniedRoot);
        Directory.CreateDirectory(readableRoot);
        string archive = Path.Combine(readableRoot, "Readable.ba2");
        TestArchives.WriteGeneralBa2(archive, ("meshes/y/only.hkx", Encoding.ASCII.GetBytes("readable")));
        if (!DenyListing(deniedRoot)) return;
        try
        {
            using var data = GameData.Discover(dataFolder, Path.Combine(root.Path, "plugins.txt"),
                                               new[] { deniedRoot, readableRoot });

            GameAsset winner = Assert.Single(data.EnumerateWinners("meshes/y"));
            Assert.Equal(archive, winner.ArchivePath);
            Assert.Equal(Encoding.ASCII.GetBytes("readable"), data.Read(winner));
        }
        finally { RestoreListing(deniedRoot); }
    }

    [Fact]
    public void ModlistProfileNamesTheProfileUnlessTheProfilesListingIsDenied()
    {
        using var root = new TestArchives.TempDirectory("bgs-gamedata-denied-profiles");
        string profiles = Path.Combine(root.Path, "profiles");
        Directory.CreateDirectory(Path.Combine(profiles, "Default"));
        File.WriteAllText(Path.Combine(profiles, "Default", "modlist.txt"), "+Readable\n");
        Assert.Equal("Default", GameData.ModlistProfile(root.Path));
        if (!DenyListing(profiles)) return;
        try { Assert.Null(GameData.ModlistProfile(root.Path)); }
        finally { RestoreListing(profiles); }
    }

    [Fact]
    public void DiscoverModdedFallsBackToPlainArchivesWhenTheModlistCannotBeRead()
    {
        using var root = new TestArchives.TempDirectory("bgs-gamedata-denied-modlist");
        string dataFolder = Path.Combine(root.Path, "Data");
        string mods = Path.Combine(root.Path, "mods");
        string plugins = Path.Combine(root.Path, "plugins.txt");
        Directory.CreateDirectory(dataFolder);
        Directory.CreateDirectory(Path.Combine(mods, "mods", "Readable"));
        TestArchives.WriteGeneralBa2(Path.Combine(dataFolder, "Base.ba2"), ("meshes/y/only.hkx", Encoding.ASCII.GetBytes("base")));
        TestArchives.WriteGeneralBa2(Path.Combine(mods, "mods", "Readable", "Readable.ba2"), ("meshes/y/only.hkx", Encoding.ASCII.GetBytes("mod")));
        string modlist = Path.Combine(mods, "modlist.txt");
        File.WriteAllText(modlist, "+Readable\n");
        if (!DenyReading(modlist)) return;
        try
        {
            using var modded = GameData.DiscoverModded(dataFolder, mods, null, plugins);
            using var plain = GameData.Discover(dataFolder, plugins);

            Assert.Equal(plain.ArchivePaths, modded.ArchivePaths);
            Assert.Equal(plain.ModRoots, modded.ModRoots);
        }
        finally { RestoreListing(modlist); }
    }

    [Fact]
    public void SubgraphIndexKeepsReadableLooseManifestsWhenAModRootListingIsDenied()
    {
        using var root = new TestArchives.TempDirectory("bgs-gamedata-denied-subgraph");
        string dataFolder = Path.Combine(root.Path, "Data");
        string modRoot = Path.Combine(root.Path, "mods", "Denied");
        const ulong id = 1234567890123;
        string dataAnim = Path.Combine(dataFolder, "meshes", "animtextdata");
        string deniedFileData = Path.Combine(modRoot, "meshes", "animtextdata", "animationfiledata");
        Directory.CreateDirectory(Path.Combine(dataAnim, "animationfiledata"));
        Directory.CreateDirectory(Path.Combine(dataAnim, "animationoffsets"));
        Directory.CreateDirectory(deniedFileData);
        File.WriteAllText(Path.Combine(dataAnim, "animationfiledata", $"{id}.txt"),
            "V1\nx\n1234567890123\nx\nActors\\Test\\Behaviors\\Test.hkx\n");
        File.WriteAllText(Path.Combine(dataAnim, "animationoffsets", $"{id}.txt"), "V4\n");
        if (!DenyListing(deniedFileData)) return;
        try
        {
            using var data = GameData.Discover(dataFolder, Path.Combine(root.Path, "plugins.txt"), new[] { modRoot });
            var index = SubgraphIndex.Discover(data);

            Assert.Equal("Actors\\Test\\Behaviors\\Test.hkx", index.Find(id)?.PrimaryBehavior);
            Assert.NotNull(index.FindOffsetData(id));
        }
        finally { RestoreListing(deniedFileData); }
    }

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
}
