using System;
using System.Linq;
using Radicle.Interop.Native;
using Xunit;
using static Radicle.Interop.Tests.Bytes;
using static Radicle.Interop.Tests.PluginWriter;

namespace Radicle.Interop.Tests;

public class SpawnTests
{
    private const uint Npc = 0x862, NpcPicked = 0x872, LvlnEmpty = 0x870, LvlnOne = 0x87F, Armor = 0x910;
    private const uint ItemOne = 0xB00, ItemTwo = 0xB01;
    private const uint Placed = 0x930, PlacedParent = 0x931, PlacedLeveled = 0x932, Zone = 0x940;

    private static FormKey Key(uint local) => new("Base.esm", local);

    private static Rec NpcRecord(uint id) => new("NPC_", id, new[] { ("EDID", Z("Npc")), ("ACBS", new byte[20]) });

    private static Rec Leveled(string signature, uint id, byte flags, params (ushort Level, uint Item, ushort Count)[] entries) =>
        new(signature, id, new[] { ("EDID", Z("Leveled")), ("LVLD", new byte[] { 0 }), ("LVLM", new byte[] { 0 }),
                ("LVLF", new[] { flags }), ("LLCT", new[] { (byte)entries.Length }) }
            .Concat(entries.Select(e => ("LVLO", BitConverter.GetBytes(e.Level).Concat(new byte[2])
                .Concat(BitConverter.GetBytes(e.Item)).Concat(BitConverter.GetBytes(e.Count)).Concat(new byte[2]).ToArray())))
            .ToArray());

    private static Rec Reference(uint id, uint baseId, params (string Sig, byte[] Data)[] extra) =>
        new("REFR", id, new[] { ("NAME", U32(baseId)) }.Concat(extra).ToArray());

    private static RadicleEnvironment Load(TempDirectory temp)
    {
        var path = temp.Write("Base.esm", Build(0x1, Array.Empty<string>(),
            Top("NPC_", NpcRecord(Npc), NpcRecord(NpcPicked)),
            Top("LVLN", new Rec("LVLN", LvlnEmpty, new[] { ("EDID", Z("LeveledNpc")) }), Leveled("LVLN", LvlnOne, 0x01, (1, NpcPicked, 1))),
            Top("LVLI", Leveled("LVLI", ItemOne, 0x01, (1, Armor, 4)), Leveled("LVLI", ItemTwo, 0x01, (1, Armor, 2), (1, NpcPicked, 6),
                (5, Armor, 9))),
            Top("ARMO", new Rec("ARMO", Armor, new[] { ("EDID", Z("Armor")), ("BOD2", U32(0x8)) })),
            Top("ECZN", new Rec("ECZN", Zone, new[] { ("EDID", Z("Zone")), ("DATA", U32(0, 0).Concat(new byte[] { 0, 7, 0, 50 }).ToArray()) })),
            Top("REFR",
                Reference(Placed, Npc, ("XCNT", U32(3)), ("XLCM", U32(2)), ("XEZN", U32(Zone)),
                    ("XESP", U32(PlacedParent).Concat(new byte[] { 0x01, 0, 0, 0 }).ToArray())),
                Reference(PlacedParent, LvlnEmpty),
                Reference(PlacedLeveled, LvlnOne))));
        return RadicleEnvironment.Load(new[] { path });
    }

    private static Status StatusOf(Action action) => Assert.Throws<RadicleException>(action).Status;

    [Fact]
    public void LeveledPickPicksTheOnlyEntryWithAndWithoutAnOverride()
    {
        using var temp = new TempDirectory();
        using var env = Load(temp);
        foreach (var flags in new[] { LeveledFlagsOverride.None, LeveledFlagsOverride.UseAll, LeveledFlagsOverride.CalculateForEach })
        {
            var pick = env.LeveledPick(Key(LvlnOne), 1, 0, flags);
            Assert.Equal(LeveledPickReason.Picked, pick.Reason);
            Assert.Equal(new[] { Key(NpcPicked) }, pick.Entries);
        }
    }

    [Fact]
    public void LeveledPickReportsItsReasonInsteadOfAStatus()
    {
        using var temp = new TempDirectory();
        using var env = Load(temp);
        var empty = env.LeveledPick(Key(LvlnEmpty), 1, 0);
        Assert.Equal(LeveledPickReason.NoEntryAtLevel, empty.Reason);
        Assert.Empty(empty.Entries);
        Assert.Equal(LeveledPickReason.NotLeveledList, env.LeveledPick(Key(Armor), 1, 0).Reason);
        Assert.Equal(LeveledPickReason.ListMissing, env.LeveledPick(Key(0x999), 1, 0).Reason);
    }

    [Fact]
    public void LeveledPickRefusesBadLevelAndBadMask()
    {
        using var temp = new TempDirectory();
        using var env = Load(temp);
        Assert.Equal(Status.InvalidArgument, StatusOf(() => env.LeveledPick(Key(LvlnOne), 0, 0)));
        Assert.Equal(Status.InvalidArgument, StatusOf(() => env.LeveledPick(Key(LvlnOne), 0x10000u, 0)));
        Assert.Equal(Status.InvalidArgument, StatusOf(() => env.LeveledPick(Key(LvlnOne), 1, 0, (LeveledFlagsOverride)0x08)));
        Assert.Equal(Status.InvalidArgument, StatusOf(() => env.LeveledPick(Key(LvlnOne), 1, 0, (LeveledFlagsOverride)0x01)));
    }

    [Fact]
    public void LeveledPickCountMultipliesWhenTheFlagIsClearAndRollsWhenForced()
    {
        using var temp = new TempDirectory();
        using var env = Load(temp);
        var multiplied = env.LeveledPickCount(Key(LvlnOne), 1, 0, 3);
        Assert.Equal(LeveledPickReason.Picked, multiplied.Reason);
        Assert.Equal((1u, 3u), (multiplied.Rolls, multiplied.Multiplier));
        Assert.Equal(new[] { Key(NpcPicked) }, multiplied.Entries);

        var rolled = env.LeveledPickCount(Key(LvlnOne), 1, 0, 3, LeveledFlagsOverride.CalculateForEach);
        Assert.Equal((3u, 1u), (rolled.Rolls, rolled.Multiplier));
        Assert.Equal(new[] { Key(NpcPicked), Key(NpcPicked), Key(NpcPicked) }, rolled.Entries);

        var none = env.LeveledPickCount(Key(LvlnEmpty), 1, 0, 4, LeveledFlagsOverride.CalculateForEach);
        Assert.Equal(LeveledPickReason.NoEntryAtLevel, none.Reason);
        Assert.Equal(4u, none.Rolls);
        Assert.Empty(none.Entries);
    }

    [Fact]
    public void LeveledPickCountReportsTheRowCountOfEveryEntry()
    {
        using var temp = new TempDirectory();
        using var env = Load(temp);
        var single = env.LeveledPickCount(Key(ItemOne), 1, 0, 1);
        Assert.Equal(new[] { Key(Armor) }, single.Entries);
        Assert.Equal(new ushort[] { 4 }, single.EntryCounts);

        var all = env.LeveledPickCount(Key(ItemTwo), 1, 0, 1, LeveledFlagsOverride.UseAll);
        Assert.Equal(new[] { Key(Armor), Key(NpcPicked) }, all.Entries);
        Assert.Equal(new ushort[] { 2, 6 }, all.EntryCounts);

        var rolled = env.LeveledPickCount(Key(ItemOne), 1, 0, 3, LeveledFlagsOverride.CalculateForEach);
        Assert.Equal(new ushort[] { 4, 4, 4 }, rolled.EntryCounts);

        Assert.Empty(env.LeveledPickCount(Key(LvlnEmpty), 1, 0, 4, LeveledFlagsOverride.CalculateForEach).EntryCounts);
    }

    [Fact]
    public void LeveledPickCountRefusesBadCountLevelAndMask()
    {
        using var temp = new TempDirectory();
        using var env = Load(temp);
        Assert.Equal(Status.InvalidArgument, StatusOf(() => env.LeveledPickCount(Key(LvlnOne), 1, 0, 0)));
        Assert.Equal(Status.InvalidArgument, StatusOf(() => env.LeveledPickCount(Key(LvlnOne), 1, 0, 0x8000u)));
        Assert.Equal(Status.InvalidArgument, StatusOf(() => env.LeveledPickCount(Key(LvlnOne), 0, 0, 1)));
        Assert.Equal(Status.InvalidArgument, StatusOf(() => env.LeveledPickCount(Key(LvlnOne), 1, 0, 1, (LeveledFlagsOverride)0x01)));
    }

    [Fact]
    public void EnableStateFollowsTheParentAndHonoursOverrides()
    {
        using var temp = new TempDirectory();
        using var env = Load(temp);
        var placed = env.Winner(Key(Placed))!.Value;
        var verdict = env.RefEnableState(placed);
        Assert.False(verdict.Enabled);
        Assert.Equal(EnableStop.Root, verdict.Stop);
        Assert.Equal(1u, verdict.ChainLength);
        Assert.Equal(Key(PlacedParent), verdict.Deciding);

        var overridden = env.RefEnableState(placed, new[] { new RefEnableOverride(Key(PlacedParent), false) });
        Assert.True(overridden.Enabled);
        Assert.Equal(EnableStop.Overridden, overridden.Stop);

        var duplicate = new[] { new RefEnableOverride(Key(PlacedParent), false), new RefEnableOverride(Key(PlacedParent), true) };
        Assert.Equal(Status.InvalidArgument, StatusOf(() => env.RefEnableState(placed, duplicate)));
    }

    [Fact]
    public void EnableStateRefusesANonPlacedRecordAndAForeignRecord()
    {
        using var temp = new TempDirectory();
        using var env = Load(temp);
        Assert.Equal(Status.InvalidArgument, StatusOf(() => env.RefEnableState(env.Winner(Key(Armor))!.Value)));
        using var otherTemp = new TempDirectory();
        using var other = Load(otherTemp);
        Assert.Throws<ArgumentException>(() => other.RefEnableState(env.Winner(Key(Placed))!.Value));
    }

    [Fact]
    public void PlacedActorSpawnDecodesCountModifierZoneAndEnableState()
    {
        using var temp = new TempDirectory();
        using var env = Load(temp);
        var spawn = env.PlacedActorSpawn(env.Winner(Key(Placed))!.Value, 1, 0);
        Assert.Equal(SpawnBaseKind.Npc, spawn.BaseKind);
        Assert.Equal(Key(Npc), spawn.Base);
        Assert.Equal(3, spawn.Count);
        Assert.Equal(2, spawn.LevelModifier);
        Assert.False(spawn.Enable.Enabled);
        Assert.Equal(Key(Zone), spawn.EncounterZone);
        Assert.Equal(Key(Npc), spawn.ResolvedBase);
        Assert.Equal(7, spawn.ZoneMinLevel);
        Assert.False(spawn.LevelRuleVerified);
        Assert.Equal(0u, spawn.ResolvedLevel);
    }

    [Fact]
    public void PlacedActorSpawnResolvesALeveledBaseAtThePlayerLevel()
    {
        using var temp = new TempDirectory();
        using var env = Load(temp);
        var spawn = env.PlacedActorSpawn(env.Winner(Key(PlacedLeveled))!.Value, 1, 0);
        Assert.Equal(SpawnBaseKind.LeveledNpc, spawn.BaseKind);
        Assert.Equal(Key(NpcPicked), spawn.ResolvedBase);
        Assert.Null(spawn.EncounterZone);
        Assert.Null(spawn.ZoneMinLevel);
        Assert.Null(spawn.Count);
        Assert.Equal(Status.InvalidArgument, StatusOf(() => env.PlacedActorSpawn(env.Winner(Key(PlacedLeveled))!.Value, 0, 0)));
    }

    [Fact]
    public void ScriptSpawnPlanPicksTheNpcAndEchoesTheModifier()
    {
        using var temp = new TempDirectory();
        using var env = Load(temp);
        var plan = env.ScriptSpawnPlan(Key(Npc), 1, 0, 2);
        Assert.Equal(SpawnBaseKind.Npc, plan.BaseKind);
        Assert.Equal(LeveledPickReason.Picked, plan.Reason);
        Assert.Equal(new[] { Key(Npc) }, plan.Entries);
        Assert.Equal(2, plan.LevelModifier);
        Assert.Null(env.ScriptSpawnPlan(Key(Npc), 1, 0).LevelModifier);
        Assert.Equal(Status.InvalidArgument, StatusOf(() => env.ScriptSpawnPlan(Key(Npc), 0, 0)));
    }
}
