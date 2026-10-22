using System;
using System.IO;
using FluentAssertions;
using FO4RecordEditor.Services.Graph;

namespace FO4RecordEditor.Core.Tests;

public class GraphScratchTests
{
    [Fact]
    public void Compiling_publishes_into_one_process_folder_and_Clear_removes_it()
    {
        GraphScratch.Clear();
        var palette = GraphTestEnvironment.Palette();
        var graph = new GraphBuilder("Fixture", "ObjectReference");
        var entry = graph.Node(palette, "event:ObjectReference.OnActivate");
        var notify = graph.Node(palette, "global:Debug.Notification");
        graph.Value(notify, "arg:asNotificationText", "string", "\"opened\"");
        graph.Wire(entry, PinIds.Exec, notify, PinIds.Exec);

        GraphTestEnvironment.Compile(graph.Document).Success.Should().BeTrue();
        GraphTestEnvironment.Compile(graph.Document).Success.Should().BeTrue();

        var root = GraphScratch.Root;
        Directory.Exists(root).Should().BeTrue("a compile publishes its script for self-reference");
        Directory.GetDirectories(root).Length.Should().BeGreaterThanOrEqualTo(2, "each compiler instance keeps its own published script");
        Path.GetDirectoryName(root).Should().EndWith("fo4re-graph-scratch");

        GraphScratch.Clear();
        Directory.Exists(root).Should().BeFalse();
    }

    [Fact]
    public void A_scratch_folder_left_by_a_crashed_run_is_swept_and_a_recent_one_is_kept()
    {
        var parent = Path.Combine(Path.GetTempPath(), "fo4re-graph-sweep-test-" + Guid.NewGuid().ToString("N"));
        var old = Path.Combine(parent, "crashed-" + Guid.NewGuid().ToString("N"));
        var recent = Path.Combine(parent, "other-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(old);
        Directory.CreateDirectory(recent);
        File.WriteAllText(Path.Combine(old, "Old.psc"), "x");
        Directory.SetLastWriteTimeUtc(old, DateTime.UtcNow.AddDays(-2));
        try
        {
            GraphScratch.SweepOld(parent);
            Directory.Exists(old).Should().BeFalse();
            Directory.Exists(recent).Should().BeTrue();
        }
        finally
        {
            Directory.Delete(parent, true);
        }
    }

    [Fact]
    public void A_sweep_examines_at_most_64_entries_so_a_large_backlog_drains_gradually()
    {
        var parent = Path.Combine(Path.GetTempPath(), "fo4re-graph-sweep-test-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(parent);
        try
        {
            for (var i = 0; i < 65; i++)
            {
                var directory = Path.Combine(parent, "backlog-" + i);
                Directory.CreateDirectory(directory);
                Directory.SetLastWriteTimeUtc(directory, DateTime.UtcNow.AddDays(-2));
            }

            GraphScratch.SweepOld(parent);

            Directory.GetDirectories(parent).Length.Should().Be(1);
        }
        finally
        {
            Directory.Delete(parent, true);
        }
    }
}
