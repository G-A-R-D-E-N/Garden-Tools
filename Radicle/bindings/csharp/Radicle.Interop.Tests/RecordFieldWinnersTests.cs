using System;
using Xunit;
using static Radicle.Interop.Tests.Bytes;
using static Radicle.Interop.Tests.PluginWriter;

namespace Radicle.Interop.Tests;

public sealed class RecordFieldWinnersTests
{
    private static byte[] FormsPlugin() =>
        Build(0, Array.Empty<string>(),
            Top("MISC", new Rec("MISC", 0x800, new[]
            {
                ("EDID", Z("MiscItem")),
                ("CVPA", U32(0x800, 2, 0x804, 1)),
            })),
            Top("ARMO", new Rec("ARMO", 0x804, new[] { ("EDID", Z("Plain")) })),
            Top("GMST", new Rec("GMST", 0x812, new[] { ("EDID", Z("sInline")), ("DATA", Z("PlainGmst")) })),
            Top("ALCH", new Rec("ALCH", 0x810, new[]
            {
                ("EDID", Z("Potion")),
                ("ENIT", U32(25, 2, 0, 0, 0)),
                ("DATA", F32(2.5f)),
                ("FULL", Z("PlainChem")),
            })),
            Top("MESG", new Rec("MESG", 0x811, new[] { ("EDID", Z("Message")), ("DNAM", U32(3)) })));

    private static byte[] LocalizedPlugin() =>
        Build(0x80, Array.Empty<string>(),
            Top("GMST", new Rec("GMST", 0x800, new[] { ("EDID", Z("sLabel")), ("DATA", U32(0x1234)) })),
            Top("ALCH", new Rec("ALCH", 0x820, new[]
            {
                ("EDID", Z("LocPotion")),
                ("FULL", U32(0x5678)),
            })));

    [Fact]
    public void BulkWinnerReadKeepsLocalizedRowsNullWithoutFailingScalarsInMixedLoadOrder()
    {
        using var temp = new TempDirectory();
        var plain = temp.Write("Forms.esp", FormsPlugin());
        var localized = temp.Write("Localized.esp", LocalizedPlugin());
        using var env = RadicleEnvironment.Load(new[] { plain, localized });

        var gmstRows = RecordFieldWinners.Read(env, "GMST\\StringValue");
        Assert.Equal(2, gmstRows.Count);
        Assert.Equal("PlainGmst", Assert.Single(gmstRows, r => r.Key.Plugin == "Forms.esp").Text);
        Assert.Null(Assert.Single(gmstRows, r => r.Key.Plugin == "Localized.esp").Text);

        var scalarRows = RecordFieldWinners.Read(env, "ALCH\\Value");
        Assert.Equal(2, scalarRows.Count);
        Assert.Equal("25", Assert.Single(scalarRows, r => r.Key.Plugin == "Forms.esp").Text);
        Assert.Null(Assert.Single(scalarRows, r => r.Key.Plugin == "Localized.esp").Text);

        var fields = RecordFieldWinners.ReadFields(env, "ALCH", new[] { "ALCH\\Value", "ALCH\\Weight" });
        Assert.Equal(2, fields.Count);
        var formsFields = Assert.Single(fields, r => r.Key.Plugin == "Forms.esp");
        Assert.Equal("25", formsFields.Texts[0]);
        Assert.Equal("2.5", formsFields.Texts[1]);
        var localizedFields = Assert.Single(fields, r => r.Key.Plugin == "Localized.esp");
        Assert.Null(localizedFields.Texts[0]);
        Assert.Null(localizedFields.Texts[1]);

        var localizedGmstRows = RecordFieldWinners.ReadFields(env, "GMST", new[] { "GMST\\StringValue" });
        Assert.Equal(2, localizedGmstRows.Count);
        Assert.Equal("PlainGmst", Assert.Single(localizedGmstRows, r => r.Key.Plugin == "Forms.esp").Texts[0]);
        Assert.Null(Assert.Single(localizedGmstRows, r => r.Key.Plugin == "Localized.esp").Texts[0]);
    }

    [Fact]
    public void BulkWinnerReadResolvesLocalizedRowWhenStringTablesAreLoaded()
    {
        using var temp = new TempDirectory();
        var plain = temp.Write("Forms.esp", FormsPlugin());
        var localized = temp.Write("Localized.esp", LocalizedPlugin());
        temp.Write("Localized_en.STRINGS", StringsWriter.Table((0x1234, "Label")));
        using var env = RadicleEnvironment.Load(new[] { plain, localized });
        using var strings = StringTables.Load(env, temp.Path);

        var rows = RecordFieldWinners.Read(env, "GMST\\StringValue", strings: strings);
        Assert.Equal(2, rows.Count);
        Assert.Equal("PlainGmst", Assert.Single(rows, r => r.Key.Plugin == "Forms.esp").Text);
        Assert.Equal("Label", Assert.Single(rows, r => r.Key.Plugin == "Localized.esp").Text);
    }
}