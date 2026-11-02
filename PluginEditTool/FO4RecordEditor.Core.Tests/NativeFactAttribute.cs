using System;
using FO4RecordEditor.Services;
using Xunit;

namespace FO4RecordEditor.Core.Tests;

// Requires the skip to mirror RadicleNative.EnsureLoaded: the environment variable wins, then the app-directory probe.
public sealed class NativeFactAttribute : FactAttribute
{
    public NativeFactAttribute()
    {
        var fromEnvironment = Environment.GetEnvironmentVariable("RADICLE_NATIVE_PATH");
        if (!string.IsNullOrWhiteSpace(fromEnvironment)) return;
        if (RadicleNative.FindFirstExisting(RadicleNative.CandidatePaths(AppContext.BaseDirectory)) is null)
            Skip = "The native Radicle library is not deployed next to the test host.";
    }
}
