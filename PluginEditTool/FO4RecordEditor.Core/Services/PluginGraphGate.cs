using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading;

namespace FO4RecordEditor.Services;

public static class PluginGraphGate
{
    private static readonly ConcurrentDictionary<string, object> _locks = new(StringComparer.OrdinalIgnoreCase);

    public static string KeyOf(string plugin)
    {
        if (string.IsNullOrWhiteSpace(plugin)) return plugin;
        if ((plugin.Contains('\\') || plugin.Contains('/')) && Path.IsPathRooted(plugin))
            return Path.GetFileName(plugin);
        return plugin;
    }

    private static object LockFor(string key) => _locks.GetOrAdd(key, static _ => new object());

    public static void Run(string plugin, Action body)
    {
        lock (LockFor(KeyOf(plugin))) body();
    }

    public static T Run<T>(string plugin, Func<T> body)
    {
        lock (LockFor(KeyOf(plugin))) return body();
    }

    public static void Run(IEnumerable<string> plugins, Action body)
    {
        using var _ = Acquire(plugins);
        body();
    }

    public static T Run<T>(IEnumerable<string> plugins, Func<T> body)
    {
        using var _ = Acquire(plugins);
        return body();
    }

    public static IDisposable Acquire(IEnumerable<string> plugins)
    {
        var locks = plugins.Select(KeyOf)
            .Where(k => !string.IsNullOrWhiteSpace(k))
            .Distinct(StringComparer.OrdinalIgnoreCase)
            .OrderBy(k => k, StringComparer.OrdinalIgnoreCase)
            .Select(LockFor)
            .ToArray();
        foreach (var l in locks) Monitor.Enter(l);
        return new Releaser(locks);
    }

    private sealed class Releaser : IDisposable
    {
        private readonly object[] _locks;
        public Releaser(object[] locks) => _locks = locks;
        public void Dispose()
        {
            for (int i = _locks.Length - 1; i >= 0; i--) Monitor.Exit(_locks[i]);
        }
    }
}
