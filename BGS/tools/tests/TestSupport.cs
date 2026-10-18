using System;

namespace BehaviourStudio.Tests;

internal static class TestFixtures
{
    public static string Path(string relative) =>
        System.IO.Path.Combine(AppContext.BaseDirectory, "fixtures/vanilla",
                               relative.Replace('/', System.IO.Path.DirectorySeparatorChar));
}
