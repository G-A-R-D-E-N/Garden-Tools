using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Threading;
using BehaviourStudio.App;
using OpenCommonwealth.Services.Archive;
using Xunit;

namespace BehaviourStudio.Tests;

public sealed class AnimationIndexClassifyTests
{
    [Theory]
    [InlineData("Meshes/Actors/Character/Animations/Paired/Kill2HMBash.hkx", AnimationKind.Animation, "Character", "Character")]
    [InlineData("Meshes/Actors/Character/Behaviors/RaiderRootBehavior.hkx", AnimationKind.Behaviour, "Character", "Character")]
    [InlineData("Meshes/Actors/Character/Character.hkx", AnimationKind.Project, "Character", "Character")]
    [InlineData("Meshes/Actors/Character/Characters/RadRoachCharacter.hkx", AnimationKind.Character, "Character", "Character")]
    [InlineData("Meshes/Actors/Character/CharacterAssets/skeleton.hkx", AnimationKind.Skeleton, "Character", "Character")]
    [InlineData("Meshes/Actors/DLC03/RadChicken/Behaviors/CritterRootBehavior.hkx", AnimationKind.Behaviour, "DLC03/RadChicken", "RadChicken")]
    [InlineData("Meshes/Actors/DLC03/RadChicken/Character.hkx", AnimationKind.Project, "DLC03/RadChicken", "RadChicken")]
    [InlineData("Meshes/Animations/Weapon/WPNReload.hkx", AnimationKind.Animation, "", "")]
    [InlineData("Meshes/Animations/Weapon/44Pistol/WPNReload.hkt", AnimationKind.Animation, "", "")]
    [InlineData("Meshes/Weapons/Rifle.hkx", AnimationKind.Other, "", "")]
    [InlineData("Meshes/Actors/Character/CharacterAssets/Hair/Female/FemaleHair04.hkx", AnimationKind.Skeleton, "Character", "Character")]
    public void KindProjectAndRaceComeFromThePathAlone(string path, AnimationKind kind, string project, string race)
    {
        var entry = AnimationIndex.Classify(path, "loose:Data", 42, "C:/data/x.hkx");

        Assert.Equal(kind, entry.Kind);
        Assert.Equal(project, entry.Project);
        Assert.Equal(race, entry.Race);
        Assert.Equal(42, entry.Size);
        Assert.Equal("C:/data/x.hkx", entry.Location);
        Assert.Equal(path, entry.VirtualPath);
    }

    [Fact]
    public void BackslashesAreFoldedAndTheFolderIsRecorded()
    {
        var entry = AnimationIndex.Classify(@"Meshes\Actors\Character\Behaviors\RaiderRootBehavior.hkx", "x", 0, "");

        Assert.Equal(AnimationKind.Behaviour, entry.Kind);
        Assert.Equal("Meshes/Actors/Character/Behaviors/RaiderRootBehavior.hkx", entry.VirtualPath);
        Assert.Equal("Meshes/Actors/Character/Behaviors", entry.Folder);
        Assert.Equal("", entry.Character);
    }

    [Fact]
    public void CharacterAndSkeletonEntriesCarryTheAssetName()
    {
        Assert.Equal("RadRoachCharacter",
            AnimationIndex.Classify("Meshes/Actors/RadRoach/Characters/RadRoachCharacter.hkx", "x", 0, "").Character);
        Assert.Equal("skeleton",
            AnimationIndex.Classify("Meshes/Actors/Character/CharacterAssets/skeleton.hkx", "x", 0, "").Character);
        Assert.Equal("",
            AnimationIndex.Classify("Meshes/Actors/Character/Animations/Idle.hkx", "x", 0, "").Character);
    }

    [Fact]
    public void OnlyHavokNamesAreIndexed()
    {
        Assert.True(AnimationIndex.IsIndexedName("Meshes/Actors/Character/Animations/Idle.HKX"));
        Assert.True(AnimationIndex.IsIndexedName("Meshes/Actors/Character/Animations/Idle.hkt"));
        Assert.False(AnimationIndex.IsIndexedName("Meshes/Textures/Noise.dds"));
        Assert.False(AnimationIndex.IsIndexedName("Meshes/AnimTextData/AnimationFileData/1.txt"));
    }

    [Fact]
    public void EveryKindHasALabelAndAGroup()
    {
        foreach (AnimationKind kind in Enum.GetValues<AnimationKind>())
        {
            Assert.False(string.IsNullOrWhiteSpace(AnimationIndex.Label(kind)));
            Assert.False(string.IsNullOrWhiteSpace(AnimationIndex.GroupLabel(kind)));
        }
    }
}

public sealed class AnimationIndexBuildTests
{
    private const string ArchiveName = "Fallout4 - Animations.ba2";

    [Fact]
    public void LooseModDataAndArchiveEntriesMergeInLoadOrder()
    {
        using var scenario = new Scenario();
        scenario.AddLoose(scenario.ModFolder, "Meshes/Actors/Character/Animations/Attack1.hkx", new byte[] { 9, 9 });
        scenario.AddLoose(scenario.DataFolder, "Meshes/Actors/Character/Animations/LooseOnly.hkx", new byte[] { 1, 2, 3 });
        scenario.CopyFixture("Meshes/Actors/Character/Behaviors/RaiderRootBehavior.hkx");
        scenario.WriteArchive(
            ("Meshes/Actors/Character/Animations/Attack1.hkx", new byte[] { 4, 4, 4, 4 }),
            ("Meshes/Actors/Character/Animations/Packed.hkx", new byte[] { 5 }),
            ("Meshes/Textures/Noise.dds", new byte[] { 6, 6 }));

        var index = AnimationIndex.Build(scenario.Request, AnimationIndexOptions.None);

        Assert.Equal(4, index.Entries.Count);
        Assert.Equal(3, index.Sources.Count);
        Assert.Equal(3, index.ScannedSources);
        Assert.Equal(0, index.ReusedSources);

        var attack = index.Entries.Single(entry => entry.VirtualPath.EndsWith("Attack1.hkx", StringComparison.Ordinal));
        Assert.Equal("loose:AttackMod", attack.Source);
        Assert.Equal(2, attack.Size);
        Assert.Equal(AnimationKind.Animation, attack.Kind);
        Assert.Equal("Character", attack.Project);

        var packed = index.Entries.Single(entry => entry.VirtualPath.EndsWith("Packed.hkx", StringComparison.Ordinal));
        Assert.Equal(ArchiveName, packed.Source);
        Assert.Equal(1, packed.Size);
        Assert.Equal(Path.Combine(scenario.DataFolder, ArchiveName), packed.Location);
        Assert.Equal(AnimationKind.Animation, packed.Kind);
        Assert.Equal("Meshes/Actors/Character/Animations", packed.Folder);

        var behaviour = index.Entries.Single(entry => entry.VirtualPath.EndsWith("RaiderRootBehavior.hkx", StringComparison.Ordinal));
        Assert.Equal(AnimationKind.Behaviour, behaviour.Kind);
        Assert.Equal("loose:Data", behaviour.Source);

        Assert.DoesNotContain(index.Entries, entry => entry.VirtualPath.EndsWith(".dds", StringComparison.OrdinalIgnoreCase));
        Assert.Equal(3, index.Count(AnimationKind.Animation));
        Assert.Equal(1, index.Count(AnimationKind.Behaviour));
    }

    [Fact]
    public void EveryLooseFixtureFileIsIndexedWithItsProvenance()
    {
        using var root = new TempDir("bgs-index-fixtures");
        string data = Path.Combine(root.Path, "Data");
        CopyTree(Fixture("Meshes"), Path.Combine(data, "Meshes"));

        var request = new AnimationIndexRequest(data, Array.Empty<string>(), Array.Empty<string>(), null, null);
        var index = AnimationIndex.Build(request, AnimationIndexOptions.None);

        int onDisk = Directory.GetFiles(Path.Combine(data, "Meshes"), "*.hkx", SearchOption.AllDirectories).Length;
        Assert.Equal(onDisk, index.Entries.Count);
        Assert.Equal(2, index.Count(AnimationKind.Animation));
        Assert.Equal(6, index.Count(AnimationKind.Behaviour));
        Assert.Equal(1, index.Count(AnimationKind.Character));
        Assert.Equal(2, index.Count(AnimationKind.Skeleton));

        var chicken = index.Entries.Single(entry => entry.VirtualPath.Contains("CritterRootBehavior", StringComparison.Ordinal));
        Assert.Equal("DLC03/RadChicken", chicken.Project);
        Assert.Equal("RadChicken", chicken.Race);
        Assert.Equal("loose:Data", chicken.Source);

        var roach = index.Entries.Single(entry => entry.VirtualPath.Contains("RadRoachCharacter", StringComparison.Ordinal));
        Assert.Equal(AnimationKind.Character, roach.Kind);
        Assert.Equal("RadRoach", roach.Project);
        Assert.Equal("RadRoachCharacter", roach.Character);
        Assert.Equal("Meshes/Actors/RadRoach/Characters", roach.Folder);
    }

    [Fact]
    public void WarmBuildReusesEverySourceAndScansNothing()
    {
        using var scenario = new Scenario();
        scenario.AddLoose(scenario.DataFolder, "Meshes/Actors/Character/Animations/LooseOnly.hkx", new byte[] { 1, 2, 3 });
        scenario.WriteArchive(("Meshes/Actors/Character/Animations/Packed.hkx", new byte[] { 5 }));

        var first = AnimationIndex.Build(scenario.Request, AnimationIndexOptions.Cached(scenario.CachePath));
        Assert.True(File.Exists(scenario.CachePath));

        var second = AnimationIndex.Build(scenario.Request, AnimationIndexOptions.Cached(scenario.CachePath));

        Assert.Equal(first.Entries.Count, second.Entries.Count);
        Assert.Equal(second.Sources.Count, second.ReusedSources);
        Assert.Equal(0, second.ScannedSources);
        Assert.Null(second.CacheNote);
        Assert.Equal(first.Entries.Select(entry => entry.VirtualPath),
                     second.Entries.Select(entry => entry.VirtualPath));
    }

    [Fact]
    public void RewritingOneLooseFileRescansOnlyThatSource()
    {
        using var scenario = new Scenario();
        string loose = Path.Combine(scenario.DataFolder, "Meshes", "Actors", "Character", "Animations", "LooseOnly.hkx");
        scenario.AddLoose(scenario.DataFolder, "Meshes/Actors/Character/Animations/LooseOnly.hkx", new byte[] { 1, 2, 3 });
        scenario.WriteArchive(("Meshes/Actors/Character/Animations/Packed.hkx", new byte[] { 5 }));
        AnimationIndex.Build(scenario.Request, AnimationIndexOptions.Cached(scenario.CachePath));

        File.WriteAllBytes(loose, new byte[] { 7, 7, 7, 7, 7 });
        var grown = AnimationIndex.Build(scenario.Request, AnimationIndexOptions.Cached(scenario.CachePath));

        Assert.Equal(1, grown.ScannedSources);
        Assert.Equal(grown.Sources.Count - 1, grown.ReusedSources);
        Assert.Equal(5, grown.Entries.Single(entry => entry.VirtualPath.EndsWith("LooseOnly.hkx", StringComparison.Ordinal)).Size);

        File.SetLastWriteTimeUtc(loose, DateTime.UtcNow.AddMinutes(5));
        var touched = AnimationIndex.Build(scenario.Request, AnimationIndexOptions.Cached(scenario.CachePath));

        Assert.Equal(1, touched.ScannedSources);
        Assert.Equal(5, touched.Entries.Single(entry => entry.VirtualPath.EndsWith("LooseOnly.hkx", StringComparison.Ordinal)).Size);
    }

    [Fact]
    public void AddingAndRemovingAModFileRevealsTheShadowedArchiveEntry()
    {
        using var scenario = new Scenario();
        scenario.WriteArchive(("Meshes/Actors/Character/Animations/Attack1.hkx", new byte[] { 4, 4, 4, 4 }));

        var bare = AnimationIndex.Build(scenario.Request, AnimationIndexOptions.Cached(scenario.CachePath));
        var fromArchive = bare.Entries.Single(entry => entry.VirtualPath.EndsWith("Attack1.hkx", StringComparison.Ordinal));
        Assert.Equal(ArchiveName, fromArchive.Source);

        string modFile = Path.Combine(scenario.ModFolder, "Meshes", "Actors", "Character", "Animations", "Attack1.hkx");
        scenario.AddLoose(scenario.ModFolder, "Meshes/Actors/Character/Animations/Attack1.hkx", new byte[] { 9 });
        var modded = AnimationIndex.Build(scenario.Request, AnimationIndexOptions.Cached(scenario.CachePath));

        Assert.Equal(1, modded.ScannedSources);
        Assert.Equal(modded.Sources.Count - 1, modded.ReusedSources);
        Assert.Equal("loose:AttackMod",
            modded.Entries.Single(entry => entry.VirtualPath.EndsWith("Attack1.hkx", StringComparison.Ordinal)).Source);

        File.Delete(modFile);
        var reverted = AnimationIndex.Build(scenario.Request, AnimationIndexOptions.Cached(scenario.CachePath));

        Assert.Equal(1, reverted.ScannedSources);
        Assert.Equal(reverted.Sources.Count - 1, reverted.ReusedSources);
        Assert.Equal(ArchiveName,
            reverted.Entries.Single(entry => entry.VirtualPath.EndsWith("Attack1.hkx", StringComparison.Ordinal)).Source);
        Assert.Equal(4, reverted.Entries.Single(entry => entry.VirtualPath.EndsWith("Attack1.hkx", StringComparison.Ordinal)).Size);
    }

    [Fact]
    public void TwoDistinctFilesInOneSourceBothSurviveTheMerge()
    {
        using var scenario = new Scenario();
        scenario.AddLoose(scenario.DataFolder, "Meshes/Actors/Character/Animations/Foo.hkx", new byte[] { 1 });
        scenario.AddLoose(scenario.DataFolder, "Meshes/Actors/Character/Animations/Foo.hkt", new byte[] { 2, 2 });

        var index = AnimationIndex.Build(scenario.Request, AnimationIndexOptions.None);

        Assert.Equal(2, index.Entries.Count);
        var hkx = index.Entries.Single(entry => entry.VirtualPath.EndsWith("Foo.hkx", StringComparison.Ordinal));
        var hkt = index.Entries.Single(entry => entry.VirtualPath.EndsWith("Foo.hkt", StringComparison.Ordinal));
        Assert.Equal("loose:Data", hkx.Source);
        Assert.Equal("loose:Data", hkt.Source);
        Assert.Equal(GameData.Normalize(hkx.VirtualPath), GameData.Normalize(hkt.VirtualPath));
        Assert.Equal(1, hkx.Size);
        Assert.Equal(2, hkt.Size);
    }

    [Fact]
    public void AModStillShadowsTheSameNameInTheSourceBelowIt()
    {
        using var scenario = new Scenario();
        scenario.AddLoose(scenario.DataFolder, "Meshes/Actors/Character/Animations/Foo.hkx", new byte[] { 1 });
        scenario.AddLoose(scenario.DataFolder, "Meshes/Actors/Character/Animations/Bar.hkt", new byte[] { 2 });
        scenario.AddLoose(scenario.ModFolder, "Meshes/Actors/Character/Animations/Foo.hkt", new byte[] { 3, 3, 3 });

        var index = AnimationIndex.Build(scenario.Request, AnimationIndexOptions.None);

        Assert.Equal(2, index.Entries.Count);
        Assert.Equal("loose:AttackMod",
            index.Entries.Single(entry => entry.VirtualPath.EndsWith("Foo.hkt", StringComparison.Ordinal)).Source);
        Assert.DoesNotContain(index.Entries,
            entry => entry.VirtualPath.EndsWith("Foo.hkx", StringComparison.Ordinal));
        Assert.Contains(index.Entries,
            entry => entry.VirtualPath.EndsWith("Bar.hkt", StringComparison.Ordinal));
    }

    [Fact]
    public void ALooseRootDoesNotFollowReparsePoints()
    {
        using var root = new TempDir("bgs-index-loop");
        string data = Path.Combine(root.Path, "Data");
        string animations = Path.Combine(data, "Meshes", "Actors", "Character", "Animations");
        Directory.CreateDirectory(animations);
        File.WriteAllBytes(Path.Combine(animations, "Idle.hkx"), new byte[] { 1 });

        try
        {
            Directory.CreateSymbolicLink(Path.Combine(data, "Meshes", "loop"), data);
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or PlatformNotSupportedException)
        {
            return;
        }

        var request = new AnimationIndexRequest(data, Array.Empty<string>(), Array.Empty<string>(), null, null);
        using var cancellation = new CancellationTokenSource(TimeSpan.FromSeconds(20));
        var index = AnimationIndex.Build(request, AnimationIndexOptions.None, null, cancellation.Token);

        Assert.Single(index.Entries);
        Assert.EndsWith("Animations/Idle.hkx", index.Entries[0].VirtualPath, StringComparison.Ordinal);
    }

    [Fact]
    public void CorruptCacheIsDiscardedAndRewritten()
    {
        using var scenario = new Scenario();
        scenario.WriteArchive(("Meshes/Actors/Character/Animations/Packed.hkx", new byte[] { 5 }));
        Directory.CreateDirectory(Path.GetDirectoryName(scenario.CachePath)!);
        File.WriteAllText(scenario.CachePath, "{ this is not the index cache");

        var index = AnimationIndex.Build(scenario.Request, AnimationIndexOptions.Cached(scenario.CachePath));

        Assert.Equal(0, index.ReusedSources);
        Assert.Equal(index.Sources.Count, index.ScannedSources);
        Assert.NotNull(index.CacheNote);
        Assert.Contains("discarded", index.CacheNote!, StringComparison.Ordinal);

        var reloaded = new AnimationIndexCache(scenario.CachePath).Load();
        Assert.NotNull(reloaded);
        Assert.Equal(AnimationIndexCache.CurrentVersion, reloaded!.Version);
        Assert.Equal(index.Sources.Count, reloaded.Sources.Count);
    }

    [Fact]
    public void OldCacheVersionAndAnotherDataFolderAreNotReused()
    {
        using var scenario = new Scenario();
        scenario.WriteArchive(("Meshes/Actors/Character/Animations/Packed.hkx", new byte[] { 5 }));
        var file = new AnimationIndexCache(scenario.CachePath);
        Assert.True(file.Save(new AnimationIndexCacheData(0, scenario.DataFolder, null, null, null,
            new List<AnimationIndexCacheSource>()), out _));
        Assert.Null(file.Load());

        var rebuilt = AnimationIndex.Build(scenario.Request, AnimationIndexOptions.Cached(scenario.CachePath));
        Assert.Equal(0, rebuilt.ReusedSources);

        using var other = new Scenario();
        other.WriteArchive(("Meshes/Actors/Character/Animations/Packed.hkx", new byte[] { 5 }));
        var elsewhere = AnimationIndex.Build(other.Request, AnimationIndexOptions.Cached(scenario.CachePath));
        Assert.Equal(0, elsewhere.ReusedSources);
        Assert.Equal(elsewhere.Sources.Count, elsewhere.ScannedSources);
    }

    [Fact]
    public void TheCacheKeyFollowsTheDataFolderTheProfileAndTheModlist()
    {
        using var scenario = new Scenario();
        var request = new AnimationIndexRequest(scenario.DataFolder, Array.Empty<string>(), Array.Empty<string>(),
                                               scenario.ModlistPath, "Default");
        var data = new AnimationIndexCacheData(AnimationIndexCache.CurrentVersion, scenario.DataFolder, "Default",
            scenario.ModlistPath, "10:20", new List<AnimationIndexCacheSource>());

        Assert.True(AnimationIndexCache.MatchesKey(data, request));
        Assert.False(AnimationIndexCache.MatchesKey(data with { Profile = "Other" }, request));
        Assert.False(AnimationIndexCache.MatchesKey(data with { ModlistPath = null }, request));
        Assert.False(AnimationIndexCache.MatchesKey(data with { DataFolder = scenario.ModFolder }, request));
        Assert.True(AnimationIndexCache.MatchesKey(data with { ModlistStamp = "11:21" }, request));
    }

    [Fact]
    public void ABuildReportsProgressAndHonoursCancellation()
    {
        using var scenario = new Scenario();
        scenario.AddLoose(scenario.DataFolder, "Meshes/Actors/Character/Animations/LooseOnly.hkx", new byte[] { 1 });
        scenario.WriteArchive(("Meshes/Actors/Character/Animations/Packed.hkx", new byte[] { 5 }));

        var report = new List<AnimationIndexProgress>();
        var index = AnimationIndex.Build(scenario.Request, AnimationIndexOptions.None, report.Add);

        Assert.Equal(3, report[0].Total);
        Assert.Equal(0, report[0].Done);
        Assert.Equal("done", report[^1].Phase);
        Assert.Contains(report, step => step.Phase.StartsWith("indexed", StringComparison.Ordinal));
        Assert.Equal(index.Sources.Count, report[^1].Total);

        using var cancellation = new CancellationTokenSource();
        cancellation.Cancel();
        Assert.Throws<OperationCanceledException>(() =>
            AnimationIndex.Build(scenario.Request, AnimationIndexOptions.None, null, cancellation.Token));
    }

    [Fact]
    public void ABackgroundBuildProducesTheSameIndex()
    {
        using var scenario = new Scenario();
        scenario.WriteArchive(("Meshes/Actors/Character/Animations/Packed.hkx", new byte[] { 5 }));

        var index = AnimationIndex.BuildAsync(scenario.Request, AnimationIndexOptions.None)
            .GetAwaiter().GetResult();

        Assert.Single(index.Entries);
        Assert.Equal(AnimationKind.Animation, index.Entries[0].Kind);
    }

    private static string Fixture(string relative) =>
        Path.Combine(AppContext.BaseDirectory, "fixtures/vanilla", relative.Replace('/', Path.DirectorySeparatorChar));

    private static void CopyTree(string from, string to)
    {
        Directory.CreateDirectory(to);
        foreach (string folder in Directory.GetDirectories(from, "*", SearchOption.AllDirectories))
            Directory.CreateDirectory(Path.Combine(to, Path.GetRelativePath(from, folder)));
        foreach (string file in Directory.GetFiles(from, "*", SearchOption.AllDirectories))
        {
            string target = Path.Combine(to, Path.GetRelativePath(from, file));
            Directory.CreateDirectory(Path.GetDirectoryName(target)!);
            File.Copy(file, target, overwrite: true);
        }
    }

    private sealed class Scenario : IDisposable
    {
        private readonly TempDir _root = new("bgs-index");

        public Scenario()
        {
            DataFolder = Path.Combine(_root.Path, "Data");
            ModFolder = Path.Combine(_root.Path, "mods", "AttackMod");
            Directory.CreateDirectory(DataFolder);
            Directory.CreateDirectory(ModFolder);
            CachePath = Path.Combine(_root.Path, "cache", "index.json");
            ModlistPath = Path.Combine(_root.Path, "mods", "modlist.txt");
            File.WriteAllText(ModlistPath, "+AttackMod\n");

            Request = new AnimationIndexRequest(DataFolder, new[] { ModFolder },
                new[] { Path.Combine(DataFolder, ArchiveName) }, ModlistPath, null);
        }

        public string DataFolder { get; }

        public string ModFolder { get; }

        public string CachePath { get; }

        public string ModlistPath { get; }

        public AnimationIndexRequest Request { get; }

        public void AddLoose(string root, string relative, byte[] bytes)
        {
            string path = Path.Combine(root, relative.Replace('/', Path.DirectorySeparatorChar));
            Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            File.WriteAllBytes(path, bytes);
        }

        public void CopyFixture(string relative) =>
            AddLoose(DataFolder, relative, File.ReadAllBytes(Fixture(relative)));

        public void WriteArchive(params (string Name, byte[] Payload)[] entries) =>
            TestArchives.WriteGeneralBa2(Path.Combine(DataFolder, ArchiveName), entries);
        public void Dispose() => _root.Dispose();
    }
}

[Collection("ScanCli")]
public sealed class AnimationIndexScanCliTests
{
    [Fact]
    public void ScanIndexEmitsKindsCountsAndAWarmRebuild()
    {
        using var root = new TempDir("bgs-index-cli");
        string data = Path.Combine(root.Path, "Data");
        Directory.CreateDirectory(data);
        WriteSingleEntryArchive(Path.Combine(data, "Fallout4 - Animations.ba2"),
            "Meshes/Actors/Character/Animations/Attack1.hkx", new byte[] { 1, 2, 3 });
        string cache = Path.Combine(root.Path, "index.json");

        var cold = Capture(() => IndexScan.Run(new[] { "--scan-index", data, "--json", "--cache", cache }));
        Assert.Equal(0, cold.Exit);
        var coldJson = Parse(cold.Stdout);
        Assert.Equal("scan-index", coldJson.GetProperty("mode").GetString());
        Assert.Equal(0, coldJson.GetProperty("exitCode").GetInt32());
        Assert.Equal(ScanReport.CurrentSchemaVersion, coldJson.GetProperty("schemaVersion").GetInt32());
        Assert.Empty(coldJson.GetProperty("findings").EnumerateArray());
        Assert.Equal(1, coldJson.GetProperty("summary").GetProperty("entries").GetInt32());
        Assert.Equal(1, coldJson.GetProperty("summary").GetProperty("animation").GetInt32());
        Assert.Equal(2, coldJson.GetProperty("summary").GetProperty("scannedSources").GetInt32());
        Assert.Equal(0, coldJson.GetProperty("summary").GetProperty("cacheHit").GetInt32());
        Assert.DoesNotContain("indexing", cold.Stdout);
        Assert.Contains("indexing", cold.Stderr);

        var warm = Capture(() => IndexScan.Run(new[] { "--scan-index", data, "--json", "--cache", cache }));
        Assert.Equal(0, warm.Exit);
        var warmJson = Parse(warm.Stdout);
        Assert.Equal(0, warmJson.GetProperty("summary").GetProperty("scannedSources").GetInt32());
        Assert.Equal(2, warmJson.GetProperty("summary").GetProperty("reusedSources").GetInt32());
        Assert.Equal(1, warmJson.GetProperty("summary").GetProperty("cacheHit").GetInt32());
    }

    [Fact]
    public void ScanIndexHumanModePrintsEveryKindAndTheMissingFolderIsUsage()
    {
        using var root = new TempDir("bgs-index-cli-human");
        string data = Path.Combine(root.Path, "Data");
        Directory.CreateDirectory(data);

        var human = Capture(() => IndexScan.Run(new[] { "--scan-index", data, "--no-cache" }));
        Assert.Equal(1, human.Exit);
        Assert.Contains("animation", human.Stdout);
        Assert.Contains("skeleton", human.Stdout);
        Assert.Contains("done  0 entries", human.Stdout);

        string missing = Path.Combine(root.Path, "no-such-folder");
        var usage = Capture(() => IndexScan.Run(new[] { "--scan-index", missing, "--json" }));
        Assert.Equal(2, usage.Exit);
        var json = Parse(usage.Stdout);
        Assert.Equal("usage", json.GetProperty("findings")[0].GetProperty("kind").GetString());
    }

    private static (int Exit, string Stdout, string Stderr) Capture(Func<int> action)
    {
        var outWriter = new StringWriter();
        var errWriter = new StringWriter();
        var savedOut = Console.Out;
        var savedErr = Console.Error;
        try
        {
            Console.SetOut(outWriter);
            Console.SetError(errWriter);
            return (action(), outWriter.ToString(), errWriter.ToString());
        }
        finally
        {
            Console.SetOut(savedOut);
            Console.SetError(savedErr);
        }
    }

    private static JsonElement Parse(string stdout)
    {
        using var document = JsonDocument.Parse(stdout);
        return document.RootElement.Clone();
    }

    private static void WriteSingleEntryArchive(string path, string entryName, byte[] payload) =>
        TestArchives.WriteBa2(path, entryName, payload);
}

internal sealed class TempDir : IDisposable
{
    public TempDir(string prefix) => Path = Directory.CreateTempSubdirectory(prefix).FullName;

    public string Path { get; }

    public void Dispose()
    {
        try
        {
            Directory.Delete(Path, recursive: true);
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException)
        {
        }
    }
}
