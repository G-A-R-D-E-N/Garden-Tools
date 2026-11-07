using System;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using FluentAssertions;
using FO4RecordEditor.Services;

namespace FO4RecordEditor.Core.Tests;

public sealed class PluginGraphGateTests
{
    [Fact]
    public void KeyOf_strips_rooted_paths_and_keeps_bare_names()
    {
        var rooted = Path.Combine(Path.GetTempPath(), "Foo.esp");
        PluginGraphGate.KeyOf(rooted).Should().Be("Foo.esp");
        PluginGraphGate.KeyOf("Foo.esp").Should().Be("Foo.esp");
        PluginGraphGate.KeyOf("Bar.esp").Should().Be("Bar.esp");
        PluginGraphGate.KeyOf("").Should().Be("");
    }

    [Fact]
    public async Task Run_serializes_same_plugin_across_case_and_path_spellings()
    {
        var plugin = $"CaseGate_{Guid.NewGuid():N}.esp";
        var rooted = Path.Combine(Path.GetTempPath(), plugin);
        using var entered = new ManualResetEventSlim(false);
        using var release = new ManualResetEventSlim(false);

        var holder = Task.Run(() =>
            PluginGraphGate.Run(plugin, () =>
            {
                entered.Set();
                release.Wait(TimeSpan.FromSeconds(10));
            }));

        entered.Wait(TimeSpan.FromSeconds(5)).Should().BeTrue();
        var secondEntered = false;
        var racer = Task.Run(() =>
            PluginGraphGate.Run(rooted.ToUpperInvariant(), () => secondEntered = true));
        await Task.Delay(200);
        secondEntered.Should().BeFalse("case and path spellings must map to the same gate");
        release.Set();
        await Task.WhenAll(holder, racer);
        secondEntered.Should().BeTrue();
    }

    [Fact]
    public async Task Run_does_not_block_different_plugins()
    {
        var a = $"IndepA_{Guid.NewGuid():N}.esp";
        var b = $"IndepB_{Guid.NewGuid():N}.esp";
        using var aEntered = new ManualResetEventSlim(false);
        using var bEntered = new ManualResetEventSlim(false);
        using var releaseA = new ManualResetEventSlim(false);

        var taskA = Task.Run(() => PluginGraphGate.Run(a, () =>
        {
            aEntered.Set();
            releaseA.Wait(TimeSpan.FromSeconds(10));
        }));
        aEntered.Wait(TimeSpan.FromSeconds(5)).Should().BeTrue("A must hold its own plugin lock");

        var taskB = Task.Run(() => PluginGraphGate.Run(b, () => bEntered.Set()));
        bEntered.Wait(TimeSpan.FromSeconds(5))
            .Should().BeTrue("a different plugin must not be blocked while A holds its own lock");

        var cBlocked = false;
        var cEntered = false;
        var taskC = Task.Run(() =>
        {
            cEntered = true;
            PluginGraphGate.Run(a, () => cBlocked = true);
        });
        await Task.Delay(200);
        cEntered.Should().BeTrue();
        cBlocked.Should().BeFalse("the same plugin must stay blocked while A holds it");

        releaseA.Set();
        await Task.WhenAll(taskA, taskB, taskC);
        cBlocked.Should().BeTrue();
    }

    [Fact]
    public async Task Acquire_sorts_multi_plugin_locks_and_avoids_abba_deadlock()
    {
        var a = $"AbbaA_{Guid.NewGuid():N}.esp";
        var b = $"AbbaB_{Guid.NewGuid():N}.esp";
        using var firstHoldsA = new ManualResetEventSlim(false);
        using var secondAboutToTry = new ManualResetEventSlim(false);
        using var secondDone = new ManualResetEventSlim(false);

        var first = Task.Run(() =>
        {
            using var _ = PluginGraphGate.Acquire([a]);
            firstHoldsA.Set();
            secondAboutToTry.Wait(TimeSpan.FromSeconds(10));
            using var __ = PluginGraphGate.Acquire([b]);
        });
        firstHoldsA.Wait(TimeSpan.FromSeconds(5)).Should().BeTrue();

        var second = Task.Run(() =>
        {
            secondAboutToTry.Set();
            using var _ = PluginGraphGate.Acquire([b, a]);
            secondDone.Set();
        });

        await first.WaitAsync(TimeSpan.FromSeconds(10));
        first.IsCompleted.Should().BeTrue("the holder must never deadlock against a reversed-order multi-acquire");
        await second.WaitAsync(TimeSpan.FromSeconds(5));
        second.IsCompleted.Should().BeTrue();
        secondDone.IsSet.Should().BeTrue();
    }

    [Fact]
    public async Task Run_is_reentrant_for_nested_same_plugin_and_superset_acquire()
    {
        var a = $"ReentrantA_{Guid.NewGuid():N}.esp";
        var b = $"ReentrantB_{Guid.NewGuid():N}.esp";
        var reached = false;
        var t = Task.Run(() => PluginGraphGate.Run(a, () =>
        {
            PluginGraphGate.Run(a, () =>
            {
                using var _ = PluginGraphGate.Acquire([b, a]);
                reached = true;
            });
        }));
        await t.WaitAsync(TimeSpan.FromSeconds(10));
        t.IsCompleted.Should().BeTrue();
        reached.Should().BeTrue();
    }
}
