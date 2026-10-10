using System;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
using Radicle.Interop.Native;
using Xunit;
using static Radicle.Interop.Tests.Bytes;
using static Radicle.Interop.Tests.PluginWriter;

namespace Radicle.Interop.Tests;

public class OwnershipTests : IClassFixture<WorldFixture>
{
    private readonly WorldFixture _world;

    public OwnershipTests(WorldFixture world) => _world = world;

    private static FormKey M(uint id) => new("Master.esm", id);

    [Fact]
    public void ARecordFromAnotherLiveEnvironmentIsAnArgumentError()
    {
        using var owner = RadicleEnvironment.Load(new[] { _world.Master });
        using var other = RadicleEnvironment.Load(new[] { _world.Master });
        using var plugin = PluginFile.Create("Foreign.esp");
        var record = owner.Winner(M(0x803))!.Value;

        Assert.Throws<ArgumentException>(() => other.Info(record));
        Assert.Throws<ArgumentException>(() => other.Container(record));
        Assert.Throws<ArgumentException>(() => plugin.CopyPlacedOverride(other, record));
        Assert.Throws<ArgumentException>(() => plugin.CopyPlacedNew(other, record));
        Assert.Equal(M(0x803), owner.Info(record).FormKey);
    }

    [Fact]
    public void ARecordAfterItsEnvironmentIsDisposedIsAnObjectDisposedError()
    {
        var env = RadicleEnvironment.Load(new[] { _world.Master });
        var record = env.Winner(M(0x803))!.Value;
        env.Dispose();

        Assert.Throws<ObjectDisposedException>(() => env.Info(record));
        Assert.Throws<ObjectDisposedException>(() => env.Container(record));
    }

    [Fact]
    public void DefaultRecordIsRejected()
    {
        using var env = RadicleEnvironment.Load(new[] { _world.Master });

        Assert.Throws<ArgumentException>(() => env.Info(default));
        Assert.Throws<ArgumentException>(() => env.Container(default));
    }

    [Fact]
    public void DisposeDefersTheNativeFreeUntilAnInFlightCallReturns()
    {
        var env = RadicleEnvironment.Load(new[] { _world.Master });
        AssertDisposeDefersNativeFree(env, () => _ = env.Plugins);
    }

    [Fact]
    public void PexDisposeDefersTheNativeFreeUntilAnInFlightCallReturns()
    {
        var pex = PexFile.FromBytes(PexModelTests.SyntheticPex());
        AssertDisposeDefersNativeFree(pex, () => pex.ToBytes());
    }

    [Fact]
    public void SeqDisposeDefersTheNativeFreeUntilAnInFlightCallReturns()
    {
        var seq = SeqFile.FromBytes(SeqModelTests.SyntheticSeq(0x07000800));
        AssertDisposeDefersNativeFree(seq, () => seq.ToBytes());
    }

    [Fact]
    public void Ba2ArchiveDisposeDefersTheNativeFreeUntilAnInFlightCallReturns()
    {
        using var temp = new TempDirectory();
        var path = temp.Write("Base - Main.ba2", Ba2Writer.General(("Meshes\\A.nif", Encoding.ASCII.GetBytes("a"))));
        var archive = Ba2Archive.Open(path);
        AssertDisposeDefersNativeFree(archive, () => _ = archive.Count);
    }

    [Fact]
    public void AssetResolverDisposeDefersTheNativeFreeUntilAnInFlightCallReturns()
    {
        using var temp = new TempDirectory();
        var archive = temp.Write("Data/Base - Main.ba2", Ba2Writer.General(("Meshes\\A.nif", Encoding.ASCII.GetBytes("a"))));
        var assets = AssetResolver.Open(null, new[] { archive });
        AssertDisposeDefersNativeFree(assets, () => _ = assets.ArchiveCount);
    }

    [Fact]
    public void StringTablesDisposeDefersTheNativeFreeUntilAnInFlightCallReturns()
    {
        using var temp = new TempDirectory();
        var plugin = temp.Write("Data/Base.esm", Build(0x81, Array.Empty<string>(),
            Top("WRLD", new Rec("WRLD", 0x800, new[] { ("FULL", U32(0x10)) }))));
        temp.Write("Data/Strings/Base_en.STRINGS", StringsWriter.Table((0x10, "World")));
        var env = RadicleEnvironment.Load(new[] { plugin });
        var strings = StringTables.Load(env, System.IO.Path.Combine(temp.Path, "Data", "Strings"));
        var record = env.Winner(new FormKey("Base.esm", 0x800))!.Value;
        AssertDisposeDefersNativeFree(strings, () => strings.ResolveName(record));
    }

    private static void AssertDisposeDefersNativeFree(IDisposable owner, Action useAfterDispose)
    {
        var handle = (SafeHandle)owner.GetType()
            .GetFields(BindingFlags.Instance | BindingFlags.NonPublic)
            .Single(f => typeof(SafeHandle).IsAssignableFrom(f.FieldType))
            .GetValue(owner)!;
        var added = false;
        handle.DangerousAddRef(ref added);

        owner.Dispose();

        Assert.True(added);
        Assert.False(handle.IsClosed);
        handle.DangerousRelease();
        Assert.True(handle.IsClosed);
        Assert.Throws<ObjectDisposedException>(useAfterDispose);
    }

    [Fact]
    public void DisposeIsIdempotentAndLaterCallsThrow()
    {
        var env = RadicleEnvironment.Load(new[] { _world.Master });
        var record = env.Winner(M(0x803))!.Value;
        env.Dispose();
        env.Dispose();
        Assert.Throws<ObjectDisposedException>(() => env.Info(record));

        var plugin = PluginFile.Create("Twice.esp");
        plugin.Dispose();
        plugin.Dispose();
        Assert.Throws<ObjectDisposedException>(() => plugin.ToBytes());
    }

    [Fact]
    public void NegativeActivateParentIndexIsAnArgumentOutOfRangeError()
    {
        using var plugin = PluginFile.Create("Delay.esp");
        Assert.Throws<ArgumentOutOfRangeException>(() => plugin.SetActivateParentDelay(M(0x803), -1, 4.0f));
    }

    [Fact]
    public void EmbeddedNulIsRejectedAndLeavesThePluginUnchanged()
    {
        using var env = RadicleEnvironment.Load(new[] { _world.Master });
        using var plugin = PluginFile.Create("Nul.esp");
        Assert.Equal(PlacedCopyStatus.Inserted, plugin.CopyPlacedOverride(env, env.Winner(M(0x803))!.Value));
        var before = plugin.ToBytes();

        Assert.Throws<ArgumentException>(() => plugin.SetEditorId(M(0x803), "a\0b"));
        Assert.Throws<ArgumentException>(() => plugin.SetComments(M(0x803), "a\0b"));
        Assert.Throws<ArgumentException>(() => plugin.SetScalar(M(0x803), "REFR\\HealthPercent", "a\0b"));
        Assert.Equal(before, plugin.ToBytes());
    }
}
