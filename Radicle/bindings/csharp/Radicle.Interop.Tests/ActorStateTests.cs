using System;
using System.Linq;
using Radicle.Interop.Native;
using Xunit;

namespace Radicle.Interop.Tests;

public class ActorStateTests
{
    private static FormKey Key(string plugin, uint local) => new(plugin, local);

    private static ActorStateRecord Populated(byte[]? tail = null) => new(
        new ActorReference(ActorRefKind.Plugin, Key("Fallout4.esm", 0xABCD), 0, null, 0),
        ActorStateFlags.Enabled | ActorStateFlags.Dead, Key("Fallout4.esm", 0x12345),
        (1.5f, -2.25f, 3.0f), (0f, 90f, 0f), 1.25f,
        new[] { new ActorInventoryEntry(Key("Fallout4.esm", 0x0F), 3), new ActorInventoryEntry(Key("DLC01.esm", 0x10), -1) },
        new[] { new ActorValueModifierEntry("Health", 25f), new ActorValueModifierEntry("Armor", -4.5f) },
        new[] { new ActorFactionEntry(Key("Fallout4.esm", 0x1C1F1), 2) },
        Key("Fallout4.esm", 0x2B1E1), null,
        new ActorReference(ActorRefKind.Created, null, 77, Key("Fallout4.esm", 0xBEEF), 0x1122334455667788ul),
        500, 42, tail ?? Array.Empty<byte>());

    private static ActorStateRecord Empty() => new(
        new ActorReference(ActorRefKind.Plugin, null, 0, null, 0), ActorStateFlags.None, null,
        (0f, 0f, 0f), (0f, 0f, 0f), 0f, Array.Empty<ActorInventoryEntry>(), Array.Empty<ActorValueModifierEntry>(),
        Array.Empty<ActorFactionEntry>(), null, null, null, 0, 0, Array.Empty<byte>());

    private static void AssertSame(ActorStateRecord expected, ActorStateRecord actual)
    {
        Assert.Equal(expected.Ref, actual.Ref);
        Assert.Equal(expected.Flags, actual.Flags);
        Assert.Equal(expected.Cell, actual.Cell);
        Assert.Equal(expected.Position, actual.Position);
        Assert.Equal(expected.Rotation, actual.Rotation);
        Assert.Equal(expected.Scale, actual.Scale);
        Assert.Equal(expected.Inventory, actual.Inventory);
        Assert.Equal(expected.ActorValues, actual.ActorValues);
        Assert.Equal(expected.Factions, actual.Factions);
        Assert.Equal(expected.ForcedPackage, actual.ForcedPackage);
        Assert.Equal(expected.CurrentPackage, actual.CurrentPackage);
        Assert.Equal(expected.CombatTarget, actual.CombatTarget);
        Assert.Equal(expected.Bounty, actual.Bounty);
        Assert.Equal(expected.SpawnLevel, actual.SpawnLevel);
        Assert.Equal(expected.UnknownTail, actual.UnknownTail);
    }

    private static Status StatusOf(Action action) => Assert.Throws<RadicleException>(action).Status;

    [Fact]
    public void PopulatedStateSurvivesEncodeDecodeAndReencodeByteForByte()
    {
        var bytes = ActorStateCodec.Encode(Populated());
        Assert.Equal("RASV", System.Text.Encoding.ASCII.GetString(bytes, 0, 4));
        var decoded = ActorStateCodec.Decode(bytes);
        AssertSame(Populated(), decoded);
        Assert.Equal(bytes, ActorStateCodec.Encode(decoded));
    }

    [Fact]
    public void EmptyStateRoundTripsWithEmptyListsAndNoneKeys()
    {
        var bytes = ActorStateCodec.Encode(Empty());
        var decoded = ActorStateCodec.Decode(bytes);
        AssertSame(Empty(), decoded);
        Assert.Null(decoded.Ref.Form);
        Assert.Null(decoded.CombatTarget);
        Assert.Equal(bytes, ActorStateCodec.Encode(decoded));
    }

    [Fact]
    public void UnknownTrailingBytesAndUnnamedFlagBitsSurvive()
    {
        var tail = new byte[] { 0xDE, 0xAD, 0xBE };
        var bytes = ActorStateCodec.Encode(Populated()).Concat(tail).ToArray();
        BitConverter.GetBytes((uint)(bytes.Length - 12)).CopyTo(bytes, 8);
        var decoded = ActorStateCodec.Decode(bytes);
        Assert.Equal(tail, decoded.UnknownTail);
        Assert.Equal(bytes, ActorStateCodec.Encode(decoded));
        Assert.Equal(bytes, ActorStateCodec.Encode(Populated(tail)));

        var flags = (ActorStateFlags)0x80000005u;
        Assert.Equal(flags, ActorStateCodec.Decode(ActorStateCodec.Encode(Populated() with { Flags = flags })).Flags);
    }

    [Fact]
    public void TextCarriesItsByteLengthSoNonAsciiAndEmbeddedNulSurvive()
    {
        var cell = Key("Plügin\0B.esp", 9);
        var decoded = ActorStateCodec.Decode(ActorStateCodec.Encode(Populated() with
        {
            Cell = cell,
            ActorValues = new[] { new ActorValueModifierEntry("Ägility", 1f) },
        }));
        Assert.Equal(cell, decoded.Cell);
        Assert.Equal("Ägility", decoded.ActorValues.Single().ActorValue);
    }

    [Fact]
    public void CreatedRefsOfOneBaseKeepDistinctIdentities()
    {
        var first = Empty() with { Ref = new ActorReference(ActorRefKind.Created, null, 1001, Key("Fallout4.esm", 0xBEEF), 7) };
        var second = first with { Ref = first.Ref with { CreatedId = 1002, SpawnSeed = 8 } };
        var a = ActorStateCodec.Decode(ActorStateCodec.Encode(first));
        var b = ActorStateCodec.Decode(ActorStateCodec.Encode(second));
        Assert.Equal(ActorRefKind.Created, a.Ref.Kind);
        Assert.Equal((1001u, 7ul), (a.Ref.CreatedId, a.Ref.SpawnSeed));
        Assert.Equal((1002u, 8ul), (b.Ref.CreatedId, b.Ref.SpawnSeed));
        Assert.Equal(a.Ref.Base, b.Ref.Base);
    }

    [Fact]
    public void EncodeRefusesBadInputWithInvalidArgument()
    {
        var longName = new string('p', ActorStateCodec.MaxStringBytes + 1);
        Assert.Equal(Status.InvalidArgument, StatusOf(() => ActorStateCodec.Encode(Populated() with { Cell = Key(longName, 0) })));
        Assert.Equal(Status.InvalidArgument, StatusOf(() => ActorStateCodec.Encode(Populated() with { CurrentPackage = Key("", 5) })));
        Assert.Equal(Status.InvalidArgument, StatusOf(() => ActorStateCodec.Encode(Populated() with
        {
            Ref = new ActorReference((ActorRefKind)2, null, 0, null, 0),
        })));
        var factions = Enumerable.Repeat(new ActorFactionEntry(Key("Fallout4.esm", 1), 0), ActorStateCodec.MaxListEntries + 1).ToArray();
        Assert.Equal(Status.InvalidArgument, StatusOf(() => ActorStateCodec.Encode(Populated() with { Factions = factions })));
    }

    [Fact]
    public void DecodeRefusesBadBlobsWithParseError()
    {
        var good = ActorStateCodec.Encode(Populated());
        var badMagic = (byte[])good.Clone();
        badMagic[0] = (byte)'X';
        Assert.Equal(Status.ParseError, StatusOf(() => ActorStateCodec.Decode(badMagic)));
        var badVersion = (byte[])good.Clone();
        BitConverter.GetBytes(2u).CopyTo(badVersion, 4);
        Assert.Equal(Status.ParseError, StatusOf(() => ActorStateCodec.Decode(badVersion)));
        Assert.Equal(Status.ParseError, StatusOf(() => ActorStateCodec.Decode(good.AsSpan(0, good.Length - 1).ToArray())));
        Assert.Equal(Status.ParseError, StatusOf(() => ActorStateCodec.Decode(ReadOnlySpan<byte>.Empty)));
    }
}
