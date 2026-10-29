using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using OpenCommonwealth.Services.Archive;
using Xunit;

namespace BehaviourStudio.Tests;

public sealed class AnimationIndexCacheTests
{
    [Fact]
    public void TamperedCachedPathsOutsideTheSourceAreDropped()
    {
        using var root = new TempDir("bgs-index-tamper");
        string data = Path.Combine(root.Path, "Data");
        string good = Path.Combine(data, "Meshes", "Actors", "Character", "Animations", "Good.hkx");
        Directory.CreateDirectory(Path.GetDirectoryName(good)!);
        File.WriteAllBytes(good, new byte[] { 1, 2, 3 });
        string cachePath = Path.Combine(root.Path, "cache", "index.json");
        var request = new AnimationIndexRequest(data, Array.Empty<string>(), Array.Empty<string>(), null, null);

        var first = AnimationIndex.Build(request, AnimationIndexOptions.Cached(cachePath));
        Assert.Single(first.Entries);
        Assert.Equal(0, first.RejectedCacheEntries);

        var file = new AnimationIndexCache(cachePath);
        var stored = file.Load();
        Assert.NotNull(stored);
        var tampered = new List<string> { "../evil.hkx", "C:/evil.hkx", "a/../../evil.hkx" };
        var poisoned = stored! with
        {
            Sources = stored.Sources
                .Select(entry => entry.Kind == AnimationIndex.DataKind
                    ? entry with { Paths = tampered, Sizes = new List<long> { 1, 1, 1 } }
                    : entry)
                .ToList(),
        };
        Assert.True(file.Save(poisoned, out _));

        var second = AnimationIndex.Build(request, AnimationIndexOptions.Cached(cachePath));

        Assert.Equal(3, second.RejectedCacheEntries);
        Assert.Empty(second.Entries);
        Assert.DoesNotContain(second.Entries,
            entry => entry.VirtualPath.Contains("evil", StringComparison.OrdinalIgnoreCase));
        Assert.DoesNotContain(second.Entries, entry => Path.IsPathRooted(entry.Location));
        Assert.Equal(0, second.ScannedSources);
        Assert.NotNull(second.CacheNote);
        Assert.Contains("dropped", second.CacheNote!, StringComparison.Ordinal);
    }

    [Fact]
    public void TheDefaultCacheFileIsNamedForTheRequestKey()
    {
        string data = Path.Combine(Path.GetTempPath(), "bgs-index-key", "Data");
        string modlist = Path.Combine(Path.GetTempPath(), "bgs-index-key", "mods", "modlist.txt");
        var one = new AnimationIndexRequest(data, Array.Empty<string>(), Array.Empty<string>(), modlist, "Default");
        var same = new AnimationIndexRequest(data + Path.DirectorySeparatorChar, Array.Empty<string>(),
                                             Array.Empty<string>(), modlist.ToUpperInvariant(), "default");
        var otherData = new AnimationIndexRequest(data + "2", Array.Empty<string>(), Array.Empty<string>(),
                                                  modlist, "Default");
        var otherProfile = new AnimationIndexRequest(data, Array.Empty<string>(), Array.Empty<string>(),
                                                     modlist, "Other");
        var otherModlist = new AnimationIndexRequest(data, Array.Empty<string>(), Array.Empty<string>(), null, "Default");

        string path = AnimationIndex.DefaultCachePathFor(one);

        Assert.Equal(path, AnimationIndex.DefaultCachePathFor(same));
        Assert.NotEqual(path, AnimationIndex.DefaultCachePathFor(otherData));
        Assert.NotEqual(path, AnimationIndex.DefaultCachePathFor(otherProfile));
        Assert.NotEqual(path, AnimationIndex.DefaultCachePathFor(otherModlist));
        Assert.Equal(AnimationIndex.CacheFolder, Path.GetDirectoryName(path));
        Assert.Equal("animation-index-" + AnimationIndex.KeyHash(one) + ".json", Path.GetFileName(path));
        Assert.Matches("^[0-9a-f]{16}$", AnimationIndex.KeyHash(one));
        Assert.NotEqual("animation-index.json", Path.GetFileName(path));
    }

    [Fact]
    public void TheCacheRefusesOversizedFilesAndOverlargeArrays()
    {
        Assert.True(AnimationIndexCache.IsLoadableSize(AnimationIndexCache.MaxCacheBytes));
        Assert.False(AnimationIndexCache.IsLoadableSize(AnimationIndexCache.MaxCacheBytes + 1));
        Assert.False(AnimationIndexCache.IsLoadableSize(-1));

        using var root = new TempDir("bgs-index-caps");
        var file = new AnimationIndexCache(Path.Combine(root.Path, "index.json"));

        var tooMany = new List<AnimationIndexCacheSource>();
        for (int i = 0; i <= AnimationIndexCache.MaxSources; i++)
            tooMany.Add(new AnimationIndexCacheSource("loose-data", "C:/data/" + i, "loose:Data", "0:0:0:0",
                                                      new List<string>(), new List<long>()));
        Assert.True(file.Save(Data(tooMany), out _));
        Assert.Null(file.Load());

        string longPath = new string('a', AnimationIndexCache.MaxPathLength + 1) + ".hkx";
        var overlong = new List<AnimationIndexCacheSource>
        {
            new("loose-data", "C:/data", "loose:Data", "0:0:0:0", new List<string> { longPath },
                new List<long> { 1 }),
        };
        Assert.True(file.Save(Data(overlong), out _));
        Assert.Null(file.Load());

        var mismatched = new List<AnimationIndexCacheSource>
        {
            new("loose-data", "C:/data", "loose:Data", "0:0:0:0", new List<string> { "Meshes/Idle.hkx" },
                new List<long>()),
        };
        Assert.True(file.Save(Data(mismatched), out _));
        Assert.Null(file.Load());

        var sound = new List<AnimationIndexCacheSource>
        {
            new("loose-data", "C:/data", "loose:Data", "0:0:0:0", new List<string> { "Meshes/Idle.hkx" },
                new List<long> { 4 }),
        };
        Assert.True(file.Save(Data(sound), out _));
        Assert.NotNull(file.Load());
    }

    private static AnimationIndexCacheData Data(List<AnimationIndexCacheSource> sources) =>
        new(AnimationIndexCache.CurrentVersion, "C:/data", null, null, null, sources);
}
