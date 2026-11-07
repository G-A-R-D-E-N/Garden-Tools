using System;
using System.Collections.Generic;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using FluentAssertions;
using FO4RecordEditor.Models;
using FO4RecordEditor.Services;

namespace FO4RecordEditor.Core.Tests;

public sealed class EnvironmentLifecycleGateTests
{
    [Fact]
    public async Task Gate_serializes_concurrent_writers()
    {
        using var entered = new ManualResetEventSlim();
        using var release = new ManualResetEventSlim();
        var inside = 0;
        var maxInside = 0;

        try
        {
            var first = Task.Run(() =>
                EnvironmentLifecycleGate.Run(() =>
                {
                    var now = Interlocked.Increment(ref inside);
                    maxInside = Math.Max(maxInside, now);
                    entered.Set();
                    release.Wait();
                    Interlocked.Decrement(ref inside);
                }));
            entered.Wait(TimeSpan.FromSeconds(10)).Should().BeTrue();

            var second = Task.Run(() => EnvironmentLifecycleGate.Run(() => { }));
            await Task.Delay(150);
            second.IsCompleted.Should().BeFalse();

            release.Set();
            await first;
            await second;
            maxInside.Should().Be(1);
        }
        finally
        {
            release.Set();
        }
    }

    [Fact]
    public async Task Coordinator_identical_request_during_flight_shares_task()
    {
        var coord = new EnvironmentLifecycleCoordinator();
        using var entered = new ManualResetEventSlim();
        using var release = new ManualResetEventSlim();
        var runs = 0;

        Task<int> Op()
        {
            return Task.Run(() =>
            {
                Interlocked.Increment(ref runs);
                entered.Set();
                release.Wait();
                return 1;
            });
        }

        var first = coord.RunAsync("op:same", Op);
        entered.Wait(TimeSpan.FromSeconds(10)).Should().BeTrue();
        var second = coord.RunAsync("op:same", Op);
        second.Should().BeSameAs(first);
        release.Set();
        (await first).Should().Be(1);
        (await second).Should().Be(1);
        runs.Should().Be(1);
    }

    [Fact]
    public async Task Coordinator_different_request_during_flight_is_not_shared()
    {
        var coord = new EnvironmentLifecycleCoordinator();
        using var enteredA = new ManualResetEventSlim();
        using var releaseA = new ManualResetEventSlim();
        var startedA = 0;
        var startedB = 0;

        Task<int> OpA()
        {
            return Task.Run(() =>
            {
                Interlocked.Increment(ref startedA);
                enteredA.Set();
                releaseA.Wait();
                return 1;
            });
        }
        Task<int> OpB()
        {
            return Task.Run(() =>
            {
                Interlocked.Increment(ref startedB);
                return 2;
            });
        }

        var a = coord.RunAsync("op:a", OpA);
        enteredA.Wait(TimeSpan.FromSeconds(10)).Should().BeTrue();
        var b = coord.RunAsync("op:b", OpB);
        b.Should().NotBeSameAs(a);
        (await b).Should().Be(2);
        startedB.Should().Be(1);
        releaseA.Set();
        (await a).Should().Be(1);
        startedA.Should().Be(1);
    }

    [Fact]
    public async Task Coordinator_new_request_after_completion_starts_fresh()
    {
        var coord = new EnvironmentLifecycleCoordinator();
        var runs = 0;

        Task<int> Op()
        {
            return Task.Run(() => Interlocked.Increment(ref runs));
        }

        var first = await coord.RunAsync("op:seq", Op);
        first.Should().Be(1);
        var second = await coord.RunAsync("op:seq", Op);
        second.Should().Be(2);
        runs.Should().Be(2);
    }

    [Fact]
    public void Publish_block_refills_shared_state_consistently()
    {
        try
        {
            MutagenLoader.SeedEnvironmentForTest(
                masterIsEsl: new Dictionary<string, bool> { ["stale.esp"] = false },
                pluginSourcePaths: new Dictionary<string, string> { ["stale.esp"] = "old/path/stale.esp" });
            MutagenLoader.SeedModIndexForTest("stale.esp", new object());
            MutagenLoader.ModIndexCacheCount.Should().BeGreaterThan(0);

            var paths = new Dictionary<string, string>
            {
                ["A.esp"] = "/data/A.esp",
                ["B.esm"] = "/data/B.esm",
            };
            var esl = new Dictionary<string, bool>
            {
                ["A.esp"] = false,
                ["B.esm"] = true,
            };
            MutagenLoader.ApplyEnvironmentPublish(null, esl, paths, Array.Empty<string>());

            var gen = EnvironmentState.Current!;
            gen.PluginSourcePaths.ContainsKey("stale.esp").Should().BeFalse();
            gen.MasterIsEsl.ContainsKey("stale.esp").Should().BeFalse();
            gen.PluginSourcePaths["A.esp"].Should().Be("/data/A.esp");
            gen.PluginSourcePaths["B.esm"].Should().Be("/data/B.esm");
            gen.MasterIsEsl["A.esp"].Should().BeFalse();
            gen.MasterIsEsl["B.esm"].Should().BeTrue();
            MutagenLoader.ModIndexCacheCount.Should().Be(0);

            MutagenLoader.ApplyEnvironmentPublish(
                null, new Dictionary<string, bool>(), new Dictionary<string, string>(), Array.Empty<string>());
            var cleared = EnvironmentState.Current!;
            cleared.PluginSourcePaths.Count.Should().Be(0);
            cleared.MasterIsEsl.Count.Should().Be(0);
        }
        finally
        {
            MutagenLoader.ClearModIndexCacheForTest();
            MutagenLoader.SeedEnvironmentForTest();
        }
    }

    [Fact]
    public async Task LoadEsp_is_serialized_by_the_environment_gate()
    {
        var name = $"EnvGateLoad_{Guid.NewGuid():N}.esp";
        var outPath = Path.Combine(Path.GetTempPath(), name);
        try
        {
            WriteService.CreatePlugin(name).Should().Contain("Created new plugin");
            WriteService.SavePlugin(name, outPath, null).Should().Contain("Saved");

            Task<RecordNode>? load = null;
            EnvironmentLifecycleGate.Gate.Wait();
            try
            {
                load = MutagenLoader.LoadEspAsync(outPath, null);
                await Task.Delay(150);
                load.IsCompleted.Should().BeFalse();
            }
            finally
            {
                EnvironmentLifecycleGate.Gate.Release();
            }

            var node = await load!;
            node.Key.Should().Be(name);
            MutagenLoader.LooseMods.ContainsKey(name).Should().BeTrue();
        }
        finally
        {
            WriteService.DiscardScriptPatch(name);
            MutagenLoader.ReleaseLooseMod(name);
            MutagenLoader.LooseModPaths.TryRemove(name, out _);
            try { File.Delete(outPath); } catch { }
        }
    }
}
