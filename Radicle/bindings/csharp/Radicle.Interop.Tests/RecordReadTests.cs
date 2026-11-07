using System;
using System.Linq;
using Xunit;
using static Radicle.Interop.Tests.Bytes;
using static Radicle.Interop.Tests.PluginWriter;

namespace Radicle.Interop.Tests;

public class RecordReadTests
{
    [Fact]
    public void PayloadKeepsDuplicateUnknownFieldsAndResolvesWinningPluginLinks()
    {
        using var temp = new TempDirectory();
        var source = temp.Write("Master.esm", Build(1, Array.Empty<string>(),
            Top("BOOK", new Rec("BOOK", 0x800, new[] { ("EDID", Z("Book")), ("ZZZZ", new byte[] { 0, 255 }) }))));
        var patch = temp.Write("Patch.esp", Build(0, new[] { "Master.esm" },
            Top("BOOK", new Rec("BOOK", 0x800, new[]
            {
                ("EDID", Z("Book")), ("ZZZZ", new byte[] { 1, 0, 255 }), ("ZZZZ", Array.Empty<byte>()),
                ("NAME", U32(0x800)), ("NAME", U32(0x01000801)),
            })), Top("STAT", new Rec("STAT", 0x01000801, new[] { ("EDID", Z("OwnArt")) }))));
        RecordPayload payload;
        using (var env = RadicleEnvironment.Load(new[] { source, patch }))
        {
            var record = env.Winner(new FormKey("Master.esm", 0x800))!.Value;
            payload = env.ReadPayload(record);
            Assert.Equal((ushort)131, payload.FormVersion);
            Assert.Equal(new[] { "EDID", "ZZZZ", "ZZZZ", "NAME", "NAME" }, payload.Subrecords.Select(s => s.Signature));
            Assert.Equal(new byte[] { 1, 0, 255 }, payload.Subrecords[1].Bytes);
            Assert.Empty(payload.Subrecords[2].Bytes);
            Assert.Equal(new FormKey("Master.esm", 0x800), env.ResolveFormId(record, 0x800));
            Assert.Equal(new FormKey("Patch.esp", 0x801), env.ResolveFormId(record, 0x01000801));
            Assert.Null(env.ResolveFormId(record, 0));
            Assert.Null(env.ResolveFormId(record, 0xffffffff));
            Assert.Throws<RadicleException>(() => env.ResolveFormId(record, 0x02000801));
            Assert.Throws<RadicleException>(() => env.ResolveFormId(record, 0xff000801));
            Assert.Throws<ArgumentException>(() => env.ReadPayload(default));
        }
        Assert.Equal(new byte[] { 1, 0, 255 }, payload.Subrecords[1].Bytes);
    }

    [Fact]
    public void ReadsRejectARecordFromAnotherLiveEnvironment()
    {
        using var temp = new TempDirectory();
        var first = temp.Write("First.esp", Build(0, Array.Empty<string>(),
            Top("BOOK", new Rec("BOOK", 0x800, new[] { ("EDID", Z("First")) }))));
        var second = temp.Write("Second.esp", Build(0, Array.Empty<string>(),
            Top("BOOK", new Rec("BOOK", 0x800, new[] { ("EDID", Z("Second")) }))));

        using var owner = RadicleEnvironment.Load(new[] { first });
        using var other = RadicleEnvironment.Load(new[] { second });
        var record = owner.Winner(new FormKey("First.esp", 0x800))!.Value;

        Assert.Equal(new FormKey("First.esp", 0x800), owner.ResolveFormId(record, 0x800));
        Assert.Throws<ArgumentException>(() => other.ResolveFormId(record, 0x800));
        Assert.Throws<ArgumentException>(() => other.ResolveFormId(record, 0));
        Assert.Throws<ArgumentException>(() => other.ResolveFormId(record, 0xffffffff));
        Assert.Throws<ArgumentException>(() => other.ReadPayload(record));
        Assert.Throws<ArgumentException>(() => owner.ReadPayload(default));
    }

    [Fact]
    public void ExtendedPayloadIsCopiedWithoutLosingItsMarkerOrBytes()
    {
        using var temp = new TempDirectory();
        var bytes = Enumerable.Range(0, 70000).Select(i => (byte)i).ToArray();
        var source = temp.Write("Large.esp", Build(0, Array.Empty<string>(),
            Top("BOOK", new Rec("BOOK", 0x800, new[] { ("XXXX", U32((uint)bytes.Length)), ("ZZZZ", bytes) }))));
        using var env = RadicleEnvironment.Load(new[] { source });
        var record = env.Winner(new FormKey("Large.esp", 0x800))!.Value;
        var subs = env.ReadPayload(record).Subrecords;
        Assert.Equal(new[] { "XXXX", "ZZZZ" }, subs.Select(s => s.Signature));
        Assert.Equal(U32((uint)bytes.Length), subs[0].Bytes);
        Assert.False(subs[0].ExtendedSize);
        var sub = subs[1];
        Assert.Equal("ZZZZ", sub.Signature);
        Assert.True(sub.ExtendedSize);
        Assert.Equal(bytes, sub.Bytes);
    }

    [Fact]
    public void UnknownSignatureBytesRemainReversible()
    {
        using var temp = new TempDirectory();
        var plugin = Build(0, Array.Empty<string>(),
            Top("BOOK", new Rec("BOOK", 0x800, new[] { ("ZZZZ", new byte[] { 12 }) })));
        int signature = Array.FindIndex(plugin, b => b == (byte)'Z');
        Assert.True(signature >= 0);
        plugin[signature] = 0xff;
        var path = temp.Write("Unknown.esp", plugin);
        using var env = RadicleEnvironment.Load(new[] { path });
        var record = env.Winner(new FormKey("Unknown.esp", 0x800))!.Value;
        var sub = Assert.Single(env.ReadPayload(record).Subrecords);
        Assert.Equal(new byte[] { 255, 90, 90, 90 }, System.Text.Encoding.Latin1.GetBytes(sub.Signature));
        Assert.Equal(new byte[] { 12 }, sub.Bytes);
    }
}
