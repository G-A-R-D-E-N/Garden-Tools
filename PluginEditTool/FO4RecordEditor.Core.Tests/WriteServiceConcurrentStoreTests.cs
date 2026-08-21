using System;
using System.Collections.Concurrent;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using FluentAssertions;
using FO4RecordEditor.Services;

namespace FO4RecordEditor.Core.Tests;

public sealed class WriteServiceConcurrentStoreTests
{
    [Fact]
    public async Task Concurrent_create_read_and_enumerate_stress_both_stores()
    {
        const int workers = 16;
        const int perWorker = 40;
        var dir = Path.Combine(Path.GetTempPath(), "fo4re-conc-" + Guid.NewGuid().ToString("N"));
        var created = new ConcurrentBag<string>();
        using var start = new Barrier(workers);

        try
        {
            var tasks = Enumerable.Range(0, workers).Select(worker => Task.Run(() =>
            {
                start.SignalAndWait();
                for (var i = 0; i < perWorker; i++)
                {
                    var fileName = $"ConcRace_{worker}_{i}_{Guid.NewGuid():N}.esp";
                    WriteService.CreatePlugin(Path.Combine(dir, fileName));
                    created.Add(fileName);
                    _ = WriteService.GetMutable(fileName);
                    _ = WriteService.TryGetSourcePath(fileName, out _);
                    _ = WriteService.EditablePlugins().Count;
                }
            }));

            var run = async () => await Task.WhenAll(tasks);
            await run.Should().NotThrowAsync();

            foreach (var name in created)
            {
                WriteService.GetMutable(name).Should().NotBeNull();
                WriteService.TryGetSourcePath(name, out _).Should().BeTrue();
            }
        }
        finally
        {
            foreach (var name in created)
                WriteService.DiscardScriptPatch(name);
        }
    }
}
