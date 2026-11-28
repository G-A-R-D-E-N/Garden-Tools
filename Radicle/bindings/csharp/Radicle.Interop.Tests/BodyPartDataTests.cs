using System;
using System.Linq;
using Radicle.Interop;
using Xunit;
using static Radicle.Interop.Tests.PluginWriter;

namespace Radicle.Interop.Tests;

public class BodyPartDataTests
{
    private const string PluginName = "Bptd.esp";

    private static byte[] BodyPartDataPlugin() => Build(0, Array.Empty<string>(),
        Top("BPTD",
            new Rec("BPTD", 0x800, new[]
            {
                ("EDID", Bytes.Z("BodyParts")),
                ("BPTN", Bytes.U32(0x00000001)),
                ("BPND", Bytes.F32(1.5f, 0.25f)),
            })));

    [Fact]
    public void BodyPartDataRoundTripsByteIdentically()
    {
        using var temp = new TempDirectory();
        var source = temp.Write(PluginName, BodyPartDataPlugin());

        using var plugin = PluginFile.Read(source);

        Assert.Equal(BodyPartDataPlugin(), plugin.ToBytes());
    }

    [Fact]
    public void BodyPartDataExposesTheSubrecordsItStores()
    {
        using var temp = new TempDirectory();
        var source = temp.Write(PluginName, BodyPartDataPlugin());

        using var environment = RadicleEnvironment.Load(new[] { source });
        var records = environment.PluginRecords(0).Where(r => environment.Info(r).Signature == "BPTD").ToList();
        var record = Assert.Single(records);

        var info = environment.Info(record);
        Assert.Equal("BPTD", info.Signature);
        Assert.False(info.Deleted);

        var payload = environment.ReadPayload(record);
        Assert.Equal(new[] { "EDID", "BPTN", "BPND" }, payload.Subrecords.Select(s => s.Signature));
        Assert.Equal(Bytes.Z("BodyParts"), payload.Subrecords[0].Bytes);
        Assert.Equal(Bytes.U32(0x00000001), payload.Subrecords[1].Bytes);
        Assert.Equal(Bytes.F32(1.5f, 0.25f), payload.Subrecords[2].Bytes);
    }
}
