using System;
using System.IO;
using System.Linq;

namespace FO4RecordEditor.Services.Graph;

internal static class GraphScratch
{
    private static readonly object Gate = new();
    private static string? _root;
    private static bool _exitHooked;

    public static string Root
    {
        get
        {
            lock (Gate)
            {
                if (_root != null) return _root;
                var parent = Path.Combine(Path.GetTempPath(), "fo4re-graph-scratch");
                SweepOld(parent);
                _root = Path.Combine(parent, "p-" + Guid.NewGuid().ToString("N"));
                if (!_exitHooked)
                {
                    AppDomain.CurrentDomain.ProcessExit += (_, _) => Clear();
                    _exitHooked = true;
                }
                return _root;
            }
        }
    }

    public static void Clear()
    {
        lock (Gate)
        {
            if (_root == null) return;
            TryDelete(_root);
            _root = null;
        }
    }

    internal static void SweepOld(string parent)
    {
        try
        {
            if (!Directory.Exists(parent)) return;
            var cutoff = DateTime.UtcNow.AddHours(-24);
            foreach (var directory in Directory.EnumerateDirectories(parent).Take(64))
            {
                if (Directory.GetLastWriteTimeUtc(directory) < cutoff) TryDelete(directory);
            }
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
        }
    }

    private static void TryDelete(string path)
    {
        try
        {
            Directory.Delete(path, recursive: true);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
        }
    }
}
