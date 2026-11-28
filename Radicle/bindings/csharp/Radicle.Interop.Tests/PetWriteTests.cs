using System;
using System.IO;
using System.Linq;
using Radicle.Interop.Native;
using Xunit;
using static Radicle.Interop.Tests.Bytes;
using static Radicle.Interop.Tests.PluginWriter;

namespace Radicle.Interop.Tests;

public class PetWriteTests : IClassFixture<WorldFixture>
{
    private const string Velocities = "REFR\\WaterCurrentVelocities";
    private readonly WorldFixture _world;

    public PetWriteTests(WorldFixture world) => _world = world;

    private static FormKey M(uint id) => new("Master.esm", id);

    private static FormKey P(uint id) => new("Pet.esp", id);

    private static byte[] FlatPlugin() => Build(0, System.Array.Empty<string>(),
        Top("FLST",
            new Rec("FLST", 0x800, System.Array.Empty<(string, byte[])>(), 0x20),
            new Rec("FLST", 0x801, System.Array.Empty<(string, byte[])>()),
            new Rec("FLST", 0x802, new[] { ("LNAM", U32(0x801)) }),
            new Rec("FLST", 0x803, new[] { ("LNAM", U32(0x800)), ("LNAM", U32(0x801)) })),
        Top("REFR", new Rec("REFR", 0x804, new[] { ("XHLT", U32(50)), ("XWCN", U32(3)), ("XWCU", F32(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12)) })));

    private static byte[] NestedPlugin() => Build(0, System.Array.Empty<string>(),
        Top("CELL", new Grp(0, 2, new Node[] { new Grp(0, 3, new Node[]
        {
            new Rec("CELL", 0x800, new[] { ("DATA", new byte[] { 1 }) }),
            new Grp(0x800, 6, new Node[] { new Grp(0x800, 8, new Node[] { new Rec("REFR", 0x801, System.Array.Empty<(string, byte[])>()) }) }),
        }) })));

    [Fact]
    public void SchemaIsDerivedFromTheTables()
    {
        var rows = RecordFieldSchema.For("REFR");
        Assert.Equal(FieldKind.Scalar, rows.Single(r => r.Path == "REFR\\HealthPercent").Kind);
        Assert.Equal(FieldKind.CountedArray, rows.Single(r => r.Path == "REFR\\WaterCurrentVelocities").Kind);
        Assert.Throws<ArgumentNullException>(() => RecordFieldSchema.For(null!));
    }

    [Fact]
    public void FieldAndListEditsRoundTripAndKeepTheCounter()
    {
        using var temp = new TempDirectory();
        var source = temp.Write("src/Pet.esp", FlatPlugin());
        var output = Path.Combine(temp.Path, "out", "Pet.esp");
        Directory.CreateDirectory(Path.GetDirectoryName(output)!);
        using (var plugin = PluginFile.Read(source))
        {
            Assert.True(plugin.SetField(P(0x804), "REFR\\HealthPercent", "75"));
            Assert.False(plugin.SetField(P(0x999), "REFR\\HealthPercent", "75"));
            Assert.Throws<RadicleException>(() => plugin.SetField(P(0x800), "REFR\\HealthPercent", "75"));

            Assert.Equal(2, plugin.ListSize(P(0x803), "FLST\\Items"));
            Assert.Throws<RadicleException>(() => plugin.ListInsert(P(0x803), "FLST\\Items", 1, P(0x850)));
            Assert.True(plugin.ListInsert(P(0x803), "FLST\\Items", 1, P(0x802)));
            Assert.True(plugin.ListRemove(P(0x803), "FLST\\Items", 0));
            Assert.False(plugin.ListInsert(P(0x803), "FLST\\Items", 9, P(0x802)));
            Assert.Equal(2, plugin.ListSize(P(0x803), "FLST\\Items"));
            Assert.False(plugin.TryListSize(P(0x999), "FLST\\Items", out _));

            Assert.Equal(3, plugin.ListSize(P(0x804), Velocities));
            Assert.True(plugin.ListInsert(P(0x804), Velocities, 1));
            Assert.Equal(4, plugin.ListSize(P(0x804), Velocities));
            Assert.True(plugin.ListRemove(P(0x804), Velocities, 0));
            Assert.Equal(3, plugin.ListSize(P(0x804), Velocities));
            plugin.Write(output);
        }

        using var env = RadicleEnvironment.Load(new[] { output });
        var refr = env.Winner(P(0x804))!.Value;
        var list = env.Winner(P(0x803))!.Value;
        Assert.Equal("75", env.GetField(refr, "REFR\\HealthPercent"));
        Assert.True(env.TryListSize(list, "FLST\\Items", out var itemCount));
        Assert.Equal(2, itemCount);
        Assert.Contains("000802", env.GetField(list, "FLST\\Items", 0));
        Assert.Null(env.GetField(list, "FLST\\Items", 5));
        var velocities = env.GetField(refr, Velocities);
        Assert.NotNull(velocities);
        Assert.Equal(32, velocities.Length);
        Assert.True(env.TryListSize(refr, Velocities, out var velocityCount));
        Assert.Equal(3, velocityCount);
        var winners = RecordFieldWinners.Read(env, "REFR\\HealthPercent");
        var winner = Assert.Single(winners);
        Assert.Equal("75", winner.Text);
        Assert.Equal(P(0x804), winner.Key);
        var schema = RecordFieldSchemaEx.For("REFR");
        Assert.Contains(schema, row => row.Path == "REFR\\HealthPercent" && row.Kind == FieldKind.Scalar);
    }

    // Regression: the fifth EXPL DATA link is the Placed Object; a schema row that moves or drops it would hand HazardHost the Impact Data Set or Spawn Projectile link, and a null link or a DATA-less EXPL must read as no object rather than a key or an error.
    [Fact]
    public void ExplosionPlacedObjectReadsTheFifthDataLink()
    {
        using var temp = new TempDirectory();
        var data = new byte[84];
        BitConverter.GetBytes(0x801u).CopyTo(data, 12);
        BitConverter.GetBytes(0x802u).CopyTo(data, 16);
        BitConverter.GetBytes(0x803u).CopyTo(data, 20);
        var path = temp.Write("src/Pet.esp", Build(0, System.Array.Empty<string>(),
            Top("EXPL",
                new Rec("EXPL", 0x800, new[] { ("DATA", data) }),
                new Rec("EXPL", 0x801, new[] { ("DATA", new byte[84]) }),
                new Rec("EXPL", 0x804, System.Array.Empty<(string, byte[])>())),
            Top("FLST", new Rec("FLST", 0x805, System.Array.Empty<(string, byte[])>()))));

        using var env = RadicleEnvironment.Load(new[] { path });
        Assert.Equal(P(0x802), env.ExplosionPlacedObject(env.Winner(P(0x800))!.Value));
        Assert.Null(env.ExplosionPlacedObject(env.Winner(P(0x801))!.Value));
        Assert.Null(env.ExplosionPlacedObject(env.Winner(P(0x804))!.Value));
        Assert.Throws<RadicleException>(() => env.ExplosionPlacedObject(env.Winner(P(0x805))!.Value));
    }

    [Fact]
    public void MaintenanceCallsMapMissingRecordsAndCounts()
    {
        using var temp = new TempDirectory();
        using var plugin = PluginFile.Read(temp.Write("src/Pet.esp", FlatPlugin()));

        Assert.Equal(new DeletedCleanup(1, 0), plugin.CleanDeleted());
        Assert.Equal(new DeletedCleanup(0, 0), plugin.CleanDeleted());
        Assert.Equal(0, plugin.RenumberAll(0x800));
        Assert.Equal(5, plugin.RenumberAll(0x900));
        Assert.Equal(P(0x910), plugin.RenumberFormId(P(0x900), 0x910));
        Assert.Null(plugin.RenumberFormId(P(0x999), 0x911));
        Assert.Throws<RadicleException>(() => plugin.RenumberFormId(P(0x901), 0x910));
        Assert.Equal(1, plugin.RetargetLinks(P(0x902), P(0x901), P(0x910)));
        Assert.Equal(0, plugin.RetargetLinks(P(0x902), P(0x901), P(0x910)));
        Assert.Null(plugin.RetargetLinks(P(0x999), P(0x901), P(0x910)));
        Assert.True(plugin.RemoveRecord(P(0x910)));
        Assert.False(plugin.RemoveRecord(P(0x910)));
    }

    [Fact]
    public void NestedRecordsBlockRenumberingAndLeaveTheBytesAlone()
    {
        using var temp = new TempDirectory();
        using var plugin = PluginFile.Read(temp.Write("src/Pet.esp", NestedPlugin()));
        var before = plugin.ToBytes();
        Assert.Equal(Status.InvalidArgument, Assert.Throws<RadicleException>(() => plugin.RenumberAll(0x900)).Status);
        Assert.Equal(Status.InvalidArgument, Assert.Throws<RadicleException>(() => plugin.RenumberFormId(P(0x801), 0x900)).Status);
        Assert.Equal(before, plugin.ToBytes());
    }

    [Fact]
    public void LocalizedFieldWriteRoundTripsThroughTheSavedStringTables()
    {
        using var temp = new TempDirectory();
        var bytes = Build(0x80, System.Array.Empty<string>(),
            Top("GMST", new Rec("GMST", 0x800, new[] { ("EDID", Z("sLabel")), ("DATA", U32(0x1234)) })));
        var source = temp.Write("src/Pet.esp", bytes);
        temp.Write("src/Strings/Pet_en.STRINGS", StringsWriter.Table((0x1234, "Label")));
        var output = Path.Combine(temp.Path, "out", "Pet.esp");
        var outStrings = Path.Combine(temp.Path, "out", "Strings");
        Directory.CreateDirectory(Path.GetDirectoryName(output)!);
        using (var plugin = PluginFile.Read(source))
        {
            Assert.Equal(Status.InvalidArgument, Assert.Throws<RadicleException>(() => plugin.SetField(P(0x800), "GMST\\StringValue", "x")).Status);
            Assert.Equal(Status.InvalidArgument, Assert.Throws<RadicleException>(() => plugin.SaveStrings(outStrings, "en")).Status);
            plugin.LoadStrings(Path.Combine(temp.Path, "src", "Strings"), "en");
            Assert.True(plugin.SetField(P(0x800), "GMST\\StringValue", "Café"));
            plugin.Write(output);
            plugin.SaveStrings(outStrings, "en");
        }

        using var env = RadicleEnvironment.Load(new[] { output });
        using var strings = StringTables.Load(env, outStrings);
        Assert.Equal("Café", strings.RecordText(env.Winner(P(0x800))!.Value, "GMST\\StringValue"));
    }

    [Fact]
    public void CopyRecordVersionInsertsReplacesAndTheLightFlagReadsBack()
    {
        using var env = RadicleEnvironment.Load(new[] { _world.Master });
        using var plugin = PluginFile.Create("Patch.esp");
        var version = env.Winner(M(0x810))!.Value;
        Assert.Equal(RecordCopyStatus.Inserted, plugin.CopyRecordVersion(env, version));
        Assert.Equal(RecordCopyStatus.AlreadyExists, plugin.CopyRecordVersion(env, version));
        Assert.Equal(RecordCopyStatus.Replaced, plugin.CopyRecordVersion(env, version, overwrite: true));
        Assert.True(plugin.RemoveRecord(M(0x810)));

        plugin.SetLightFlag(true);
        Assert.NotEqual(0u, BitConverter.ToUInt32(plugin.ToBytes(), 8) & 0x200u);
        plugin.SetLightFlag(false);
        Assert.Equal(0u, BitConverter.ToUInt32(plugin.ToBytes(), 8) & 0x200u);
    }
}
