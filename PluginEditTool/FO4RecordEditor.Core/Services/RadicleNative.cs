using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using Radicle.Interop;

namespace FO4RecordEditor.Services;

// Constraint: every managed Radicle call must set the native library path before the first P/Invoke.
public static class RadicleNative
{
    private static readonly object Gate = new();
    private static bool _configured;

    public static string LibraryName => OperatingSystem.IsWindows() ? "radicle_c.dll"
        : OperatingSystem.IsMacOS() ? "libradicle.dylib" : "libradicle.so";

    // Requires an explicit deployed library path; takes precedence over probing and the environment variable.
    public static void Configure(string path)
    {
        lock (Gate)
        {
            RadicleLibrary.SetPath(path);
            _configured = true;
        }
    }

    // Requires the first managed SWF call to run after this; RADICLE_NATIVE_PATH wins, then the layout probes.
    public static void EnsureLoaded()
    {
        lock (Gate)
        {
            if (_configured) return;
            _configured = true;
            var fromEnvironment = Environment.GetEnvironmentVariable("RADICLE_NATIVE_PATH");
            if (!string.IsNullOrWhiteSpace(fromEnvironment))
            {
                RadicleLibrary.SetPath(fromEnvironment);
                return;
            }
            var found = FindFirstExisting(CandidatePaths(AppContext.BaseDirectory));
            if (found != null) RadicleLibrary.SetPath(found);
        }
    }

    public static string? FindFirstExisting(IEnumerable<string> candidates) =>
        candidates.FirstOrDefault(File.Exists);

    // Requires the probe to stay inside the app directory; RADICLE_NATIVE_PATH is the only way to point outside it.
    public static IReadOnlyList<string> CandidatePaths(string baseDirectory)
    {
        var name = LibraryName;
        var paths = new List<string>();
        void Add(string directory) => paths.Add(Path.Combine(directory, name));
        Add(baseDirectory);
        Add(Path.Combine(baseDirectory, "native"));
        Add(Path.Combine(baseDirectory, "radicle"));
        Add(Path.Combine(baseDirectory, "runtimes", "win-x64", "native"));
        Add(Path.Combine(baseDirectory, "runtimes", "linux-x64", "native"));
        return paths.Distinct(StringComparer.OrdinalIgnoreCase).ToArray();
    }
}