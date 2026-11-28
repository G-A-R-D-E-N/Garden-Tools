using System;
using System.IO;
using System.Linq;
using Radicle.Interop.Native;
using static Radicle.Interop.Tests.Bytes;
using static Radicle.Interop.Tests.PluginWriter;

namespace Radicle.Interop.Tests;

internal static class GameplayFixture
{
    public const uint FactionA = 0x900, FactionB = 0x901, FactionC = 0x902, FactionD = 0x903, FactionE = 0x904;
    public const uint Health = 0x930, Strength = 0x931, ActionPoints = 0x932;
    public const uint Race = 0x940, Class = 0x941, Style = 0x942, Spell = 0x943, Perk = 0x944, Npc = 0x910;
    public const uint NoStyleNpc = 0x911, CrimeFaction = 0x920, Zone = 0x960;

    public static FormKey Key(uint local) => new("Base.esm", local);

    private static byte[] Words(params uint[] values) => U32(values);

    private static byte[] Acbs(uint flags, ushort level, ushort templateFlags) =>
        U32(flags).Concat(new byte[2]).Concat(BitConverter.GetBytes(level)).Concat(new byte[6])
            .Concat(BitConverter.GetBytes(templateFlags)).Concat(new byte[4]).ToArray();

    private static byte[] Membership(uint id, byte rank) => U32(id).Append(rank).ToArray();

    private static byte[] CalculatedStats(ushort health, ushort actionPoints) =>
        BitConverter.GetBytes(health).Concat(BitConverter.GetBytes(actionPoints)).Concat(new byte[4]).ToArray();

    private static Rec Simple(string signature, uint id, string edid) =>
        new(signature, id, new[] { ("EDID", Z(edid)) });

    private static Rec Fact(uint id, string edid, params (uint Target, int Modifier, uint Reaction)[] relations) =>
        new("FACT", id, new[] { ("EDID", Z(edid)) }
            .Concat(relations.Select(r => ("XNAM", Words(r.Target, unchecked((uint)r.Modifier), r.Reaction)))).ToArray());

    public static Rec RichNpc(uint flags = 0, ushort level = 12, ushort templateFlags = 0) =>
        new("NPC_", Npc, new (string, byte[])[]
        {
            ("EDID", Z("Npc")), ("ACBS", Acbs(flags, level, templateFlags)),
            ("SNAM", Membership(FactionA, 2)), ("DNAM", CalculatedStats(150, 40)),
            ("RNAM", Words(Race)), ("CNAM", Words(Class)), ("ZNAM", Words(Style)),
            ("SPLO", Words(Spell)), ("PRKZ", Words(1)), ("PRKR", Membership(Perk, 3)),
            ("AIDT", new byte[] { 1, 2, 3, 4, 5, 6 }.Concat(new byte[18]).ToArray()),
        });

    public static Rec NpcWithoutStyle() =>
        RichNpc() with
        {
            FormId = NoStyleNpc,
            Subs = RichNpc().Subs.Where(sub => sub.Sig is not ("ZNAM" or "EDID")).Prepend(("EDID", Z("NoStyle"))).ToArray(),
        };

    private static byte[] CrimeValues() =>
        new byte[] { 1, 1 }.Concat(new byte[] { 10, 0, 20, 0, 30, 0, 40, 0, 50, 0 }).Concat(F32(0.5f))
            .Concat(new byte[] { 60, 0, 70, 0 }).ToArray();

    private static byte[] ZoneData(byte minLevel, byte flags, byte maxLevel) =>
        U32(0, 0).Concat(new[] { (byte)0, minLevel, flags, maxLevel }).ToArray();

    public static RadicleEnvironment Load(TempDirectory temp, params Rec[] npcs)
    {
        var path = temp.Write("Base.esm", Build(0x1, Array.Empty<string>(), Top("FACT",
            Fact(FactionA, "FactionA", (FactionB, 0, 1)),
            Fact(FactionB, "FactionB", (FactionA, 0, 2)),
            Fact(FactionC, "FactionC", (FactionD, 5, 3), (FactionE, -50, 1)),
            Fact(FactionD, "FactionD"), Fact(FactionE, "FactionE"),
            new Rec("FACT", CrimeFaction, new[] { ("EDID", Z("Crime")), ("CRVA", CrimeValues()) })),
            Top("ECZN", new Rec("ECZN", Zone, new[] { ("EDID", Z("ZonePlain")), ("DATA", ZoneData(10, 0, 0)) })),
            Top("AVIF", Simple("AVIF", Health, "Health"), Simple("AVIF", Strength, "Strength"),
                Simple("AVIF", ActionPoints, "ActionPoints")),
            Top("RACE", Simple("RACE", Race, "Race")), Top("CLAS", Simple("CLAS", Class, "Class")),
            Top("CSTY", Simple("CSTY", Style, "Style")), Top("SPEL", Simple("SPEL", Spell, "Spell")),
            Top("PERK", Simple("PERK", Perk, "Perk")), Top("NPC_", npcs)));
        return RadicleEnvironment.Load(new[] { path });
    }
}
