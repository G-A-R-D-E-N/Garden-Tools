using System;
using Xunit;

namespace BehaviourStudio.Tests;

internal sealed class SkinDataFactAttribute : FactAttribute
{
    public SkinDataFactAttribute()
    {
        if (string.IsNullOrEmpty(Environment.GetEnvironmentVariable("BGS_SKIN_DATA")))
            Skip = "Set BGS_SKIN_DATA to run the vanilla skin corpus.";
    }
}

internal static class TestFixtures
{
    public static string Path(string relative) =>
        System.IO.Path.Combine(AppContext.BaseDirectory, "fixtures/vanilla",
                               relative.Replace('/', System.IO.Path.DirectorySeparatorChar));
}
