using System;
using System.Linq;
using Xunit;
using static Radicle.Interop.Tests.Bytes;
using static Radicle.Interop.Tests.PluginWriter;

namespace Radicle.Interop.Tests;

// source:Radicle/schema/fallout4/fields.json MESG\Flags, MESG\DisplayTime, MESG\SWF, MESG\OwnerQuest; DNAM bit 0 Message Box, bit 1 Delay Initial Display, TNAM u32.
public class MessageTests
{
    private static byte[] Dl(uint id, string text) =>
        U32(1, (uint)(4 + Z(text).Length), id, 0, (uint)Z(text).Length).Concat(Z(text)).ToArray();

    private static Rec Mesg(uint formId, params (string, byte[])[] subs) => new("MESG", formId, subs);

    [Fact]
    public void InlineMessageReadsTextButtonsFlagsSwfAndQuest()
    {
        using var temp = new TempDirectory();
        var path = temp.Write("Mesg.esm", Build(1, Array.Empty<string>(), Top("MESG", Mesg(0x800,
            ("FULL", Z("Title")), ("DESC", Z("Body")), ("ITXT", Z("Yes")), ("ITXT", Z("No")), ("DNAM", U32(3)),
            ("TNAM", U32(7)), ("SNAM", Z("msg.swf")), ("QNAM", U32(0x810))))));
        MessageRecord message;
        using (var env = RadicleEnvironment.Load(new[] { path }))
            message = env.Message(env.Winner(new FormKey("Mesg.esm", 0x800))!.Value);
        Assert.Equal("Title", message.Title);
        Assert.Equal("Body", message.Body);
        Assert.Equal(new[] { "Yes", "No" }, message.Buttons.ToArray());
        Assert.True(message.IsMessageBox);
        Assert.True(message.DelayInitialDisplay);
        Assert.Equal(7u, message.DisplayTime);
        Assert.Equal("msg.swf", message.Swf);
        Assert.Equal(new FormKey("Mesg.esm", 0x810), message.OwnerQuest);
    }

    [Fact]
    public void FlagBitsAreIndependentAndAbsentFieldsAreNull()
    {
        using var temp = new TempDirectory();
        var path = temp.Write("Mesg.esm", Build(1, Array.Empty<string>(), Top("MESG",
            Mesg(0x800, ("DNAM", U32(1))), Mesg(0x801, ("DNAM", U32(2))), Mesg(0x802, ("DNAM", U32(0))), Mesg(0x803))));
        using var env = RadicleEnvironment.Load(new[] { path });
        MessageRecord Read(uint id) => env.Message(env.Winner(new FormKey("Mesg.esm", id))!.Value);

        Assert.True(Read(0x800).IsMessageBox);
        Assert.False(Read(0x800).DelayInitialDisplay);
        Assert.False(Read(0x801).IsMessageBox);
        Assert.True(Read(0x801).DelayInitialDisplay);
        Assert.False(Read(0x802).IsMessageBox);
        Assert.False(Read(0x802).DelayInitialDisplay);
        var bare = Read(0x803);
        Assert.False(bare.IsMessageBox);
        Assert.False(bare.DelayInitialDisplay);
        Assert.Null(bare.DisplayTime);
        Assert.Null(bare.Title);
        Assert.Null(bare.Body);
        Assert.Null(bare.Swf);
        Assert.Null(bare.OwnerQuest);
        Assert.Empty(bare.Buttons);
    }

    [Fact]
    public void LocalizedMessageResolvesThroughStringsAndDlstringsAndMissingIdsAreNull()
    {
        using var temp = new TempDirectory();
        var path = temp.Write("Mesg.esm", Build(0x81, Array.Empty<string>(), Top("MESG",
            Mesg(0x800, ("FULL", U32(1)), ("DESC", U32(2)), ("ITXT", U32(3)), ("ITXT", U32(4)), ("DNAM", U32(1))),
            Mesg(0x801, ("FULL", U32(99)), ("DESC", U32(1)), ("ITXT", Z("inline"))))));
        temp.Write("Mesg_en.STRINGS", StringsWriter.Table((1, "Title"), (3, "Yes")));
        temp.Write("Mesg_en.DLSTRINGS", Dl(2, "Body"));
        using var env = RadicleEnvironment.Load(new[] { path });
        using var tables = StringTables.Load(env, temp.Path);

        var message = env.Message(env.Winner(new FormKey("Mesg.esm", 0x800))!.Value, tables);
        Assert.Equal("Title", message.Title);
        Assert.Equal("Body", message.Body);
        Assert.Equal(new[] { "Yes", string.Empty }, message.Buttons.ToArray());
        Assert.True(message.IsMessageBox);

        var missing = env.Message(env.Winner(new FormKey("Mesg.esm", 0x801))!.Value, tables);
        Assert.Null(missing.Title);
        Assert.Null(missing.Body);
        Assert.Equal(new[] { string.Empty }, missing.Buttons.ToArray());

        var untabled = env.Message(env.Winner(new FormKey("Mesg.esm", 0x800))!.Value);
        Assert.Null(untabled.Title);
        Assert.Null(untabled.Body);
        Assert.Equal(new[] { string.Empty, string.Empty }, untabled.Buttons.ToArray());
    }

    // Detects: an inline FULL decoded as UTF-8 instead of Windows-1252, so the lone 0xE9 would become U+FFFD instead of U+00E9.
    [Fact]
    public void InlineTextDecodesWindows1252()
    {
        using var temp = new TempDirectory();
        var path = temp.Write("Mesg.esm", Build(1, Array.Empty<string>(), Top("MESG",
            Mesg(0x800, ("FULL", new byte[] { 0x43, 0x61, 0x66, 0xE9, 0x00 })))));
        using var env = RadicleEnvironment.Load(new[] { path });
        Assert.Equal("Caf\u00e9", env.Message(env.Winner(new FormKey("Mesg.esm", 0x800))!.Value).Title);
    }

    // Detects: QNAM index 0 in a one-master plugin attributed to the owning plugin instead of its master.
    [Fact]
    public void OwnerQuestResolvesThroughTheOwningPluginsMasterTable()
    {
        using var temp = new TempDirectory();
        var master = temp.Write("Master.esm", Build(1, Array.Empty<string>(), Top("BOOK",
            new Rec("BOOK", 0x800, new[] { ("EDID", Z("Book")) }))));
        var patch = temp.Write("Patch.esp", Build(0, new[] { "Master.esm" }, Top("MESG",
            Mesg(0x01000801, ("QNAM", U32(0x00000810))))));
        using var env = RadicleEnvironment.Load(new[] { master, patch });
        var message = env.Message(env.Winner(new FormKey("Patch.esp", 0x801))!.Value);
        Assert.Equal(new FormKey("Master.esm", 0x810), message.OwnerQuest);
    }

    // Detects: an out-of-range QNAM master index throwing out of Message and discarding Title, Body and Buttons.
    [Fact]
    public void InvalidOwnerQuestMasterIndexLeavesTheRestOfTheMessageReadable()
    {
        using var temp = new TempDirectory();
        var path = temp.Write("Mesg.esm", Build(1, Array.Empty<string>(), Top("MESG", Mesg(0x800,
            ("FULL", Z("Title")), ("DESC", Z("Body")), ("ITXT", Z("Yes")), ("QNAM", U32(0x05000810))))));
        using var env = RadicleEnvironment.Load(new[] { path });
        var message = env.Message(env.Winner(new FormKey("Mesg.esm", 0x800))!.Value);
        Assert.Null(message.OwnerQuest);
        Assert.Equal("Title", message.Title);
        Assert.Equal("Body", message.Body);
        Assert.Equal(new[] { "Yes" }, message.Buttons.ToArray());
    }

    // Detects: Message accepting StringTables loaded for a different environment and looking up another plugin's ids.
    [Fact]
    public void StringsFromAnotherEnvironmentAreRefused()
    {
        using var temp = new TempDirectory();
        var first = temp.Write("First.esm", Build(0x81, Array.Empty<string>(), Top("MESG", Mesg(0x800, ("FULL", U32(1))))));
        var second = temp.Write("Second.esm", Build(0x81, Array.Empty<string>(), Top("MESG", Mesg(0x800, ("FULL", U32(1))))));
        temp.Write("First_en.STRINGS", StringsWriter.Table((1, "One")));
        temp.Write("Second_en.STRINGS", StringsWriter.Table((1, "Two")));
        using var owner = RadicleEnvironment.Load(new[] { first });
        using var other = RadicleEnvironment.Load(new[] { second });
        using var tables = StringTables.Load(owner, temp.Path);
        var record = other.Winner(new FormKey("Second.esm", 0x800))!.Value;
        Assert.Throws<ArgumentException>(() => other.Message(record, tables));
        Assert.Equal("One", owner.Message(owner.Winner(new FormKey("First.esm", 0x800))!.Value, tables).Title);
    }

    [Fact]
    public void NonMessageRecordIsRefused()
    {
        using var temp = new TempDirectory();
        var path = temp.Write("Mesg.esm", Build(1, Array.Empty<string>(),
            Top("BOOK", new Rec("BOOK", 0x800, new[] { ("EDID", Z("Book")) }))));
        using var env = RadicleEnvironment.Load(new[] { path });
        var record = env.Winner(new FormKey("Mesg.esm", 0x800))!.Value;
        Assert.Throws<ArgumentException>(() => env.Message(record));
        Assert.Throws<ArgumentException>(() => env.Message(default));
    }
}
