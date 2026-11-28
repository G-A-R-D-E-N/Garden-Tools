using System;
using System.Linq;
using Radicle.Interop.Native;
using Xunit;
using static Radicle.Interop.Tests.Bytes;
using static Radicle.Interop.Tests.PluginWriter;

namespace Radicle.Interop.Tests;

public class UiRecordTests
{
    private static byte[] Pair(uint form, uint count) => U32(form, count);

    private static byte[] BodyPartNode(byte partType, byte toHit, uint actorValue)
    {
        var node = new byte[101];
        BitConverter.GetBytes(1.5f).CopyTo(node, 0);
        node[64] = 0x09;
        node[65] = partType;
        node[66] = 100;
        BitConverter.GetBytes(actorValue).CopyTo(node, 67);
        node[71] = toHit;
        node[72] = 10;
        node[73] = 20;
        node[78] = 3;
        return node;
    }

    private static byte[] HasPerk(uint perk) => new byte[4].Concat(U32(0x3F800000)).Concat(BitConverter.GetBytes((ushort)448))
        .Concat(new byte[2]).Concat(U32(perk, 0, 0, 0, uint.MaxValue)).ToArray();

    private static byte[] OmodRow(byte type, byte function, ushort property, byte[] value1, byte[] value2, float step) =>
        new byte[] { type, 0, 0, 0, function, 0, 0, 0 }.Concat(BitConverter.GetBytes(property)).Concat(new byte[2])
            .Concat(value1).Concat(value2).Concat(F32(step)).ToArray();

    private static byte[] OmodData(params byte[][] rows) =>
        U32(0, (uint)rows.Length).Concat(new byte[2]).Concat(U32(0)).Concat(new byte[2]).Concat(U32(0, 0, 0))
            .Concat(rows.SelectMany(row => row)).ToArray();

    private static string Fixture(TempDirectory temp) => temp.Write("Base.esm", Build(1, Array.Empty<string>(),
        Top("KYWD", new Rec("KYWD", 0x800, new[] { ("EDID", Z("Bench")) }), new Rec("KYWD", 0x802, new[] { ("EDID", Z("CatA")) })),
        Top("ARMO", new Rec("ARMO", 0x801, new[] { ("EDID", Z("Made")) })),
        Top("MISC", new Rec("MISC", 0x810, new[] { ("EDID", Z("Screw")), ("FULL", Z("Screw")), ("DATA", U32(0xFFFFFFFEu).Concat(F32(1.5f)).ToArray()), ("CVPA", Pair(0x811, 2)) }),
            new Rec("MISC", 0x812, new[] { ("EDID", Z("Gone")) }, 0x20)),
        Top("CMPO", new Rec("CMPO", 0x811, new[] { ("EDID", Z("Steel")), ("DATA", U32(3)), ("MNAM", U32(0x810)) })),
        Top("COBJ", new Rec("COBJ", 0x820, new[]
        {
            ("EDID", Z("Recipe")), ("DESC", Z("Make it")), ("BNAM", U32(0x800)), ("CNAM", U32(0x801)),
            ("FNAM", U32(0x802, 0)), ("FVPA", Pair(0x811, 4)),
        }),
            new Rec("COBJ", 0x821, new[] { ("EDID", Z("NullComp")), ("FVPA", U32(0, 2, 0x811, 5)) }),
            new Rec("COBJ", 0x822, new[] { ("EDID", Z("BadFnam")), ("FNAM", new byte[] { 1, 2, 3, 4, 5, 6 }) }),
            new Rec("COBJ", 0x823, new[] { ("EDID", Z("Gated")), ("BNAM", U32(0x800)), ("CNAM", U32(0x801)), ("CTDA", HasPerk(0x830)) })),
        Top("PERK", new Rec("PERK", 0x830, new[] { ("EDID", Z("One")), ("FULL", Z("Rank One")), ("NNAM", U32(0x831)), ("DATA", new byte[] { 0, 2, 3, 1, 0 }) }),
            new Rec("PERK", 0x831, new[] { ("EDID", Z("Two")), ("NNAM", U32(0)) }),
            new Rec("PERK", 0x832, new[]
            {
                ("EDID", Z("Effects")), ("DATA", new byte[] { 0, 2, 3, 1, 0 }), ("PRKE", new byte[] { 2, 0, 0 }),
                ("DATA", new byte[] { 1, 2, 3 }), ("EPFT", new byte[] { 1 }), ("EPFD", U32(0x3F800000)), ("PRKF", Array.Empty<byte>()),
            }),
            new Rec("PERK", 0x833, new[] { ("EDID", Z("Short")), ("DATA", new byte[] { 0, 2 }) }),
            new Rec("PERK", 0x834, new[] { ("EDID", Z("Data3")), ("DATA", new byte[] { 1, 4, 6 }) }),
            new Rec("PERK", 0x835, new[] { ("EDID", Z("Data4")), ("DATA", new byte[] { 0, 5, 7, 1 }) }),
            new Rec("PERK", 0x836, new[]
            {
                ("EDID", Z("Barter")), ("DATA", new byte[] { 0, 2, 3, 1, 0 }),
                ("PRKE", new byte[] { 2, 1, 5 }), ("DATA", new byte[] { 0x08, 3, 1 }), ("PRKC", new byte[] { 0 }), ("CTDA", HasPerk(0x830)),
                ("EPFT", new byte[] { 1 }), ("EPFD", F32(0.5f)), ("PRKF", Array.Empty<byte>()),
                ("PRKE", new byte[] { 2, 0, 2 }), ("DATA", new byte[] { 0x3C, 2, 0 }),
                ("EPFT", new byte[] { 1 }), ("EPFD", F32(1.5f)), ("PRKF", Array.Empty<byte>()),
                ("PRKE", new byte[] { 0, 0, 0 }), ("DATA", U32(0x850).Concat(new byte[] { 10, 0 }).ToArray()), ("PRKF", Array.Empty<byte>()),
            })),
        Top("OMOD", new Rec("OMOD", 0x8E0, new[]
        {
            ("EDID", Z("Props")), ("DATA", OmodData(
                OmodRow(1, 0, 3, F32(1.5f), F32(0f), 0.25f),
                OmodRow(4, 2, 9, U32(0x802), U32(7), 0f),
                OmodRow(0, 1, 0x0102, U32(5), U32(0), 0f))),
        }), new Rec("OMOD", 0x8E1, new[] { ("EDID", Z("Empty")), ("DATA", OmodData()) }),
            new Rec("OMOD", 0x8E2, new[] { ("EDID", Z("NoData")) })),
        Top("NOTE", new Rec("NOTE", 0x840, new[] { ("EDID", Z("Tape")), ("DNAM", new byte[] { 3 }), ("SNAM", U32(0x850)) }),
            new Rec("NOTE", 0x841, new[] { ("EDID", Z("Open")), ("PNAM", new byte[] { 0x61, 0x62 }) }),
            new Rec("NOTE", 0x842, new[] { ("EDID", Z("Short")), ("DATA", U32(5)) })),
        Top("BPTD", new Rec("BPTD", 0x8C0, new[]
        {
            ("EDID", Z("Body")), ("BPTN", Z("Head")), ("BPNN", Z("Head_Node")), ("BPNT", Z("HeadVATS")), ("BPND", BodyPartNode(1, 35, 0x830)),
            ("BPNN", Z("Bare_Node")),
        }), new Rec("BPTD", 0x8C1, new[] { ("EDID", Z("Short")), ("BPTN", Z("Head")), ("BPND", new byte[100]) })),
        Top("REFR",
            new Rec("REFR", 0x860, new[] { ("NAME", U32(0x810)), ("XLOC", new byte[] { 75, 0, 0, 0 }.Concat(U32(0x810)).Concat(new byte[] { 4, 0, 0, 0, 0, 0, 0, 0 }).ToArray()) }),
            new Rec("REFR", 0x861, new[] { ("NAME", U32(0x810)) })),
        Top("TERM", new Rec("TERM", 0x850, new[]
        {
            ("EDID", Z("Term")), ("FULL", Z("Terminal")), ("BTXT", Z("Body")), ("CTDA", HasPerk(0x830)), ("ITXT", Z("Open")), ("RNAM", Z("Opening")),
            ("ANAM", new byte[] { 8 }), ("ITID", new byte[] { 9, 0 }), ("UNAM", Z("Shown")), ("CTDA", HasPerk(0x831)),
        }))));

    [Fact]
    public void ComponentsFormLinksAndLiveWinnersReadThroughTheManagedWrappers()
    {
        using var temp = new TempDirectory();
        using var env = RadicleEnvironment.Load(new[] { Fixture(temp) });
        var cobj = env.Winner(new FormKey("Base.esm", 0x820))!.Value;
        Assert.Equal(new[] { new FormKey("Base.esm", 0x802) }, env.FormLinks(cobj, "FNAM"));
        Assert.Contains(env.FormLinks(cobj), l => l.Subrecord == "BNAM" && l.Key == new FormKey("Base.esm", 0x800));
        Assert.Single(env.LiveWinnersOf("MISC"));
        Assert.Equal(2, env.WinnersOf("MISC").Count);
    }

    [Fact]
    public void TypedReadersReturnDecodedRecords()
    {
        using var temp = new TempDirectory();
        using var env = RadicleEnvironment.Load(new[] { Fixture(temp) });
        FormKey K(uint id) => new("Base.esm", id);
        var recipe = env.ConstructibleObject(env.Winner(K(0x820))!.Value);
        Assert.Equal(K(0x800), recipe.WorkbenchKeyword);
        Assert.Equal(K(0x801), recipe.CreatedObject);
        Assert.Equal("Make it", recipe.Description.Text);
        Assert.Equal(4u, recipe.Components.Single().Count);
        Assert.Empty(recipe.Conditions!);
        var gate = Assert.Single(env.ConstructibleObject(env.Winner(K(0x823))!.Value).Conditions!);
        Assert.Equal((ushort)448, gate.Row.Function);
        Assert.Equal(K(0x830), gate.Param1);
        Assert.Null(gate.Global);
        Assert.Null(gate.Reference);
        var term = env.Terminal(env.Winner(K(0x850))!.Value);
        Assert.Equal("Terminal", term.FullName.Text);
        Assert.Equal(TerminalItemType.DisplayText, term.MenuItems.Single().Type);
        Assert.Equal((ushort)9, term.MenuItems.Single().ItemId);
        Assert.Equal("Shown", term.MenuItems.Single().DisplayText.Text);
        Assert.Equal(1, term.MenuItems.Single().ConditionCount);
        var itemGate = Assert.Single(term.MenuItems.Single().Conditions!);
        Assert.Equal((ushort)448, itemGate.Row.Function);
        Assert.Equal(K(0x831), itemGate.Param1);
        var bodyGate = Assert.Single(Assert.Single(term.BodyTexts).Conditions!);
        Assert.Equal(K(0x830), bodyGate.Param1);
    }

    [Fact]
    public void MalformedConstructibleObjectFailsWithParseError()
    {
        using var temp = new TempDirectory();
        using var env = RadicleEnvironment.Load(new[] { Fixture(temp) });
        Assert.Equal(Status.ParseError, Assert.Throws<RadicleException>(() => env.ConstructibleObject(env.Winner(new FormKey("Base.esm", 0x822))!.Value)).Status);
    }

    [Fact]
    public void PerkEntryPointsReadRawBarterEntriesFloatValuesAndTabConditionsForTheWinner()
    {
        using var temp = new TempDirectory();
        using var env = RadicleEnvironment.Load(new[] { Fixture(temp) });
        FormKey K(uint id) => new("Base.esm", id);
        var perk = env.PerkEntryPoints(K(0x836))!;
        Assert.Equal(K(0x836), perk.Record);
        Assert.Equal(2, perk.Effects.Count);
        var buy = perk.Effects[0];
        Assert.Equal((byte)0x08, buy.EntryPoint);
        Assert.Equal((byte)3, buy.Function);
        Assert.Equal((byte)1, buy.Rank);
        Assert.Equal((byte)5, buy.Priority);
        Assert.Equal((byte)1, buy.ParameterType);
        Assert.Equal(0.5f, buy.Value);
        var tab = Assert.Single(buy.Tabs);
        Assert.Equal((sbyte)0, tab.RunOn);
        var gate = Assert.Single(tab.Conditions);
        Assert.Equal((ushort)448, gate.Row.Function);
        Assert.Equal(K(0x830), gate.Param1);
        var sell = perk.Effects[1];
        Assert.Equal((byte)0x3C, sell.EntryPoint);
        Assert.Equal((byte)2, sell.Function);
        Assert.Equal(1.5f, sell.Value);
        Assert.Empty(sell.Tabs);
        Assert.Empty(env.PerkEntryPoints(K(0x831))!.Effects);
        Assert.Null(env.PerkEntryPoints(K(0x8FF)));
        Assert.Equal(Status.ParseError, Assert.Throws<RadicleException>(() => env.PerkEntryPoints(K(0x820))).Status);
    }

    [Fact]
    public void OmodPropertiesReadRawRowsAndResolveFormValuesForTheWinner()
    {
        using var temp = new TempDirectory();
        using var env = RadicleEnvironment.Load(new[] { Fixture(temp) });
        FormKey K(uint id) => new("Base.esm", id);
        var omod = env.OmodProperties(K(0x8E0))!;
        Assert.Equal(K(0x8E0), omod.Record);
        Assert.Equal(3, omod.Properties.Count);
        var scalar = omod.Properties[0];
        Assert.Equal((byte)1, scalar.ValueType);
        Assert.Equal((byte)0, scalar.FunctionType);
        Assert.Equal((ushort)3, scalar.Property);
        Assert.Equal(BitConverter.SingleToUInt32Bits(1.5f), scalar.Value1);
        Assert.Equal(0u, scalar.Value2);
        Assert.Equal(0.25f, scalar.Step);
        Assert.Null(scalar.Value1Form);
        var form = omod.Properties[1];
        Assert.Equal((byte)2, form.FunctionType);
        Assert.Equal(7u, form.Value2);
        Assert.Equal(K(0x802), form.Value1Form);
        var integer = omod.Properties[2];
        Assert.Equal((ushort)0x0102, integer.Property);
        Assert.Equal(5u, integer.Value1);
        Assert.Null(integer.Value1Form);
        Assert.Empty(env.OmodProperties(K(0x8E1))!.Properties);
        Assert.Null(env.OmodProperties(K(0x8FF)));
        Assert.Equal(Status.ParseError, Assert.Throws<RadicleException>(() => env.OmodProperties(K(0x8E2))).Status);
        Assert.Equal(Status.ParseError, Assert.Throws<RadicleException>(() => env.OmodProperties(K(0x820))).Status);
    }

    [Fact]
    public void BodyPartDataReadsPartsNodeDataAndRejectsAShortNode()
    {
        using var temp = new TempDirectory();
        using var env = RadicleEnvironment.Load(new[] { Fixture(temp) });
        var body = env.BodyPartData(env.Winner(new FormKey("Base.esm", 0x8C0))!.Value);
        Assert.Equal(2, body.Parts.Count);
        var head = body.Parts[0];
        Assert.Equal("Head", head.Name.Text);
        Assert.Equal("Head_Node", head.PartNode);
        Assert.Equal("HeadVATS", head.VatsTarget);
        Assert.Equal(BodyPartType.Head1, head.Node!.PartType);
        Assert.Equal(1.5f, head.Node.DamageMult);
        Assert.Equal((byte)0x09, head.Node.Flags);
        Assert.Equal((byte)100, head.Node.HealthPercent);
        Assert.Equal(new FormKey("Base.esm", 0x830), head.Node.ActorValue);
        Assert.Equal((byte)35, head.Node.ToHitChance);
        Assert.Equal((byte)10, head.Node.ExplodableExplosionChance);
        Assert.Equal((byte)20, head.Node.NonLethalDismembermentChance);
        Assert.Equal((byte)3, head.Node.GeometrySegmentIndex);
        Assert.Equal(NameKind.Absent, body.Parts[1].Name.Kind);
        Assert.Equal("Bare_Node", body.Parts[1].PartNode);
        Assert.Null(body.Parts[1].VatsTarget);
        Assert.Null(body.Parts[1].Node);
        Assert.Equal(Status.ParseError, Assert.Throws<RadicleException>(() => env.BodyPartData(env.Winner(new FormKey("Base.esm", 0x8C1))!.Value)).Status);
    }

    [Fact]
    public void NullComponentFormIdsAreSkippedInsteadOfFailingTheWholeRecipe()
    {
        using var temp = new TempDirectory();
        using var env = RadicleEnvironment.Load(new[] { Fixture(temp) });
        var cobj = env.Winner(new FormKey("Base.esm", 0x821))!.Value;
        var expected = new[] { new ComponentAmount(new FormKey("Base.esm", 0x811), 5) };
        Assert.Equal(expected, env.ConstructibleObject(cobj).Components);
    }

    [Fact]
    public void LockReadsLevelKindKeyAndLeveledFlagAndReportsNoDataWithoutXloc()
    {
        using var temp = new TempDirectory();
        using var env = RadicleEnvironment.Load(new[] { Fixture(temp) });
        var locked = env.Lock(env.Winner(new FormKey("Base.esm", 0x860))!.Value);
        Assert.True(locked.HasLockData);
        Assert.Equal((byte)75, locked.Level);
        Assert.Equal(LockKind.Expert, locked.Kind);
        Assert.Equal(new FormKey("Base.esm", 0x810), locked.Key);
        Assert.True(locked.LeveledLock);
        var none = env.Lock(env.Winner(new FormKey("Base.esm", 0x861))!.Value);
        Assert.False(none.HasLockData);
        Assert.Equal(LockKind.None, none.Kind);
    }

    [Fact]
    public void ReadersRejectARecordOfTheWrongSignature()
    {
        using var temp = new TempDirectory();
        using var env = RadicleEnvironment.Load(new[] { Fixture(temp) });
        var misc = env.Winner(new FormKey("Base.esm", 0x810))!.Value;
        Assert.Throws<RadicleException>(() => env.Terminal(misc));
        Assert.Throws<RadicleException>(() => env.Lock(misc));
    }
}
