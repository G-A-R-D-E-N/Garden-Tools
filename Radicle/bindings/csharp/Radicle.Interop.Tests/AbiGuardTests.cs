using System;
using System.IO;
using System.Reflection;
using System.Runtime.Loader;
using Radicle.Interop.Native;
using Xunit;

namespace Radicle.Interop.Tests;

public sealed class NativeArtifactFactAttribute : FactAttribute
{
    public NativeArtifactFactAttribute(string variable)
    {
        if (string.IsNullOrEmpty(Environment.GetEnvironmentVariable(variable)))
        {
            Skip = $"{variable} is not set; build the radicle_abi_* targets and run via ctest radicle_csharp_bindings or tools/run_managed_tests.py --build-dir <build>";
        }
    }
}

public class AbiGuardTests
{
    private const string MismatchType = "Radicle.Interop.RadicleAbiMismatchException: ";

    private static string Env(string variable)
    {
        var value = Environment.GetEnvironmentVariable(variable);
        Assert.False(string.IsNullOrEmpty(value), $"set {variable}");
        return value!;
    }

    private static string SystemLibrary() => OperatingSystem.IsWindows() ? "kernel32.dll" : "libc.so.6";

    private static string FirstCallThroughAFreshCopyOfTheBindings(string library)
    {
        var context = new AssemblyLoadContext("abi-guard", isCollectible: true);
        try
        {
            var bindings = context.LoadFromAssemblyPath(typeof(RadicleNative).Assembly.Location);
            bindings.GetType("Radicle.Interop.RadicleLibrary")!.GetMethod("SetPath")!.Invoke(null, new object[] { library });
            return bindings.GetType("Radicle.Interop.Native.RadicleNative")!.GetMethod("AbiMinor")!.Invoke(null, null)!.ToString()!;
        }
        catch (TargetInvocationException e) when (e.InnerException is { } inner)
        {
            return inner.GetType().FullName + ": " + inner.Message;
        }
        finally
        {
            context.Unload();
        }
    }

    [NativeArtifactFact("RADICLE_NATIVE_PATH")]
    public void TheFirstCallThroughMatchingBindingsSucceeds()
    {
        Assert.Equal(RadicleNative.CompiledAbiMinor.ToString(), FirstCallThroughAFreshCopyOfTheBindings(Env("RADICLE_NATIVE_PATH")));
    }

    // Constraint: detects the managed ABI accessors drifting from the generated constant or loading below it.
    [NativeArtifactFact("RADICLE_NATIVE_PATH")]
    public void TheManagedAbiAccessorsReportTheCompiledAndLoadedVersions()
    {
        Assert.Equal(RadicleNative.CompiledAbiMajor * 100000u + RadicleNative.CompiledAbiMinor, RadicleLibrary.AbiVersion);
        Assert.True(RadicleLibrary.LoadedAbiVersion >= RadicleLibrary.AbiVersion);
        Assert.Equal(RadicleLibrary.AbiVersion / 100000u, RadicleLibrary.LoadedAbiVersion / 100000u);
    }

    [NativeArtifactFact("RADICLE_ABI_STUB")]
    public void TheFirstCallIntoALibraryWithAnotherAbiIsRejected()
    {
        var stub = Env("RADICLE_ABI_STUB");
        var result = FirstCallThroughAFreshCopyOfTheBindings(stub);
        Assert.StartsWith(MismatchType, result);
        Assert.Contains($"reports Radicle ABI {RadicleNative.CompiledAbiMajor + 1}.{RadicleNative.CompiledAbiMinor}", result);
        Assert.Contains($"generated for ABI {RadicleNative.CompiledAbiMajor}.{RadicleNative.CompiledAbiMinor}", result);
        Assert.Contains(stub, result);
    }

    [NativeArtifactFact("RADICLE_ABI_STUB_NEWER_MINOR")]
    public void ALibraryWithTheSameMajorAndAHigherMinorIsAccepted()
    {
        var result = FirstCallThroughAFreshCopyOfTheBindings(Env("RADICLE_ABI_STUB_NEWER_MINOR"));
        Assert.Equal((RadicleNative.CompiledAbiMinor + 1).ToString(), result);
    }

    [NativeArtifactFact("RADICLE_ABI_STUB_OLDER_MINOR")]
    public void ALibraryWithTheSameMajorAndALowerMinorIsRejected()
    {
        var result = FirstCallThroughAFreshCopyOfTheBindings(Env("RADICLE_ABI_STUB_OLDER_MINOR"));
        Assert.StartsWith(MismatchType, result);
        Assert.Contains($"reports Radicle ABI {RadicleNative.CompiledAbiMajor}.{RadicleNative.CompiledAbiMinor - 1}", result);
        Assert.Contains($"generated for ABI {RadicleNative.CompiledAbiMajor}.{RadicleNative.CompiledAbiMinor}", result);
    }

    [NativeArtifactFact("RADICLE_ABI_STUB_V1")]
    public void ALibraryFromBeforeTheMaterialSwapExportsIsRejected()
    {
        var stub = Env("RADICLE_ABI_STUB_V1");
        var result = FirstCallThroughAFreshCopyOfTheBindings(stub);
        Assert.StartsWith(MismatchType, result);
        Assert.Contains("reports Radicle ABI 0.1,", result);
        Assert.Contains($"generated for ABI {RadicleNative.CompiledAbiMajor}.{RadicleNative.CompiledAbiMinor}", result);
    }

    [NativeArtifactFact("RADICLE_ABI_STUB_LEGACY")]
    public void ALibraryWithOnlyTheLegacyIntegerVersionIsRejected()
    {
        var result = FirstCallThroughAFreshCopyOfTheBindings(Env("RADICLE_ABI_STUB_LEGACY"));
        Assert.StartsWith(MismatchType, result);
        Assert.Contains("reports legacy Radicle ABI 516, which predates MAJOR.MINOR", result);
    }

    [NativeArtifactFact("RADICLE_ABI_STUB")]
    public void ARejectedLibraryIsUnloaded()
    {
        var stub = Env("RADICLE_ABI_STUB");
        var copy = Path.Combine(Path.GetTempPath(), Guid.NewGuid().ToString("N") + Path.GetExtension(stub));
        File.Copy(stub, copy);
        try
        {
            Assert.StartsWith(MismatchType, FirstCallThroughAFreshCopyOfTheBindings(copy));
            File.Delete(copy);
            Assert.False(File.Exists(copy));
        }
        finally
        {
            try { File.Delete(copy); } catch (IOException) { }
        }
    }

    [Fact]
    public void TheFirstCallIntoALibraryThatIsNotRadicleIsRejected()
    {
        var result = FirstCallThroughAFreshCopyOfTheBindings(SystemLibrary());
        Assert.StartsWith(MismatchType, result);
        Assert.Contains("not a Radicle library", result);
    }
}
