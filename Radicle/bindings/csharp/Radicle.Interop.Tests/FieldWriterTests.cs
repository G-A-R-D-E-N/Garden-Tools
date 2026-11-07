using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using Radicle.Interop.Native;
using Xunit;
using static Radicle.Interop.Tests.Bytes;
using static Radicle.Interop.Tests.PluginWriter;

namespace Radicle.Interop.Tests;

public class FieldWriterTests
{
    private const string Plugin = "Fields.esp";
    private static readonly string[] Masters = { "Master.esm" };

    private static Radicle.Interop.FormKey K(uint id) => new(Plugin, id);

    private static Radicle.Interop.FormKey M(uint id) => new("Master.esm", id);

    private static readonly Radicle.Interop.FormKey Flst = K(0x810);
    private static readonly Radicle.Interop.FormKey Refr = K(0x820);
    private static readonly Radicle.Interop.FormKey Expl = K(0x830);
    private static readonly Radicle.Interop.FormKey Race = K(0x840);

    private static byte[] Fixture()
    {
        var explData = new byte[84];
        BitConverter.GetBytes(0x801u).CopyTo(explData, 0);
        return Build(0, Masters,
            Top("FLST", new Rec("FLST", 0x01000810, new[] { ("EDID", Z("List")), ("LNAM", U32(0x801)), ("LNAM", U32(0x802)) })),
            Top("REFR", new Rec("REFR", 0x01000820, new[] { ("XHLT", U32(50)) })),
            Top("EXPL", new Rec("EXPL", 0x01000830, new[] { ("DATA", explData) })),
            Top("RACE", new Rec("RACE", 0x01000840, new[] { ("RPRM", U32(0x801)), ("NAME", Z("Hand")), ("NAME", Z("Foot")), ("HNAM", Z("hair.dds")) })));
    }

    private static PluginFile Open(TempDirectory temp) => PluginFile.Read(temp.Write(Plugin, Fixture()));

    private static bool Contains(byte[] haystack, byte[] needle) =>
        haystack.AsSpan().IndexOf(needle) >= 0;

    private static PluginFile SaveAndReload(PluginFile plugin, TempDirectory temp, out byte[] bytes)
    {
        var output = Path.Combine(temp.Path, "saved", Plugin);
        Directory.CreateDirectory(Path.GetDirectoryName(output)!);
        plugin.Write(output);
        bytes = File.ReadAllBytes(output);
        return PluginFile.Read(output);
    }

    [Fact]
    public void SetScalarEditsByIndexAndMapsMisses()
    {
        using var temp = new TempDirectory();
        using var plugin = Open(temp);

        Assert.True(plugin.SetScalar(Refr, "REFR\\HealthPercent", "75"));
        Assert.True(plugin.SetScalar(Refr, "REFR\\HealthPercent", "80", 0));
        Assert.True(plugin.SetScalar(Expl, "EXPL\\Damage", "40"));
        Assert.False(plugin.SetScalar(Refr, "REFR\\HealthPercent", "80", 1));
        Assert.False(plugin.SetScalar(Refr, "REFR\\Scale", "1.5"));
        Assert.False(plugin.SetScalar(K(0x999), "REFR\\HealthPercent", "1"));
        Assert.Throws<ArgumentOutOfRangeException>(() => plugin.SetScalar(Refr, "REFR\\HealthPercent", "1", -1));
        Assert.Throws<ArgumentNullException>(() => plugin.SetScalar(Refr, null!, "1"));

        foreach (var (record, path, value) in new[]
                 {
                     (Refr, "REFR\\Nope", "1"),
                     (Refr, "REFR\\HealthPercent", "abc"),
                     (Refr, "REFR\\HealthPercent", "150"),
                     (Expl, "REFR\\HealthPercent", "1"),
                     (Race, "RACE\\BipedObjectNames", "x"),
                 })
        {
            var error = Assert.Throws<RadicleException>(() => plugin.SetScalar(record, path, value));
            Assert.Equal(Status.InvalidArgument, error.Status);
        }

        using var reloaded = SaveAndReload(plugin, temp, out var bytes);
        Assert.True(Contains(bytes, Encoding.ASCII.GetBytes("XHLT").Concat(new byte[] { 4, 0, 80, 0, 0, 0 }).ToArray()));
        Assert.True(Contains(bytes, F32(40f)));
    }

    [Fact]
    public void SetFormLinkEditsAndClearsLinks()
    {
        using var temp = new TempDirectory();
        using var plugin = Open(temp);

        Assert.True(plugin.SetFormLink(Expl, "EXPL\\Light", M(0x900)));
        Assert.True(plugin.SetFormLink(Expl, "EXPL\\Light", M(0x900), 0));
        Assert.True(plugin.SetFormLink(Expl, "EXPL\\Sound1", null));
        Assert.False(plugin.SetFormLink(Expl, "EXPL\\Light", M(0x900), 1));
        Assert.False(plugin.SetFormLink(K(0x999), "EXPL\\Light", M(0x900)));
        Assert.Throws<ArgumentOutOfRangeException>(() => plugin.SetFormLink(Expl, "EXPL\\Light", null, -1));

        foreach (var (record, path, target) in new[]
                 {
                     (Expl, "EXPL\\Nope", (Radicle.Interop.FormKey?)M(0x900)),
                     (Refr, "EXPL\\Light", M(0x900)),
                     (Expl, "EXPL\\Light", new Radicle.Interop.FormKey("Other.esm", 0x900)),
                     (Expl, "EXPL\\Light", Flst),
                 })
        {
            var error = Assert.Throws<RadicleException>(() => plugin.SetFormLink(record, path, target));
            Assert.Equal(Status.InvalidArgument, error.Status);
        }

        using var reloaded = SaveAndReload(plugin, temp, out var bytes);
        var data = Encoding.ASCII.GetBytes("DATA").Concat(new byte[] { 84, 0, 0x00, 0x09, 0, 0, 0, 0, 0, 0 }).ToArray();
        Assert.True(Contains(bytes, data));
    }

    [Fact]
    public void SetStringEditsOccurrences()
    {
        using var temp = new TempDirectory();
        using var plugin = Open(temp);

        Assert.True(plugin.SetString(Race, "RACE\\BipedObjectNames", "Torso", 1));
        Assert.True(plugin.SetString(Race, "RACE\\BipedObjectNames", "Torso", 1));
        Assert.True(plugin.SetString(Race, "RACE\\HairColorLookupTexture", "new.dds"));
        Assert.False(plugin.SetString(Race, "RACE\\BipedObjectNames", "x", 2));
        Assert.False(plugin.SetString(Race, "RACE\\HairColorExtendedLookupTexture", "x"));
        Assert.False(plugin.SetString(K(0x999), "RACE\\BipedObjectNames", "x"));
        Assert.Throws<ArgumentException>(() => plugin.SetString(Race, "RACE\\BipedObjectNames", "a\0b"));
        Assert.Throws<ArgumentNullException>(() => plugin.SetString(Race, "RACE\\BipedObjectNames", null!));

        var error = Assert.Throws<RadicleException>(() => plugin.SetString(Race, "RACE\\Nope", "x"));
        Assert.Equal(Status.InvalidArgument, error.Status);
        error = Assert.Throws<RadicleException>(() => plugin.SetString(Refr, "REFR\\HealthPercent", "x"));
        Assert.Equal(Status.InvalidArgument, error.Status);

        using var reloaded = SaveAndReload(plugin, temp, out var bytes);
        Assert.True(Contains(bytes, Z("Torso")));
        Assert.True(Contains(bytes, Z("new.dds")));
        Assert.True(Contains(bytes, Z("Hand")));
        Assert.False(Contains(bytes, Z("Foot")));
        Assert.False(Contains(bytes, Z("hair.dds")));
    }

    [Fact]
    public void SetStringStoresWindows1252AndRefusesUnrepresentableText()
    {
        using var temp = new TempDirectory();
        using var plugin = Open(temp);

        Assert.True(plugin.SetString(Race, "RACE\\HairColorLookupTexture", "caf\u00e9\u20ac"));
        var error = Assert.Throws<RadicleException>(() => plugin.SetString(Race, "RACE\\HairColorLookupTexture", "\u0100"));
        Assert.Equal(Status.InvalidArgument, error.Status);

        using var reloaded = SaveAndReload(plugin, temp, out var bytes);
        Assert.True(Contains(bytes, new byte[] { (byte)'c', (byte)'a', (byte)'f', 0xE9, 0x80, 0 }));
    }

    [Fact]
    public void EmbeddedNulInWritePathsIsRefusedBeforeNativeTruncatesIt()
    {
        using var temp = new TempDirectory();
        using var plugin = Open(temp);
        var before = plugin.ToBytes();

        Assert.Throws<ArgumentException>(() => plugin.SetScalar(Refr, "REFR\\HealthPercent\0junk", "80"));
        Assert.Throws<ArgumentException>(() => plugin.SetString(Race, "RACE\\BipedObjectNames\0junk", "x"));
        Assert.Throws<ArgumentException>(() => plugin.SetFormLink(Expl, "EXPL\\Light\0junk", M(0x900)));
        Assert.Throws<ArgumentException>(() => plugin.FormLinkListInsert(Flst, "FLST\\Items\0junk", 0, M(0x900)));
        Assert.Throws<ArgumentException>(() => plugin.AddSubrecord(Refr, "XHLT\0junk"));
        Assert.Throws<ArgumentException>(() => plugin.SetScalar(new Radicle.Interop.FormKey("Fields.esp\0junk", 0x820), "REFR\\HealthPercent", "80"));

        Assert.Equal(before, plugin.ToBytes());
    }

    [Fact]
    public void EmbeddedNulInFilePathsIsRefused()
    {
        using var temp = new TempDirectory();
        using var plugin = Open(temp);

        Assert.Throws<ArgumentException>(() => PluginFile.Read(Path.Combine(temp.Path, Plugin) + "\0junk"));
        Assert.Throws<ArgumentException>(() => PluginFile.Create(Plugin + "\0junk"));
        Assert.Throws<ArgumentException>(() => plugin.Write(Path.Combine(temp.Path, "Written.esp") + "\0junk"));
        Assert.False(File.Exists(Path.Combine(temp.Path, "Written.esp")));
    }

    [Fact]
    public void FieldCountReportsOccurrences()
    {
        using var temp = new TempDirectory();
        using var plugin = Open(temp);

        Assert.Equal(2, plugin.FieldCount(Race, "RACE\\BipedObjectNames"));
        Assert.Equal(1, plugin.FieldCount(Expl, "EXPL\\Light"));
        Assert.Equal(0, plugin.FieldCount(Refr, "REFR\\Scale"));
        Assert.Throws<KeyNotFoundException>(() => plugin.FieldCount(K(0x999), "REFR\\Scale"));
        Assert.False(plugin.TryFieldCount(K(0x999), "REFR\\Scale", out var missing));
        Assert.Equal(0, missing);
        Assert.True(plugin.TryFieldCount(Refr, "REFR\\Scale", out var none));
        Assert.Equal(0, none);
        Assert.True(plugin.TryFieldCount(Race, "RACE\\BipedObjectNames", out var biped));
        Assert.Equal(2, biped);
        var error = Assert.Throws<RadicleException>(() => plugin.FieldCount(Refr, "REFR\\Nope"));
        Assert.Equal(Status.InvalidArgument, error.Status);
        Assert.Throws<ArgumentNullException>(() => plugin.FieldCount(Refr, null!));
    }

    [Fact]
    public void FormLinkListOperationsEditByPath()
    {
        using var temp = new TempDirectory();
        using var plugin = Open(temp);

        Assert.Equal(2, plugin.FormLinkListCount(Flst, "FLST\\Items"));
        Assert.True(plugin.FormLinkListInsert(Flst, "FLST\\Items", 1, M(0x900)));
        Assert.True(plugin.FormLinkListSet(Flst, "FLST\\Items", 2, M(0x901)));
        Assert.Equal(3, plugin.FormLinkListCount(Flst, "FLST\\Items"));
        Assert.False(plugin.FormLinkListInsert(Flst, "FLST\\Items", 9, M(0x900)));
        Assert.False(plugin.FormLinkListSet(Flst, "FLST\\Items", 9, M(0x900)));
        Assert.False(plugin.FormLinkListRemove(Flst, "FLST\\Items", 9));
        Assert.False(plugin.FormLinkListClear(K(0x999), "FLST\\Items"));
        Assert.Throws<KeyNotFoundException>(() => plugin.FormLinkListCount(K(0x999), "FLST\\Items"));
        Assert.False(plugin.TryFormLinkListCount(K(0x999), "FLST\\Items", out var missing));
        Assert.Equal(0, missing);
        Assert.True(plugin.TryFormLinkListCount(Flst, "FLST\\Items", out var items));
        Assert.Equal(3, items);
        Assert.True(plugin.FormLinkListRemove(Flst, "FLST\\Items", 0));
        Assert.Throws<ArgumentOutOfRangeException>(() => plugin.FormLinkListRemove(Flst, "FLST\\Items", -1));

        foreach (var action in new Action[]
                 {
                     () => plugin.FormLinkListInsert(Flst, "FLST\\Items", 0, null),
                     () => plugin.FormLinkListSet(Flst, "FLST\\Items", 0, null),
                     () => plugin.FormLinkListInsert(Flst, "FLST\\Items", 0, new Radicle.Interop.FormKey("Other.esm", 1)),
                     () => plugin.FormLinkListCount(Flst, "FLST\\Nope"),
                     () => plugin.FormLinkListRemove(Refr, "FLST\\Items", 0),
                     () => plugin.FormLinkListClear(Refr, "FLST\\Items"),
                 })
        {
            var error = Assert.Throws<RadicleException>(action);
            Assert.Equal(Status.InvalidArgument, error.Status);
        }

        using (var reloaded = SaveAndReload(plugin, temp, out var bytes))
        {
            Assert.Equal(2, reloaded.FormLinkListCount(Flst, "FLST\\Items"));
            Assert.True(Contains(bytes, Encoding.ASCII.GetBytes("LNAM").Concat(new byte[] { 4, 0, 0x00, 0x09, 0, 0 }).ToArray()));
            Assert.True(Contains(bytes, Encoding.ASCII.GetBytes("LNAM").Concat(new byte[] { 4, 0, 0x01, 0x09, 0, 0 }).ToArray()));
        }

        Assert.True(plugin.FormLinkListClear(Flst, "FLST\\Items"));
        Assert.True(plugin.FormLinkListClear(Flst, "FLST\\Items"));
        Assert.Equal(0, plugin.FormLinkListCount(Flst, "FLST\\Items"));
    }

    [Fact]
    public void NullableListsTakeNullElements()
    {
        using var temp = new TempDirectory();
        using var plugin = Open(temp);

        Assert.True(plugin.FormLinkListInsert(Race, "RACE\\MalePresets", 1, null));
        Assert.Equal(2, plugin.FormLinkListCount(Race, "RACE\\MalePresets"));
        Assert.True(plugin.FormLinkListSet(Race, "RACE\\MalePresets", 0, null));
    }
}
