using System;
using System.Linq;
using Radicle.Interop.Native;
using Xunit;
using static Radicle.Interop.Tests.Bytes;
using static Radicle.Interop.Tests.PluginWriter;

namespace Radicle.Interop.Tests;

public class InteractionRecordTests
{
    private const uint Kw = 0x800, KwB = 0x801, KwC = 0x802, KwD = 0x803, MiscA = 0x810, MiscB = 0x811, Owner = 0x850;
    private const uint Acti = 0x900, ActiBare = 0x901, Cont = 0x910, ContBad = 0x911, Door = 0x920;
    private const uint Furn = 0x930, FurnOld = 0x931, FurnBadSnam = 0x932, FurnOrphan = 0x933, Npc = 0x940, NpcEmpty = 0x941;

    private static FormKey K(uint id) => new("Base.esm", id);

    private static byte[] Pair(uint form, uint count) => U32(form, count);

    private static byte[] U16(params ushort[] values) => values.SelectMany(BitConverter.GetBytes).ToArray();

    private static byte[] MarkerRow(float x, float y, float z, float rotation, uint keyword, byte entryTypes, bool withEntryTypes) =>
        F32(x, y, z, rotation).Concat(U32(keyword)).Concat(withEntryTypes ? new[] { entryTypes, (byte)0, (byte)0, (byte)0 } : Array.Empty<byte>()).ToArray();

    private static string Fixture(TempDirectory temp) => temp.Write("Base.esm", Build(1, Array.Empty<string>(),
        Top("KYWD", new Rec("KYWD", Kw, new[] { ("EDID", Z("A")) }), new Rec("KYWD", KwB, new[] { ("EDID", Z("B")) }),
            new Rec("KYWD", KwC, new[] { ("EDID", Z("C")) }), new Rec("KYWD", KwD, new[] { ("EDID", Z("D")) })),
        Top("MISC", new Rec("MISC", MiscA, new[] { ("EDID", Z("Screw")) }), new Rec("MISC", MiscB, new[] { ("EDID", Z("Nut")) })),
        Top("ACTI",
            new Rec("ACTI", Acti, new[]
            {
                ("EDID", Z("Lever")), ("FULL", Z("Lever")), ("SNAM", U32(Kw)), ("VNAM", U32(KwB)), ("WNAM", U32(KwC)),
                ("ATTX", Z("Pull")), ("FNAM", U16(2)), ("KNAM", U32(KwD)),
            }),
            new Rec("ACTI", ActiBare, new[] { ("EDID", Z("Bare")), ("KNAM", U32(0)) })),
        Top("CONT",
            new Rec("CONT", Cont, new[]
            {
                ("EDID", Z("Footlocker")), ("FULL", Z("Footlocker")), ("COCT", U32(3)), ("CNTO", Pair(MiscA, 3)),
                ("CNTO", Pair(0, 9)), ("COED", new byte[12]), ("CNTO", Pair(MiscB, 0xFFFFFFFFu)),
                ("DATA", new byte[] { 0x02 }.Concat(F32(12.5f)).ToArray()), ("SNAM", U32(Kw)), ("QNAM", U32(KwB)),
                ("TNAM", U32(KwC)), ("ONAM", U32(KwD)),
            }),
            new Rec("CONT", ContBad, new[] { ("EDID", Z("BadCnto")), ("CNTO", U32(MiscA)) })),
        Top("DOOR", new Rec("DOOR", Door, new[]
        {
            ("EDID", Z("Door")), ("FULL", Z("Door")), ("SNAM", U32(Kw)), ("ANAM", U32(KwB)), ("BNAM", U32(KwC)),
            ("FNAM", new byte[] { 0x0A }), ("ONAM", Z("Unlock")), ("CNAM", Z("Lock")), ("TNAM", U32(MiscA)),
            ("TNAM", U32(0)), ("TNAM", U32(MiscB)),
        })),
        Top("FURN",
            new Rec("FURN", Furn, new[]
            {
                ("EDID", Z("Chair")), ("FULL", Z("Chair")), ("WNAM", U32(KwB)), ("ATTX", Z("Sit")), ("FNAM", U16(2)),
                ("COCT", U32(1)), ("CNTO", Pair(MiscA, 1)), ("MNAM", U32(0x80400003u)), ("WBDT", new byte[] { 7, 0 }),
                ("NAM1", U32(KwD)), ("ENAM", U32(0)), ("NAM0", U16(0, 3)), ("ENAM", U32(1)), ("FNPR", U16(1, 9)),
                ("FNPR", U16(4, 0x10)), ("XMRK", Z("Chair.nif")), ("OBTE", U32(1)), ("FULL", Z("Template")),
                ("SNAM", MarkerRow(1f, 2f, 3f, 1.5f, Kw, 0x05, true).Concat(MarkerRow(-1f, 0f, 0.5f, 0f, 0, 0x01, true)).ToArray()),
            }),
            new Rec("FURN", FurnOld, new[]
            {
                ("EDID", Z("OldChair")),
                ("SNAM", MarkerRow(4f, 5f, 6f, 2f, Kw, 0, false).Concat(MarkerRow(0f, 0f, 0f, 0f, 0, 0, false)).ToArray()),
            }, 0, 120),
            new Rec("FURN", FurnBadSnam, new[] { ("EDID", Z("BadSnam")), ("SNAM", new byte[30]) }),
            new Rec("FURN", FurnOrphan, new[] { ("EDID", Z("Orphan")), ("NAM0", U16(0, 1)) })),
        Top("NPC_",
            new Rec("NPC_", Npc, new[]
            {
                ("EDID", Z("Npc")), ("ACBS", new byte[20]), ("COCT", U32(3)),
                ("CNTO", Pair(MiscA, 2)), ("COED", U32(Owner, 7).Concat(F32(0.5f)).ToArray()),
                ("CNTO", Pair(MiscB, 1)), ("CNTO", Pair(MiscA, unchecked((uint)-3))),
            }),
            new Rec("NPC_", NpcEmpty, new[] { ("EDID", Z("Empty")), ("ACBS", new byte[20]) }),
            new Rec("NPC_", Owner, new[] { ("EDID", Z("Owner")), ("ACBS", new byte[20]) }))));

    private static Record Winner(RadicleEnvironment env, uint id) => env.Winner(K(id))!.Value;

    [Fact]
    public void ActivatorReadsTextFlagsKeywordSoundsAndWaterAndLeavesAbsentFieldsNull()
    {
        using var temp = new TempDirectory();
        using var env = RadicleEnvironment.Load(new[] { Fixture(temp) });
        var acti = env.Activator(Winner(env, Acti));
        Assert.Equal(K(Acti), acti.Record);
        Assert.Equal("Lever", acti.FullName.Text);
        Assert.Equal("Pull", acti.ActivateText.Text);
        Assert.Equal((ushort)2, acti.Flags);
        Assert.Equal(K(KwD), acti.InteractionKeyword);
        Assert.Equal(K(Kw), acti.LoopingSound);
        Assert.Equal(K(KwB), acti.ActivationSound);
        Assert.Equal(K(KwC), acti.WaterType);
        var bare = env.Activator(Winner(env, ActiBare));
        Assert.Null(bare.Flags);
        Assert.Null(bare.InteractionKeyword);
        Assert.Null(bare.ActivateText.Text);
    }

    [Fact]
    public void ContainerKeepsItemOrderAndSignedCountsDropsNullItemsAndReadsDataAndSounds()
    {
        using var temp = new TempDirectory();
        using var env = RadicleEnvironment.Load(new[] { Fixture(temp) });
        var cont = env.ContainerDefinition(Winner(env, Cont));
        Assert.Equal(new[] { new ItemAmount(K(MiscA), 3), new ItemAmount(K(MiscB), -1) }, cont.Items);
        Assert.Equal((byte)0x02, cont.Flags);
        Assert.Equal(12.5f, cont.Weight);
        Assert.Equal(K(Kw), cont.OpenSound);
        Assert.Equal(K(KwB), cont.CloseSound);
        Assert.Equal(K(KwC), cont.TakeAllSound);
        Assert.Equal(K(KwD), cont.FilterList);
        Assert.Equal(Status.ParseError, Assert.Throws<RadicleException>(() => env.ContainerDefinition(Winner(env, ContBad))).Status);
    }

    [Fact]
    public void DoorReadsSoundsFlagsAlternateTextAndRandomDestinationsInStoredOrder()
    {
        using var temp = new TempDirectory();
        using var env = RadicleEnvironment.Load(new[] { Fixture(temp) });
        var door = env.Door(Winner(env, Door));
        Assert.Equal(K(Kw), door.OpenSound);
        Assert.Equal(K(KwB), door.CloseSound);
        Assert.Equal(K(KwC), door.LoopSound);
        Assert.Equal((byte)0x0A, door.Flags);
        Assert.Equal("Unlock", door.OpenText.Text);
        Assert.Equal("Lock", door.CloseText.Text);
        Assert.Equal(new[] { K(MiscA), K(MiscB) }, door.RandomDestinations);
    }

    [Fact]
    public void FurnitureReadsBenchMarkersEntryPointsAndMarkerParamsAndIgnoresTheTemplateName()
    {
        using var temp = new TempDirectory();
        using var env = RadicleEnvironment.Load(new[] { Fixture(temp) });
        var furn = env.Furniture(Winner(env, Furn));
        Assert.Equal("Chair", furn.FullName.Text);
        Assert.Equal("Sit", furn.ActivateText.Text);
        Assert.Equal((ushort)2, furn.Flags);
        Assert.Equal(0x80400003u, furn.MarkerFlags);
        Assert.Equal((byte)7, furn.BenchType);
        Assert.Equal(K(KwB), furn.DrinkingWaterType);
        Assert.Equal(K(KwD), furn.AssociatedForm);
        Assert.Equal(new[] { new ItemAmount(K(MiscA), 1) }, furn.Items);
        Assert.Equal(new[] { new FurnitureMarkerSlot(0, 3), new FurnitureMarkerSlot(1, null) }, furn.Markers);
        Assert.Equal(new[] { new FurnitureEntry(1, 0x0009), new FurnitureEntry(4, 0x0010) }, furn.EntryPoints);
        Assert.Equal("Chair.nif", furn.MarkerModel);
        Assert.Equal(new[]
        {
            new FurnitureMarkerParam((1f, 2f, 3f), 1.5f, K(Kw), 0x05),
            new FurnitureMarkerParam((-1f, 0f, 0.5f), 0f, null, 0x01),
        }, furn.MarkerParams);
    }

    [Fact]
    public void FurnitureMarkerParamsBelowFormVersion125HaveNoEntryTypes()
    {
        using var temp = new TempDirectory();
        using var env = RadicleEnvironment.Load(new[] { Fixture(temp) });
        var old = env.Furniture(Winner(env, FurnOld));
        Assert.Equal(new[]
        {
            new FurnitureMarkerParam((4f, 5f, 6f), 2f, K(Kw), 0),
            new FurnitureMarkerParam((0f, 0f, 0f), 0f, null, 0),
        }, old.MarkerParams);
        Assert.Null(old.MarkerModel);
        Assert.Null(old.BenchType);
        Assert.Null(old.MarkerFlags);
        Assert.Empty(old.Markers);
        Assert.Empty(old.Items);
    }

    [Fact]
    public void MalformedFurnitureAndWrongSignaturesFailWithParseError()
    {
        using var temp = new TempDirectory();
        using var env = RadicleEnvironment.Load(new[] { Fixture(temp) });
        Assert.Equal(Status.ParseError, Assert.Throws<RadicleException>(() => env.Furniture(Winner(env, FurnBadSnam))).Status);
        Assert.Equal(Status.ParseError, Assert.Throws<RadicleException>(() => env.Furniture(Winner(env, FurnOrphan))).Status);
        Assert.Equal(Status.ParseError, Assert.Throws<RadicleException>(() => env.Door(Winner(env, Cont))).Status);
        Assert.Equal(Status.ParseError, Assert.Throws<RadicleException>(() => env.Activator(Winner(env, Door))).Status);
    }

    [Fact]
    public void NpcInventoryReturnsCntoRowsWithTheirCoedAndKeepsNegativeCounts()
    {
        using var temp = new TempDirectory();
        using var env = RadicleEnvironment.Load(new[] { Fixture(temp) });
        var rows = env.NpcInventory(Winner(env, Npc));
        Assert.Equal(3, rows.Count);
        Assert.Equal(new NpcInventoryEntry(K(MiscA), 2, true, K(Owner), 7, 0.5f), rows[0]);
        Assert.Equal(new NpcInventoryEntry(K(MiscB), 1, false, null, 0, 0f), rows[1]);
        Assert.Equal(-3, rows[2].Count);
        Assert.False(rows[2].HasExtra);
        Assert.Empty(env.NpcInventory(Winner(env, NpcEmpty)));
    }

    [Fact]
    public void NpcInventoryRejectsARecordThatIsNotAnNpc()
    {
        using var temp = new TempDirectory();
        using var env = RadicleEnvironment.Load(new[] { Fixture(temp) });
        Assert.Equal(Status.InvalidArgument, Assert.Throws<RadicleException>(() => env.NpcInventory(Winner(env, MiscA))).Status);
    }
}
