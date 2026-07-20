using System;
using FluentAssertions;
using FO4RecordEditor.Services;
using Xunit;

namespace FO4RecordEditor.Tests;

// MutagenLoader._modIndexCache used to grow unbounded: every plugin a sweep (search_all /
// scan_conflicts / list_records across a 650+-plugin load order) touched cached a full per-mod index
// forever, driving multi-GB long-session memory growth (bd f4se_og-ths). It is now an LRU capped at
// MaxCachedModIndexes, with actively-edited plugins (EditableMods) protected from eviction. These
// tests pin that behavior down against the real StoreModIndex/EvictModIndexLru path via the internal
// seams -- no real mod enumeration needed, so they're deterministic and fast.
//
// The tests mutate MutagenLoader's shared static cache + EditableMods + the cap; each resets that
// state on entry and the class restores the default cap on dispose. They are grouped in one
// non-collection-parallel class so they don't race each other.
[Collection("MutagenLoaderCache")]
public class MutagenLoaderCacheTests : IDisposable
{
    private readonly int _originalCap = MutagenLoader.MaxCachedModIndexes;

    public MutagenLoaderCacheTests()
    {
        MutagenLoader.ClearModIndexCacheForTest();
        MutagenLoader.EditableMods.Clear();
    }

    public void Dispose()
    {
        MutagenLoader.MaxCachedModIndexes = _originalCap;
        MutagenLoader.ClearModIndexCacheForTest();
        MutagenLoader.EditableMods.Clear();
    }

    [Fact]
    public void Cache_is_bounded_by_the_cap_and_evicts_least_recently_used()
    {
        MutagenLoader.MaxCachedModIndexes = 5;

        // Seed well past the cap. Each Seed goes through the real store+evict path.
        for (int i = 0; i < 20; i++)
            MutagenLoader.SeedModIndexForTest($"Mod{i:D2}.esp", new object());

        // Single-threaded, so eviction settles exactly at the cap.
        MutagenLoader.ModIndexCacheCount.Should().Be(5);

        // The last 5 seeded are the most-recently-used and must survive; the early ones are gone.
        for (int i = 15; i < 20; i++)
            MutagenLoader.ModIndexCacheContains($"Mod{i:D2}.esp").Should().BeTrue($"Mod{i:D2} was seeded last");
        for (int i = 0; i < 15; i++)
            MutagenLoader.ModIndexCacheContains($"Mod{i:D2}.esp").Should().BeFalse($"Mod{i:D2} is the oldest");
    }

    [Fact]
    public void Actively_edited_plugins_are_never_evicted_even_when_oldest()
    {
        MutagenLoader.MaxCachedModIndexes = 3;

        // Seed the edited plugin FIRST so it is the least-recently-used by tick, then flood the cache.
        MutagenLoader.EditableMods["MyPatch.esp"] = new object();
        MutagenLoader.SeedModIndexForTest("MyPatch.esp", new object());
        for (int i = 0; i < 20; i++)
            MutagenLoader.SeedModIndexForTest($"Vanilla{i:D2}.esp", new object());

        // Despite being the oldest, the in-edit plugin's index is protected from eviction.
        MutagenLoader.ModIndexCacheContains("MyPatch.esp").Should().BeTrue("EditableMods entries are never evicted");
    }
}
