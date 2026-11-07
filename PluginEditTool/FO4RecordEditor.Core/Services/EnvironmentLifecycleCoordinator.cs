using System;
using System.Threading.Tasks;

namespace FO4RecordEditor.Services;

public sealed class EnvironmentLifecycleCoordinator
{
    private readonly object _lock = new();
    private (string Key, Task Task)? _inflight;

    public Task<T> RunAsync<T>(string opKey, Func<Task<T>> start)
    {
        lock (_lock)
        {
            if (_inflight is { } cur)
            {
                if (cur.Task.IsCompleted)
                {
                    _inflight = null;
                }
                else if (string.Equals(cur.Key, opKey, StringComparison.OrdinalIgnoreCase))
                {
                    return (Task<T>)cur.Task;
                }
            }
            var task = start();
            _inflight = (opKey, task);
            return task;
        }
    }

    public Task RunAsync(string opKey, Func<Task> start)
        => RunAsync<bool>(opKey, async () =>
        {
            await start();
            return true;
        });
}
