using System;
using System.IO;
using System.Reflection;
using System.Runtime.Loader;
using Radicle.Interop.Native;
using Xunit;

namespace Radicle.Interop.Tests;

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
            return bindings.GetType("Radicle.Interop.Native.RadicleNative")!.GetMethod("AbiVersion")!.Invoke(null, null)!.ToString()!;
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

    [Fact]
    public void TheFirstCallThroughMatchingBindingsSucceeds()
    {
        Assert.Equal(RadicleNative.CompiledAbiVersion.ToString(), FirstCallThroughAFreshCopyOfTheBindings(Env("RADICLE_NATIVE_PATH")));
    }

    [Fact]
    public void TheFirstCallIntoALibraryWithAnotherAbiIsRejected()
    {
        var stub = Env("RADICLE_ABI_STUB");
        var result = FirstCallThroughAFreshCopyOfTheBindings(stub);
        Assert.StartsWith(MismatchType, result);
        Assert.Contains($"reports Radicle ABI {RadicleNative.CompiledAbiVersion + 1}", result);
        Assert.Contains($"generated for ABI {RadicleNative.CompiledAbiVersion}", result);
        Assert.Contains(stub, result);
    }

    [Fact]
    public void ALibraryFromBeforeTheMaterialSwapExportsIsRejected()
    {
        var stub = Env("RADICLE_ABI_STUB_V1");
        var result = FirstCallThroughAFreshCopyOfTheBindings(stub);
        Assert.StartsWith(MismatchType, result);
        Assert.Contains("reports Radicle ABI 1,", result);
        Assert.Contains($"generated for ABI {RadicleNative.CompiledAbiVersion}", result);
    }

    [Fact]
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
