using System;
using System.Diagnostics;
using System.Linq;
using System.Runtime.InteropServices;
using Radicle.Interop.Native;
using Xunit;

namespace Radicle.Interop.Tests;

// source:Radicle/bindings/csharp/tests/abi_layout_probe.cpp; managed layouts must match its output.
public class AbiLayoutTests
{
    [Fact]
    public void EveryStructMatchesTheCLayout()
    {
        var probe = Environment.GetEnvironmentVariable("RADICLE_ABI_PROBE");
        Assert.False(string.IsNullOrEmpty(probe), "set RADICLE_ABI_PROBE to the abi_layout_probe executable");
        var start = new ProcessStartInfo(probe!) { RedirectStandardOutput = true };
        using var process = Process.Start(start)!;
        var lines = process.StandardOutput.ReadToEnd().Split('\n', StringSplitOptions.RemoveEmptyEntries);
        process.WaitForExit();
        Assert.Equal(0, process.ExitCode);

        var assembly = typeof(RadicleNative).Assembly;
        var checkedTypes = 0;
        foreach (var line in lines)
        {
            var parts = line.Split(' ');
            var expected = long.Parse(parts[1]);
            var name = parts[0].Split('.');
            var type = assembly.GetType("Radicle.Interop.Native." + name[0], throwOnError: true)!;
            if (name.Length == 1)
            {
                Assert.True(expected == Marshal.SizeOf(type), $"sizeof {name[0]}: C {expected}, C# {Marshal.SizeOf(type)}");
                ++checkedTypes;
            }
            else
            {
                var actual = (long)Marshal.OffsetOf(type, name[1]);
                Assert.True(expected == actual, $"offsetof {parts[0]}: C {expected}, C# {actual}");
            }
        }
        var structs = assembly.GetTypes().Count(t => t.Namespace == "Radicle.Interop.Native" && t.IsValueType && !t.IsEnum &&
                                                     !t.IsNested && !t.Name.StartsWith("Runtime", StringComparison.Ordinal));
        Assert.Equal(structs, checkedTypes);
    }
}
