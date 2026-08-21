using System;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using FluentAssertions;
using FO4RecordEditor.Services;
using FO4RecordEditor.Models;
using Mutagen.Bethesda.Fallout4;
using Mutagen.Bethesda.Plugins;

namespace FO4RecordEditor.Core.Tests;

public sealed class WriteServiceGraphGateTests
{
    [Fact]
    public async Task Concurrent_mutations_of_same_plugin_do_not_throw()
    {
        var name = $"MutRace_{Guid.NewGuid():N}.esp";
        const string recordId = "MutRaceWeapon";
        const int workers = 8;
        const int perWorker = 25;
        using var start = new Barrier(workers);

        try
        {
            WriteService.CreatePlugin(name).Should().Contain("Created new plugin");
            WriteService.CreateRecord(name, "WEAP", recordId, null).Should().Contain("Created WEAP");

            var tasks = Enumerable.Range(0, workers).Select(w => Task.Run(() =>
            {
                start.SignalAndWait();
                for (var i = 0; i < perWorker; i++)
                    WriteService.SetField(name, recordId, "Weight", $"{w * 100 + i}.5", null)
                        .Should().Contain("Set Weight");
            }));

            var run = async () => await Task.WhenAll(tasks);
            await run.Should().NotThrowAsync();

            WriteService.GetMutable(name).Should().NotBeNull();
        }
        finally
        {
            WriteService.DiscardScriptPatch(name);
        }
    }

    [Fact]
    public async Task Mutation_against_save_does_not_throw_and_file_round_trips()
    {
        var name = $"MutSave_{Guid.NewGuid():N}.esp";
        const string recordId = "MutSaveWeapon";
        var outPath = Path.Combine(Path.GetTempPath(), name);
        const int mutators = 8;
        const int savers = 4;
        using var start = new Barrier(mutators + savers);

        try
        {
            WriteService.CreatePlugin(name).Should().Contain("Created new plugin");
            WriteService.CreateRecord(name, "WEAP", recordId, null).Should().Contain("Created WEAP");

            var mutatorTasks = Enumerable.Range(0, mutators).Select(w => Task.Run(() =>
            {
                start.SignalAndWait();
                for (var i = 0; i < 20; i++)
                    WriteService.SetField(name, recordId, "Weight", $"{w * 100 + i}.5", null);
            }));
            var saverTasks = Enumerable.Range(0, savers).Select(s => Task.Run(() =>
            {
                start.SignalAndWait();
                for (var i = 0; i < 12; i++)
                    WriteService.SavePlugin(name, outPath, null).Should().Contain("Saved");
            }));

            var run = async () => await Task.WhenAll(mutatorTasks.Concat(saverTasks));
            await run.Should().NotThrowAsync();

            File.Exists(outPath).Should().BeTrue();
            using var reloaded = Fallout4Mod.CreateFromBinaryOverlay(
                ModPath.FromPath(outPath), Fallout4Release.Fallout4);
            reloaded.Weapons.Single(w => w.EditorID == recordId).Should().NotBeNull();
        }
        finally
        {
            WriteService.DiscardScriptPatch(name);
            try { File.Delete(outPath); } catch { }
        }
    }

    [Fact]
    public async Task Mutation_against_traversal_does_not_throw()
    {
        var name = $"MutTraverse_{Guid.NewGuid():N}.esp";
        const string recordId = "MutTraverseWeapon";
        const int mutators = 6;
        const int readers = 6;
        using var start = new Barrier(mutators + readers);

        try
        {
            WriteService.CreatePlugin(name).Should().Contain("Created new plugin");
            WriteService.CreateRecord(name, "WEAP", recordId, null).Should().Contain("Created WEAP");

            var mutatorTasks = Enumerable.Range(0, mutators).Select(w => Task.Run(() =>
            {
                start.SignalAndWait();
                for (var i = 0; i < 25; i++)
                    WriteService.SetField(name, recordId, "Weight", $"{w * 100 + i}.5", null);
            }));
            var readerTasks = Enumerable.Range(0, readers).Select(r => Task.Run(() =>
            {
                start.SignalAndWait();
                for (var i = 0; i < 25; i++)
                    MutagenLoader.GetGroups(name, null, new RecordNode { Key = name })
                        .Should().Contain(g => g.Key == "Weapon");
            }));

            var run = async () => await Task.WhenAll(mutatorTasks.Concat(readerTasks));
            await run.Should().NotThrowAsync();
        }
        finally
        {
            WriteService.DiscardScriptPatch(name);
        }
    }

    [Fact]
    public async Task Mutation_against_reload_does_not_throw()
    {
        var name = $"MutReload_{Guid.NewGuid():N}.esp";
        const string recordId = "MutReloadWeapon";
        var path = Path.Combine(Path.GetTempPath(), name);
        const int mutators = 6;
        const int reloaders = 3;
        using var start = new Barrier(mutators + reloaders);

        try
        {
            WriteService.CreatePlugin(path).Should().Contain("Created new plugin");
            WriteService.CreateRecord(name, "WEAP", recordId, null).Should().Contain("Created WEAP");
            WriteService.SavePlugin(name, path, null).Should().Contain("Saved");

            var mutatorTasks = Enumerable.Range(0, mutators).Select(w => Task.Run(() =>
            {
                start.SignalAndWait();
                for (var i = 0; i < 20; i++)
                    WriteService.SetField(name, recordId, "Weight", $"{w * 100 + i}.5", null);
            }));
            var reloaderTasks = Enumerable.Range(0, reloaders).Select(r => Task.Run(() =>
            {
                start.SignalAndWait();
                for (var i = 0; i < 8; i++)
                    WriteService.ReloadPlugin(name, null).Should().Contain("Opened");
            }));

            var run = async () => await Task.WhenAll(mutatorTasks.Concat(reloaderTasks));
            await run.Should().NotThrowAsync();

            WriteService.GetMutable(name).Should().NotBeNull();
            WriteService.SetField(name, recordId, "Weight", "9.5", null).Should().Contain("Set Weight");
        }
        finally
        {
            WriteService.DiscardScriptPatch(name);
            try { File.Delete(path); } catch { }
        }
    }
}
