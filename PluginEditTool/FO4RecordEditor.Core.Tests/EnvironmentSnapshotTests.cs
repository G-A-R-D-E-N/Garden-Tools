using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using FluentAssertions;
using FO4RecordEditor.Services;

namespace FO4RecordEditor.Core.Tests;

public sealed class EnvironmentSnapshotTests
{
    [Fact]
    public async Task Readers_always_observe_one_complete_generation()
    {
        var genA = new EnvironmentSnapshot(
            null,
            new Dictionary<string, bool> { ["A.esp"] = false },
            new Dictionary<string, string> { ["A.esp"] = "/dataA/A.esp" },
            new[] { "/dataA" });
        var genB = new EnvironmentSnapshot(
            null,
            new Dictionary<string, bool> { ["B.esp"] = true },
            new Dictionary<string, string> { ["B.esp"] = "/dataB/B.esp" },
            new[] { "/dataB" });

        var stop = false;
        var readers = Enumerable.Range(0, 4).Select(_ => Task.Run(() =>
        {
            while (!Volatile.Read(ref stop))
            {
                var snap = EnvironmentState.Current;
                if (snap == null) continue;
                var keys = snap.MasterIsEsl.Keys.ToList();
                keys.Should().BeEquivalentTo(snap.PluginSourcePaths.Keys);
                if (keys.Count > 0)
                {
                    var root = snap.AssetRoots[0];
                    if (keys.Contains("A.esp")) root.Should().Be("/dataA");
                    if (keys.Contains("B.esp")) root.Should().Be("/dataB");
                }
            }
        })).ToArray();

        var writers = Enumerable.Range(0, 2).Select(w => Task.Run(() =>
        {
            for (var i = 0; i < 500; i++)
                EnvironmentState.Publish(i % 2 == 0 ? genA : genB);
        })).ToArray();

        await Task.WhenAll(writers);
        Volatile.Write(ref stop, true);
        await Task.WhenAll(readers);
    }

    [Fact]
    public void Targeted_link_cache_swap_keeps_the_current_generation()
    {
        try
        {
            var paths = new Dictionary<string, string> { ["A.esp"] = "/dataA/A.esp" };
            var esl = new Dictionary<string, bool> { ["A.esp"] = false };
            MutagenLoader.ApplyEnvironmentPublish(null, esl, paths, new[] { "/dataA" });

            MutagenLoader.PublishLinkCache(null);

            var gen = EnvironmentState.Current!;
            gen.LinkCache.Should().BeNull();
            gen.MasterIsEsl["A.esp"].Should().BeFalse();
            gen.PluginSourcePaths["A.esp"].Should().Be("/dataA/A.esp");
            gen.AssetRoots.Should().Equal("/dataA");
        }
        finally
        {
            MutagenLoader.SeedEnvironmentForTest();
        }
    }

    [Fact]
    public void Accessors_are_empty_before_any_environment()
    {
        try
        {
            MutagenLoader.SeedEnvironmentForTest();
            MutagenLoader.LinkCache.Should().BeNull();
            MutagenLoader.PluginSourcePaths.Count.Should().Be(0);
            MutagenLoader.MasterIsEsl.Count.Should().Be(0);
            MutagenLoader.PluginSourcePaths.TryGetValue("X.esp", out _).Should().BeFalse();
        }
        finally
        {
            MutagenLoader.SeedEnvironmentForTest();
        }
    }
}
