using System;
using System.Text;
using Radicle.Interop.Native;
using Xunit;
using static Radicle.Interop.Tests.Bytes;
using static Radicle.Interop.Tests.PluginWriter;

namespace Radicle.Interop.Tests;

public class LifetimeTests
{
    [Fact]
    public void ABorrowedArchiveFailsOnceItsResolverIsDisposed()
    {
        using var temp = new TempDirectory();
        var archive = temp.Write("Data/Base - Main.ba2", Ba2Writer.General(("Meshes\\A.nif", Encoding.ASCII.GetBytes("a"))));
        var assets = AssetResolver.Open(null, new[] { archive });
        var borrowed = assets.Archive(0);
        Assert.Equal(1, borrowed.Count);
        assets.Dispose();
        Assert.Throws<ObjectDisposedException>(() => borrowed.Count);
    }

    [Fact]
    public void StringTablesFailOnceTheirEnvironmentIsDisposed()
    {
        using var temp = new TempDirectory();
        var plugin = temp.Write("Data/Base.esm", Build(0x81, Array.Empty<string>(),
            Top("WRLD", new Rec("WRLD", 0x800, new[] { ("FULL", U32(0x10)) }))));
        temp.Write("Data/Strings/Base_en.STRINGS", StringsWriter.Table((0x10, "World")));
        var env = RadicleEnvironment.Load(new[] { plugin });
        using var strings = StringTables.Load(env, System.IO.Path.Combine(temp.Path, "Data", "Strings"));
        var record = env.Winner(new FormKey("Base.esm", 0x800))!.Value;
        Assert.NotNull(strings.ResolveName(record));
        env.Dispose();
        Assert.Throws<ObjectDisposedException>(() => strings.ResolveName(record));
    }

    [Fact]
    public void TheLibraryPathCannotChangeOnceTheLibraryIsLoaded()
    {
        RadicleNative.AbiVersion();
        Assert.Throws<InvalidOperationException>(() => RadicleLibrary.SetPath("another-radicle-library"));
    }
}
