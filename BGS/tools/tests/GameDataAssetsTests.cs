using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using OpenCommonwealth.Services;
using OpenCommonwealth.Services.Archive;
using Xunit;

namespace BehaviourStudio.Tests;

public sealed class GameDataAssetsTests
{
    private const string Prefix = "Meshes/Actors/Turret";

    [Fact]
    public void ReadEntryReadsThroughTheHeldOpenArchiveCache()
    {
        using var root = new TestArchives.TempDirectory("bgs-gamedata-entry");
        byte[] payload = Bytes(5, 1);
        string archive = Path.Combine(root.Path, "tiny.ba2");
        TestArchives.WriteGeneralBa2(archive, ("Meshes\\Actors/Turret/Idle.hkx", payload));

        using var data = GameData.Discover(root.Path, Path.Combine(root.Path, "plugins.txt"));

        Ba2.OpenCountForTest = 0;
        Assert.Equal(payload, data.ReadEntry(archive, "Meshes/Actors/Turret/Idle.hkx"));
        Assert.Equal(payload, data.ReadEntry(archive, "meshes/actors/turret/idle.HKX"));
        Assert.Equal(payload, data.ReadEntry(archive, "Meshes\\Actors\\Turret\\Idle.hkt"));
        Assert.Equal(1, Ba2.OpenCountForTest);

        Assert.True(data.TryReadEntry(archive, "Meshes/Actors/Turret/Idle.hkx",
                                      out byte[] bytes, out string? warning));
        Assert.Equal(payload, bytes);
        Assert.Null(warning);
    }

    [Fact]
    public void ReadEntryReportsMissesAndSkippableArchives()
    {
        using var root = new TestArchives.TempDirectory("bgs-gamedata-miss");
        string archive = Path.Combine(root.Path, "tiny.ba2");
        TestArchives.WriteGeneralBa2(archive, ("Meshes/Actors/Turret/Idle.hkx", Bytes(3, 7)));

        using var data = GameData.Discover(root.Path, Path.Combine(root.Path, "plugins.txt"));

        Assert.Null(data.ReadEntry(archive, "Meshes/Actors/Turret/Absent.hkx"));
        Assert.False(data.TryReadEntry(archive, "Meshes/Actors/Turret/Absent.hkx",
                                       out byte[] missed, out string? missedWarning));
        Assert.Empty(missed);
        Assert.Null(missedWarning);

        Assert.Null(data.ReadEntry(archive, ""));
        Assert.False(data.TryReadEntry(archive, "", out _, out string? emptyWarning));
        Assert.Null(emptyWarning);

        string broken = Path.Combine(root.Path, "broken.ba2");
        File.WriteAllText(broken, "this is not a BTDX archive");
        Assert.Null(data.ReadEntry(broken, "Meshes/Actors/Turret/Idle.hkx"));
        Assert.False(data.TryReadEntry(broken, "Meshes/Actors/Turret/Idle.hkx",
                                       out byte[] unreadable, out string? brokenWarning));
        Assert.Empty(unreadable);
        Assert.NotNull(brokenWarning);
        Assert.Contains("broken.ba2", brokenWarning, StringComparison.Ordinal);

        string absent = Path.Combine(root.Path, "absent.ba2");
        Assert.Null(data.ReadEntry(absent, "Meshes/Actors/Turret/Idle.hkx"));
        Assert.False(data.TryReadEntry(absent, "Meshes/Actors/Turret/Idle.hkx",
                                       out _, out string? absentWarning));
        Assert.NotNull(absentWarning);
        Assert.Contains("absent.ba2", absentWarning, StringComparison.Ordinal);

        string textures = Path.Combine(root.Path, "textures.ba2");
        using (var stream = File.Create(textures))
        using (var writer = new BinaryWriter(stream))
        {
            writer.Write("BTDX"u8.ToArray());
            writer.Write((uint)1);
            writer.Write("DX10"u8.ToArray());
            writer.Write((uint)0);
            writer.Write((ulong)0);
        }
        Assert.Null(data.ReadEntry(textures, "Meshes/Actors/Turret/Idle.hkx"));
        Assert.False(data.TryReadEntry(textures, "Meshes/Actors/Turret/Idle.hkx",
                                       out _, out string? textureWarning));
        Assert.Null(textureWarning);
    }

    [Fact]
    public void DisposedGameDataClosesItsArchivesAndRefusesFurtherReads()
    {
        using var root = new TestArchives.TempDirectory("bgs-gamedata-lifetime");
        byte[] payload = Bytes(2, 9);
        string archive = Path.Combine(root.Path, "tiny.ba2");
        TestArchives.WriteGeneralBa2(archive, ("Meshes/Actors/Turret/Idle.hkx", payload));

        var data = GameData.Discover(root.Path, Path.Combine(root.Path, "plugins.txt"));
        Assert.Equal(payload, data.ReadEntry(archive, "Meshes/Actors/Turret/Idle.hkx"));

        data.Dispose();
        data.Dispose();

        Assert.Throws<ObjectDisposedException>(() => data.ReadEntry(archive, "Meshes/Actors/Turret/Idle.hkx"));
        Assert.Throws<ObjectDisposedException>(() => data.EnumerateWinners(Prefix));

        File.Delete(archive);
        Assert.False(File.Exists(archive));
    }

    [Fact]
    public void EnumerateWinnersAppliesLoadOrderPrecedenceAcrossSources()
    {
        using var root = new TestArchives.TempDirectory("bgs-gamedata-winners");
        string dataFolder = Path.Combine(root.Path, "Data");
        string low = Path.Combine(root.Path, "mods", "Low");
        string high = Path.Combine(root.Path, "mods", "High");
        Directory.CreateDirectory(dataFolder);
        Directory.CreateDirectory(low);
        Directory.CreateDirectory(high);

        string shared = Prefix + "/shared.hkx";
        Put(dataFolder, shared, Bytes(1, 1));
        Put(low, shared, Bytes(2, 2));
        Put(high, shared, Bytes(3, 3));
        Put(high, Prefix + "/high-only.hkx", Bytes(4, 4));
        Put(low, Prefix + "/low-only.hkx", Bytes(5, 5));
        Put(dataFolder, Prefix + "/data-only.hkx", Bytes(6, 6));
        Put(high, Prefix + "/pair.hkx", Bytes(7, 7));
        Put(high, Prefix + "/pair.hkt", Bytes(8, 8));
        Put(low, Prefix + "/pair.hkx", Bytes(9, 9));
        Put(dataFolder, "Meshes/Actors/Canine/outside.hkx", Bytes(10, 10));

        string highArchive = Path.Combine(high, "High - Main.ba2");
        TestArchives.WriteGeneralBa2(highArchive,
            (shared, Bytes(11, 11)),
            (Prefix + "/arch-only.hkx", Bytes(12, 12)));
        string lowArchive = Path.Combine(low, "Low - Main.ba2");
        TestArchives.WriteGeneralBa2(lowArchive, (shared, Bytes(13, 13)));
        string firstBase = Path.Combine(dataFolder, "a - Base.ba2");
        TestArchives.WriteGeneralBa2(firstBase,
            (shared, Bytes(14, 14)),
            (Prefix + "/arch-only.hkx", Bytes(15, 15)),
            (Prefix + "/only-a.hkx", Bytes(16, 16)),
            (Prefix + "/data-only.hkx", Bytes(17, 17)));
        string secondBase = Path.Combine(dataFolder, "z - Base.ba2");
        TestArchives.WriteGeneralBa2(secondBase, (Prefix + "/only-a.hkx", Bytes(18, 18)));

        using var data = GameData.Discover(dataFolder, Path.Combine(root.Path, "plugins.txt"),
                                           new[] { low, high });
        Assert.Equal(new[] { highArchive, lowArchive, firstBase, secondBase }, data.ArchivePaths);

        IReadOnlyList<GameAsset> winners = data.EnumerateWinners(Prefix);

        GameAsset winner = Only(winners, shared);
        Assert.Equal(GameAssetOrigin.Loose, winner.Origin);
        Assert.Equal(high, winner.SourceRoot);
        Assert.Null(winner.ArchivePath);
        Assert.Null(winner.EntryName);
        Assert.Equal(3, winner.Size);
        Assert.Equal(Path.Combine(high, "Meshes", "Actors", "Turret", "shared.hkx"), winner.FullPath);
        Assert.Equal(Bytes(3, 3), data.Read(winner));

        Assert.Equal(5, winner.Shadowed.Count);
        Assert.Equal(
            new[] { low, dataFolder, highArchive, lowArchive, firstBase },
            winner.Shadowed.Select(s => s.Origin == GameAssetOrigin.Loose ? s.SourceRoot : s.ArchivePath));
        Assert.Equal(Bytes(2, 2), data.Read(winner.Shadowed[0]));
        Assert.Equal(Bytes(1, 1), data.Read(winner.Shadowed[1]));
        Assert.Equal(Bytes(11, 11), data.Read(winner.Shadowed[2]));
        Assert.Equal(Bytes(13, 13), data.Read(winner.Shadowed[3]));
        Assert.Equal(Bytes(14, 14), data.Read(winner.Shadowed[4]));
        Assert.All(winner.Shadowed, s => Assert.Empty(s.Shadowed));

        GameAsset highOnly = Only(winners, Prefix + "/high-only.hkx");
        Assert.Equal(high, highOnly.SourceRoot);
        Assert.Equal("Meshes/Actors/Turret/high-only.hkx", highOnly.VirtualPath);
        Assert.Empty(highOnly.Shadowed);

        GameAsset lowOnly = Only(winners, Prefix + "/low-only.hkx");
        Assert.Equal(low, lowOnly.SourceRoot);
        Assert.Equal(Bytes(5, 5), data.Read(lowOnly));

        GameAsset archOnly = Only(winners, Prefix + "/arch-only.hkx");
        Assert.Equal(GameAssetOrigin.Archive, archOnly.Origin);
        Assert.Equal(highArchive, archOnly.ArchivePath);
        Assert.Equal("Meshes/Actors/Turret/arch-only.hkx", archOnly.EntryName);
        Assert.Null(archOnly.SourceRoot);
        Assert.Null(archOnly.FullPath);
        Assert.Equal(12, archOnly.Size);
        Assert.Equal(Bytes(12, 12), data.Read(archOnly));
        Assert.Equal(new[] { firstBase }, archOnly.Shadowed.Select(s => s.ArchivePath));

        GameAsset onlyA = Only(winners, Prefix + "/only-a.hkx");
        Assert.Equal(firstBase, onlyA.ArchivePath);
        Assert.Equal(secondBase, Assert.Single(onlyA.Shadowed).ArchivePath);

        GameAsset dataOnly = Only(winners, Prefix + "/data-only.hkx");
        Assert.Equal(dataFolder, dataOnly.SourceRoot);
        Assert.Equal(Bytes(6, 6), data.Read(dataOnly));
        Assert.Equal(firstBase, Assert.Single(dataOnly.Shadowed).ArchivePath);

        Assert.DoesNotContain(winners, a => a.VirtualPath.Contains("Canine", StringComparison.Ordinal));

        var ranks = winners.Select(asset => Rank(asset, data, high, low, dataFolder)).ToList();
        Assert.Equal(ranks.OrderBy(rank => rank), ranks);
        Assert.Equal(new[] { 0, 1, 2, 3, 5 }, ranks.Distinct().OrderBy(rank => rank));
        Assert.Equal(4, ranks.Count(rank => rank == 0));
        Assert.Equal(1, ranks.Count(rank => rank == 1));
        Assert.Equal(1, ranks.Count(rank => rank == 2));
    }

    [Fact]
    public void EnumerateWinnersKeepsDistinctFilesWithinOneSource()
    {
        using var root = new TestArchives.TempDirectory("bgs-gamedata-pair");
        string dataFolder = Path.Combine(root.Path, "Data");
        string low = Path.Combine(root.Path, "mods", "Low");
        string high = Path.Combine(root.Path, "mods", "High");
        Directory.CreateDirectory(dataFolder);
        Directory.CreateDirectory(low);
        Directory.CreateDirectory(high);

        Put(high, Prefix + "/pair.hkx", Bytes(1, 1));
        Put(high, Prefix + "/pair.hkt", Bytes(2, 2));
        Put(low, Prefix + "/pair.hkx", Bytes(3, 3));

        using var data = GameData.Discover(dataFolder, Path.Combine(root.Path, "plugins.txt"),
                                           new[] { low, high });

        IReadOnlyList<GameAsset> winners = data.EnumerateWinners(Prefix);

        IReadOnlyList<GameAsset> pair = Match(winners, Prefix + "/pair.hkx");
        Assert.Equal(2, pair.Count);
        Assert.All(pair, asset => Assert.Equal(high, asset.SourceRoot));
        Assert.Contains(pair, asset => asset.VirtualPath.EndsWith(".hkt", StringComparison.OrdinalIgnoreCase));
        Assert.Contains(pair, asset => asset.VirtualPath.EndsWith(".hkx", StringComparison.OrdinalIgnoreCase));
        Assert.All(pair, asset => Assert.Equal(low, Assert.Single(asset.Shadowed).SourceRoot));

        IReadOnlyList<GameAsset> hkxOnly = data.EnumerateWinners(
            Prefix, path => path.EndsWith(".hkx", StringComparison.OrdinalIgnoreCase));
        Assert.Single(Match(hkxOnly, Prefix + "/pair.hkx"));
        Assert.DoesNotContain(hkxOnly, asset => asset.VirtualPath.EndsWith(".hkt", StringComparison.OrdinalIgnoreCase));

        Assert.Empty(data.EnumerateWinners("Meshes/Actors/Canine"));
    }

    [Fact]
    public void EnumerateWinnersSkipsReparsePoints()
    {
        using var root = new TestArchives.TempDirectory("bgs-gamedata-reparse");
        string dataFolder = Path.Combine(root.Path, "Data");
        string outside = Path.Combine(root.Path, "outside", "Turret");
        Directory.CreateDirectory(Path.Combine(dataFolder, "Meshes", "Actors", "Turret"));
        Directory.CreateDirectory(outside);
        File.WriteAllBytes(Path.Combine(dataFolder, "Meshes", "Actors", "Turret", "visible.hkx"),
                           Bytes(2, 2));
        File.WriteAllBytes(Path.Combine(outside, "linked.hkx"), Bytes(1, 1));

        string link = Path.Combine(dataFolder, "Meshes", "Actors", "Turret", "linked");
        try { Directory.CreateSymbolicLink(link, outside); }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or PlatformNotSupportedException)
        {
            return;
        }

        using var data = GameData.Discover(dataFolder, Path.Combine(root.Path, "plugins.txt"));
        IReadOnlyList<GameAsset> winners = data.EnumerateWinners(Prefix);

        Assert.Contains(winners, asset => asset.VirtualPath.EndsWith("visible.hkx", StringComparison.OrdinalIgnoreCase));
        Assert.DoesNotContain(winners, asset => asset.VirtualPath.EndsWith("linked.hkx", StringComparison.OrdinalIgnoreCase));
    }

    [Fact]
    public void LoosePathsOutsideTheirRootAreRefused()
    {
        using var root = new TestArchives.TempDirectory("bgs-gamedata-root");
        string dataFolder = Path.Combine(root.Path, "Data");
        Directory.CreateDirectory(Path.Combine(dataFolder, "Meshes"));
        string file = Path.Combine(dataFolder, "Meshes", "a.hkx");
        File.WriteAllBytes(file, Bytes(1, 1));

        Assert.True(GameData.IsInsideRoot(dataFolder, file));
        Assert.True(GameData.IsInsideRoot(dataFolder, dataFolder));
        Assert.False(GameData.IsInsideRoot(dataFolder, Path.Combine(root.Path, "outside", "a.hkx")));
        Assert.False(GameData.IsInsideRoot(dataFolder, Path.Combine(dataFolder, "..", "outside.hkx")));
        Assert.False(GameData.IsInsideRoot(dataFolder, dataFolder + "-sibling"));
    }

    [Fact]
    public void ModlistPathMatchesTheFileDiscoverModdedReads()
    {
        using var root = new TestArchives.TempDirectory("bgs-gamedata-modlist");
        string dataFolder = Path.Combine(root.Path, "Data");
        string mods = Path.Combine(root.Path, "mo2");
        Directory.CreateDirectory(dataFolder);
        Directory.CreateDirectory(Path.Combine(mods, "mods", "High"));
        Directory.CreateDirectory(Path.Combine(mods, "mods", "Middle"));
        Directory.CreateDirectory(Path.Combine(mods, "mods", "Disabled"));
        Directory.CreateDirectory(Path.Combine(mods, "profiles", "Fallen World"));
        Directory.CreateDirectory(Path.Combine(mods, "overwrite"));

        string list = Path.Combine(mods, "profiles", "Fallen World", "modlist.txt");
        File.WriteAllText(list, "# comment\n+High\n+Middle\n-Disabled\n*Unmanaged\n");

        Assert.Equal(list, GameData.ModlistPath(mods, "Fallen World"));
        Assert.Null(GameData.ModlistPath(mods, null));
        Assert.Null(GameData.ModlistPath(mods, "Missing"));

        File.WriteAllText(Path.Combine(mods, "modlist.txt"), "+Middle\n");
        Assert.Equal(Path.Combine(mods, "modlist.txt"), GameData.ModlistPath(mods, null));
        Assert.Null(GameData.ModlistPath(mods, "Missing"));

        using var taken = GameData.DiscoverModded(dataFolder, mods, "Fallen World");

        string named = GameData.ModlistPath(mods, "Fallen World")!;
        var expected = File.ReadAllLines(named)
            .Reverse()
            .Select(line => line.Trim())
            .Where(line => line.StartsWith('+'))
            .Select(line => Path.Combine(mods, "mods", line[1..].Trim()))
            .Where(Directory.Exists)
            .Append(Path.Combine(mods, "overwrite"))
            .ToList();

        Assert.Equal(expected, taken.ModRoots);
        Assert.Equal(Path.Combine(mods, "mods", "Middle"), taken.ModRoots[0]);
        Assert.Equal(Path.Combine(mods, "mods", "High"), taken.ModRoots[1]);
        Assert.Equal(Path.Combine(mods, "overwrite"), taken.ModRoots[^1]);
    }

    [Theory]
    [InlineData("Meshes/Actors/Character/CharacterAssets/skeleton.hkx", true)]
    [InlineData("Meshes/Actors/Turret/CharacterAssets/Skeleton.HKX", true)]
    [InlineData("Meshes/Actors/Turret/CharacterAssets/Rig.hkx", true)]
    [InlineData("Meshes/Actors/Dogmeat/Rig/skeleton_physics.hkx", true)]
    [InlineData("skeleton.hkx", true)]
    [InlineData("Meshes/Actors/Character/Animations/Idle.hkx", false)]
    [InlineData("Meshes/Actors/Character/CharacterAssets/skeleton.hkt", false)]
    [InlineData("Meshes/Actors/Character/CharacterAssets/skeleton.nif", false)]
    [InlineData("Meshes/Actors/Turret/CharacterAssets/Idle.hkx", true)]
    [InlineData("Meshes/Weapons/skeletons/rifle.hkx", false)]
    public void IsSkeletonPathCoversCharacterAssetsAndSkeletonNames(string virtualPath, bool expected) =>
        Assert.Equal(expected, GameData.IsSkeletonPath(virtualPath));

    [Fact]
    public void ReadEntryPrefersTheExactNameWhenExtensionsShareANormalizedKey()
    {
        using var root = new TestArchives.TempDirectory("bgs-gamedata-exact");
        byte[] hkx = Bytes(1, 1);
        byte[] hkt = Bytes(2, 2);
        string archive = Path.Combine(root.Path, "tiny.ba2");
        TestArchives.WriteGeneralBa2(archive,
            (Prefix + "/pair.hkx", hkx),
            (Prefix + "/pair.hkt", hkt));

        using var data = GameData.Discover(root.Path, Path.Combine(root.Path, "plugins.txt"));

        Assert.Equal(hkt, data.ReadEntry(archive, Prefix + "/pair.hkt"));
        Assert.Equal(hkx, data.ReadEntry(archive, Prefix + "/pair.hkx"));

        IReadOnlyList<GameAsset> pair = Match(data.EnumerateWinners(Prefix), Prefix + "/pair.hkx");
        Assert.Equal(2, pair.Count);
        foreach (GameAsset asset in pair)
        {
            byte[] expected = asset.EntryName!.EndsWith(".hkt", StringComparison.OrdinalIgnoreCase) ? hkt : hkx;
            Assert.Equal(expected, data.Read(asset));
        }
    }

    [Fact]
    public void EnumerateWinnersWarnsAboutArchivesItCannotRead()
    {
        using var root = new TestArchives.TempDirectory("bgs-gamedata-warn");
        string dataFolder = Path.Combine(root.Path, "Data");
        Directory.CreateDirectory(dataFolder);

        TestArchives.WriteGeneralBa2(Path.Combine(dataFolder, "a - Good.ba2"),
                                     (Prefix + "/kept.hkx", Bytes(3, 3)));

        string full = Path.Combine(root.Path, "full.ba2");
        TestArchives.WriteGeneralBa2(full,
            (Prefix + "/gone.hkx", Bytes(9, 9)),
            (Prefix + "/also-gone.hkx", Bytes(9, 9)));
        byte[] written = File.ReadAllBytes(full);
        File.WriteAllBytes(Path.Combine(dataFolder, "z - Truncated.ba2"), written[..(written.Length / 2)]);

        string textures = Path.Combine(dataFolder, "m - Textures.ba2");
        using (var stream = File.Create(textures))
        using (var writer = new BinaryWriter(stream))
        {
            writer.Write("BTDX"u8.ToArray());
            writer.Write((uint)1);
            writer.Write("DX10"u8.ToArray());
            writer.Write((uint)0);
            writer.Write((ulong)0);
        }

        using var data = GameData.Discover(dataFolder, Path.Combine(root.Path, "plugins.txt"));

        var warnings = new List<string>();
        IReadOnlyList<GameAsset> winners = data.EnumerateWinners(Prefix, null, warnings.Add);

        Assert.Contains(winners, asset => asset.VirtualPath.EndsWith("kept.hkx", StringComparison.OrdinalIgnoreCase));
        Assert.DoesNotContain(winners, asset => asset.VirtualPath.Contains("gone", StringComparison.Ordinal));
        Assert.Single(warnings);
        Assert.Contains("Truncated.ba2", warnings[0], StringComparison.Ordinal);
    }

    [Fact]
    public void EnumerateWinnersReportsTheFailedArchivePathSeparatelyFromTheReason()
    {
        using var root = new TestArchives.TempDirectory("bgs-gamedata-warnpath");
        string dataFolder = Path.Combine(root.Path, "Data");
        Directory.CreateDirectory(dataFolder);

        string full = Path.Combine(root.Path, "full.ba2");
        TestArchives.WriteGeneralBa2(full, (Prefix + "/gone.hkx", Bytes(9, 9)));
        byte[] written = File.ReadAllBytes(full);
        string truncated = Path.Combine(dataFolder, "z - Truncated.ba2");
        File.WriteAllBytes(truncated, written[..(written.Length / 2)]);

        using var data = GameData.Discover(dataFolder, Path.Combine(root.Path, "plugins.txt"));

        var failed = new List<(string Path, string Message)>();
        data.EnumerateWinners(Prefix, null, (path, message) => failed.Add((path, message)));

        var only = Assert.Single(failed);
        Assert.Equal(truncated, only.Path);
        Assert.DoesNotContain(truncated, only.Message, StringComparison.Ordinal);
        Assert.NotEmpty(only.Message);
    }

    [Fact]
    public void ReadRefusesAnAssetOverTheHkxCapAndSaysWhy()
    {
        using var root = new TestArchives.TempDirectory("bgs-gamedata-cap");
        string dataFolder = Path.Combine(root.Path, "Data");
        Directory.CreateDirectory(dataFolder);
        using var data = GameData.Discover(dataFolder, Path.Combine(root.Path, "plugins.txt"));

        var asset = new GameAsset(Prefix + "/big.hkx", GameAssetOrigin.Archive, null,
                                  Path.Combine(dataFolder, "a.ba2"), Prefix + "/big.hkx",
                                  InputFilePolicy.MaximumHkxBytes + 1, null);

        Assert.False(data.TryRead(asset, out byte[] bytes, out string? warning));
        Assert.Empty(bytes);
        Assert.NotNull(warning);
        Assert.Contains("big.hkx", warning!, StringComparison.Ordinal);
        Assert.Contains("HKX", warning!, StringComparison.Ordinal);
        Assert.Contains("512 MiB", warning!, StringComparison.Ordinal);
        Assert.Null(data.Read(asset));
    }

    [Fact]
    public void ReadRefusesALooseFileOverTheHkxCapBeforeReadingIt()
    {
        using var root = new TestArchives.TempDirectory("bgs-gamedata-cap-loose");
        string dataFolder = Path.Combine(root.Path, "Data");
        string loose = Path.Combine(dataFolder, Prefix.Replace('/', Path.DirectorySeparatorChar), "big.hkx");
        Directory.CreateDirectory(Path.GetDirectoryName(loose)!);
        using (var stream = File.Create(loose)) stream.SetLength(InputFilePolicy.MaximumHkxBytes + 1);

        using var data = GameData.Discover(dataFolder, Path.Combine(root.Path, "plugins.txt"));

        GameAsset asset = Assert.Single(data.EnumerateWinners(Prefix));
        Assert.Equal(InputFilePolicy.MaximumHkxBytes + 1, asset.Size);

        Assert.False(data.TryRead(asset, out byte[] bytes, out string? warning));
        Assert.Empty(bytes);
        Assert.NotNull(warning);
        Assert.Contains("HKX", warning!, StringComparison.Ordinal);
    }

    [Fact]
    public void ReadRefusesAnArchiveEntryOverAnInjectableCap()
    {
        using var root = new TestArchives.TempDirectory("bgs-gamedata-cap-entry");
        string dataFolder = Path.Combine(root.Path, "Data");
        Directory.CreateDirectory(dataFolder);
        byte[] payload = Bytes(4, 4);
        TestArchives.WriteGeneralBa2(Path.Combine(dataFolder, "a - Good.ba2"),
                                     (Prefix + "/small.hkx", payload));

        using var data = GameData.Discover(dataFolder, Path.Combine(root.Path, "plugins.txt"));

        GameAsset asset = Assert.Single(data.EnumerateWinners(Prefix));
        Assert.Equal(payload.Length, asset.Size);

        data.ReadLimitBytes = payload.Length - 1;
        Assert.False(data.TryRead(asset, out byte[] refused, out string? warning));
        Assert.Empty(refused);
        Assert.NotNull(warning);
        Assert.Contains("HKX", warning!, StringComparison.Ordinal);

        data.ReadLimitBytes = InputFilePolicy.MaximumHkxBytes;
        Assert.Equal(payload, data.Read(asset));
    }

    [Fact]
    public void ReadEntryRefusesAnArchiveEntryOverTheCapBeforeItIsRead()
    {
        using var root = new TestArchives.TempDirectory("bgs-gamedata-cap-read");
        string dataFolder = Path.Combine(root.Path, "Data");
        Directory.CreateDirectory(dataFolder);
        byte[] payload = Bytes(4, 4);
        TestArchives.WriteGeneralBa2(Path.Combine(dataFolder, "a - Good.ba2"),
                                     (Prefix + "/packed.hkx", payload));

        using var data = GameData.Discover(dataFolder, Path.Combine(root.Path, "plugins.txt"));
        string archive = Assert.Single(data.ArchivePaths);
        data.ReadLimitBytes = payload.Length - 1;

        Assert.False(data.TryReadEntry(archive, Prefix + "/packed.hkx", out byte[] refused,
                                       out string? warning));
        Assert.Empty(refused);
        Assert.NotNull(warning);
        Assert.Contains("HKX", warning!, StringComparison.Ordinal);

        data.ReadLimitBytes = InputFilePolicy.MaximumHkxBytes;
        Assert.Equal(payload, data.ReadEntry(archive, Prefix + "/packed.hkx"));
    }

    [Fact]
    public void ASkeletonEntryOverTheCapIsNotReadButItsNameStillResolves()
    {
        using var root = new TestArchives.TempDirectory("bgs-gamedata-cap-skeleton");
        string dataFolder = Path.Combine(root.Path, "Data");
        Directory.CreateDirectory(dataFolder);
        byte[] skeleton = Bytes(4, 4);
        const string animation = "Meshes/Actors/Turret/Animations/Idle.hkx";
        const string packed = "Meshes/Actors/Turret/CharacterAssets/skeleton.hkx";
        TestArchives.WriteGeneralBa2(Path.Combine(dataFolder, "Fallout4 - Animations.ba2"),
                                     (animation, new byte[] { 1, 1 }), (packed, skeleton));

        using var data = GameData.Discover(dataFolder, Path.Combine(root.Path, "plugins.txt"));
        data.ReadLimitBytes = skeleton.Length - 1;

        Assert.Equal(packed, data.SkeletonEntryName(animation));
        Assert.Null(data.SkeletonBytes(animation));
        Assert.False(data.TrySkeletonEntry(animation, out string? entryName, out byte[] bytes));
        Assert.Equal(packed, entryName);
        Assert.Empty(bytes);
        Assert.False(data.TryReadSkeletonEntry(packed, out byte[] refused));
        Assert.Empty(refused);

        data.ReadLimitBytes = InputFilePolicy.MaximumHkxBytes;
        Assert.Equal(skeleton, data.SkeletonBytes(animation));
        Assert.True(data.TryReadSkeletonEntry(packed, out byte[] read));
        Assert.Equal(skeleton, read);
    }

    [Fact]
    public void ReadReportsTheIoDetailForALooseFileThatVanished()
    {
        using var root = new TestArchives.TempDirectory("bgs-gamedata-missing");
        string dataFolder = Path.Combine(root.Path, "Data");
        string loose = Path.Combine(dataFolder, Prefix.Replace('/', Path.DirectorySeparatorChar), "gone.hkx");
        Directory.CreateDirectory(Path.GetDirectoryName(loose)!);
        File.WriteAllBytes(loose, Bytes(2, 2));

        using var data = GameData.Discover(dataFolder, Path.Combine(root.Path, "plugins.txt"));
        GameAsset asset = Assert.Single(data.EnumerateWinners(Prefix));
        File.Delete(loose);

        Assert.False(data.TryRead(asset, out byte[] bytes, out string? warning));
        Assert.Empty(bytes);
        Assert.NotNull(warning);
        Assert.Contains("gone.hkx", warning!, StringComparison.OrdinalIgnoreCase);
    }

    [Theory]
    [InlineData("skeleton.hkx", true)]
    [InlineData("Skeleton.HKX", true)]
    [InlineData("skeleton_physics.hkx", true)]
    [InlineData("Meshes/Actors/Character/CharacterAssets/skeleton.hkx", true)]
    [InlineData("Meshes/Actors/Turret/CharacterAssets/Rig.hkx", false)]
    [InlineData("skeleton.hkt", false)]
    [InlineData("skeleton.nif", false)]
    [InlineData("Meshes/Weapons/skeletons/rifle.hkx", false)]
    [InlineData("", false)]
    public void IsSkeletonNameCoversSkeletonHkxNames(string fileName, bool expected) =>
        Assert.Equal(expected, GameData.IsSkeletonName(fileName));

    [Theory]
    [InlineData("Meshes/Actors/Character/CharacterAssets/skeleton.hkt", true)]
    [InlineData("Meshes/Actors/Character/CharacterAssets/Rig.hkx", true)]
    [InlineData("CharacterAssets/skeleton.hkx", true)]
    [InlineData("Meshes/Actors/Character/Animations/Idle.hkx", false)]
    [InlineData("CharacterAssets", false)]
    [InlineData("Meshes/CharacterAssets", false)]
    public void IsCharacterAssetPathCoversTheFolderRule(string virtualPath, bool expected) =>
        Assert.Equal(expected, GameData.IsCharacterAssetPath(virtualPath));

    [Fact]
    public void LaterPluginsBaseArchiveWinsOverEarlierOne()
    {
        using var root = new TestArchives.TempDirectory("bgs-gamedata-plugin-order");
        string dataFolder = Path.Combine(root.Path, "Data");
        Directory.CreateDirectory(dataFolder);

        string archiveA = Path.Combine(dataFolder, "A - Animations.ba2");
        string archiveB = Path.Combine(dataFolder, "B - Animations.ba2");
        string fallout = Path.Combine(dataFolder, "Fallout4 - Animations.ba2");
        const string anim = "Meshes/X/anim.hkx";
        TestArchives.WriteGeneralBa2(archiveA, (anim, Bytes(1, 1)));
        TestArchives.WriteGeneralBa2(archiveB, (anim, Bytes(2, 2)));
        TestArchives.WriteGeneralBa2(fallout, (anim, Bytes(3, 3)));
        string plugins = Path.Combine(root.Path, "plugins.txt");
        File.WriteAllText(plugins, "*A.esm\n*B.esm\n");

        using var data = GameData.Discover(dataFolder, plugins);
        Assert.Equal(new[] { archiveB, archiveA, fallout }, data.ArchivePaths);

        GameAsset winner = Only(data.EnumerateWinners("Meshes/X"), anim);
        Assert.Equal(archiveB, winner.ArchivePath);
        Assert.Equal(Bytes(2, 2), data.Read(winner));
        Assert.Equal(Bytes(2, 2), data.ReadAnimation(dataFolder, anim)?.Bytes);
        Assert.Equal(new[] { archiveA, fallout }, winner.Shadowed.Select(s => s.ArchivePath));
    }

    [Fact]
    public void ModArchiveOutranksBaseArchivesWithTheSamePath()
    {
        using var root = new TestArchives.TempDirectory("bgs-gamedata-mod-archive");
        string dataFolder = Path.Combine(root.Path, "Data");
        string modRoot = Path.Combine(root.Path, "mods", "Zed");
        Directory.CreateDirectory(dataFolder);
        Directory.CreateDirectory(modRoot);

        const string anim = "meshes/y/only.hkx";
        string archiveA = Path.Combine(dataFolder, "A.ba2");
        string archiveB = Path.Combine(dataFolder, "B.ba2");
        string zed = Path.Combine(modRoot, "Zed.ba2");
        TestArchives.WriteGeneralBa2(archiveA, (anim, Bytes(1, 1)));
        TestArchives.WriteGeneralBa2(archiveB, (anim, Bytes(2, 2)));
        TestArchives.WriteGeneralBa2(zed, (anim, Bytes(3, 3)));

        using var data = GameData.Discover(dataFolder, Path.Combine(root.Path, "plugins.txt"), new[] { modRoot });

        GameAsset winner = Only(data.EnumerateWinners("meshes/y"), anim);
        Assert.Equal(zed, winner.ArchivePath);
        Assert.Equal(Bytes(3, 3), data.Read(winner));
        Assert.Equal(new[] { archiveA, archiveB }, winner.Shadowed.Select(s => s.ArchivePath));
    }

    [Fact]
    public void ModRootWithoutMeshesFolderStillWinsItsSoundPrefix()
    {
        using var root = new TestArchives.TempDirectory("bgs-gamedata-voice");
        string dataFolder = Path.Combine(root.Path, "Data");
        string modRoot = Path.Combine(root.Path, "mods", "Voice");
        Directory.CreateDirectory(dataFolder);
        Put(modRoot, "sound/line.wav", Bytes(3, 3));

        using var data = GameData.Discover(dataFolder, Path.Combine(root.Path, "plugins.txt"), new[] { modRoot });

        GameAsset winner = Only(data.EnumerateWinners("Sound/"), "sound/line.wav");
        Assert.Equal(modRoot, winner.SourceRoot);
        Assert.Equal(Bytes(3, 3), data.Read(winner));
    }

    [Fact]
    public void EnumerateWinnersListsHiddenLooseFiles()
    {
        using var root = new TestArchives.TempDirectory("bgs-gamedata-hidden");
        string dataFolder = Path.Combine(root.Path, "Data");
        string name = OperatingSystem.IsWindows() ? "hidden.hkx" : ".hidden.hkx";
        string file = Path.Combine(dataFolder, "Meshes", "H", name);
        Put(dataFolder, "Meshes/H/" + name, Bytes(1, 1));
        if (OperatingSystem.IsWindows()) File.SetAttributes(file, FileAttributes.Hidden);

        using var data = GameData.Discover(dataFolder, Path.Combine(root.Path, "plugins.txt"));

        Assert.Equal(new[] { "Meshes/H/" + name }, data.EnumerateWinners("Meshes/H").Select(asset => asset.VirtualPath));
    }

    [Fact]
    public void ModLooseIndexFindsFilesUnderLowercaseMeshesRoot()
    {
        using var root = new TestArchives.TempDirectory("bgs-gamedata-lower");
        string dataFolder = Path.Combine(root.Path, "Data");
        string modRoot = Path.Combine(root.Path, "mods", "Lower");
        Directory.CreateDirectory(dataFolder);
        Put(modRoot, "meshes/h/pose.hkx", Bytes(3, 3));

        using var data = GameData.Discover(dataFolder, Path.Combine(root.Path, "plugins.txt"), new[] { modRoot });

        Assert.Equal(Bytes(3, 3), data.ReadAnimation(dataFolder, "meshes/h/pose.hkx")?.Bytes);
    }

    private static byte[] Bytes(int count, byte value)
    {
        var bytes = new byte[count];
        Array.Fill(bytes, value);
        return bytes;
    }

    private static void Put(string root, string virtualPath, byte[] bytes)
    {
        string path = Path.Combine(root, virtualPath.Replace('/', Path.DirectorySeparatorChar));
        string? folder = Path.GetDirectoryName(path);
        if (folder != null) Directory.CreateDirectory(folder);
        File.WriteAllBytes(path, bytes);
    }

    private static GameAsset Only(IReadOnlyList<GameAsset> winners, string virtualPath) =>
        Assert.Single(Match(winners, virtualPath));

    private static IReadOnlyList<GameAsset> Match(IReadOnlyList<GameAsset> winners, string virtualPath)
    {
        string key = GameData.Normalize(virtualPath);
        return winners.Where(asset => string.Equals(GameData.Normalize(asset.VirtualPath), key,
                                                     StringComparison.Ordinal)).ToList();
    }

    private static int Rank(GameAsset asset, GameData data, string high, string low, string dataFolder)
    {
        if (asset.Origin == GameAssetOrigin.Archive)
            return 3 + data.ArchivePaths.ToList().IndexOf(asset.ArchivePath!);
        if (string.Equals(asset.SourceRoot, high, StringComparison.OrdinalIgnoreCase)) return 0;
        if (string.Equals(asset.SourceRoot, low, StringComparison.OrdinalIgnoreCase)) return 1;
        return 2;
    }
}
