using System;
using System.Threading;
using System.Threading.Tasks;

namespace FO4RecordEditor.Services;

public static class EnvironmentLifecycleGate
{
    internal static readonly SemaphoreSlim Gate = new(1, 1);

    internal static void Run(Action body)
    {
        Gate.Wait();
        try
        {
            body();
        }
        finally
        {
            Gate.Release();
        }
    }

    internal static T Run<T>(Func<T> body)
    {
        Gate.Wait();
        try
        {
            return body();
        }
        finally
        {
            Gate.Release();
        }
    }

    internal static async Task<T> RunAsync<T>(Func<Task<T>> body)
    {
        await Gate.WaitAsync();
        try
        {
            return await body();
        }
        finally
        {
            Gate.Release();
        }
    }
}
