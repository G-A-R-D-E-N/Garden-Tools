using System;
using System.Collections.Generic;
using Radicle.Interop.Native;
using Xunit;
using static Radicle.Interop.Tests.Bytes;
using static Radicle.Interop.Tests.PluginWriter;

namespace Radicle.Interop.Tests;

public class NpcVisualTests
{
    private const uint Human = 0x800;
    private const uint Bot = 0x801;

    private static Rec Race()
    {
        var data = new byte[200];
        BitConverter.GetBytes(0x2u).CopyTo(data, 32);
        return new Rec("RACE", Human, new (string, byte[])[]
        {
            ("EDID", Z("HumanRaceT")), ("DATA", data), ("WNAM", U32(0x810)),
            ("MNAM", Array.Empty<byte>()), ("ANAM", Z(@"Actors\Character\CharacterAssets\skeleton.nif")),
            ("FNAM", Array.Empty<byte>()), ("ANAM", Z(@"Actors\Character\CharacterAssets\skeleton.nif")),
        });
    }

    private static Rec BotRace()
    {
        var data = new byte[200];
        return new Rec("RACE", Bot, new (string, byte[])[]
        {
            ("EDID", Z("BotRace")), ("DATA", data),
            ("MNAM", Array.Empty<byte>()), ("ANAM", Z(@"Actors\Robot\CharacterAssets\skeleton.nif")),
            ("FNAM", Array.Empty<byte>()), ("ANAM", Z(@"Actors\CreateABot\CharacterAssets\Skeleton.NIF")),
        });
    }

    private static byte[] OmodData(params uint[] includes)
    {
        var bytes = new byte[28 + includes.Length * 7];
        BitConverter.GetBytes((uint)includes.Length).CopyTo(bytes, 0);
        for (var i = 0; i < includes.Length; i++)
            BitConverter.GetBytes(includes[i]).CopyTo(bytes, 28 + i * 7);
        return bytes;
    }

    private static byte[] ObtsData(params uint[] includes)
    {
        var bytes = new byte[18 + includes.Length * 7];
        BitConverter.GetBytes((uint)includes.Length).CopyTo(bytes, 0);
        for (var i = 0; i < includes.Length; i++)
            BitConverter.GetBytes(includes[i]).CopyTo(bytes, 18 + i * 7);
        return bytes;
    }

    private static Rec Armo(uint id, uint slots, params uint[] addons)
    {
        var subs = new List<(string, byte[])> { ("EDID", Z("Armor")), ("BOD2", U32(slots)) };
        foreach (var addon in addons)
        {
            subs.Add(("INDX", new byte[] { 0, 0 }));
            subs.Add(("MODL", U32(addon)));
        }
        return new Rec("ARMO", id, subs.ToArray());
    }

    private static byte[] Lvlo(ushort level, uint item)
    {
        var bytes = new byte[12];
        BitConverter.GetBytes(level).CopyTo(bytes, 0);
        BitConverter.GetBytes(item).CopyTo(bytes, 4);
        BitConverter.GetBytes((ushort)1).CopyTo(bytes, 8);
        return bytes;
    }

    private static Rec Arma(uint id, uint slots, string male, string female) =>
        new("ARMA", id, new (string, byte[])[]
        {
            ("EDID", Z("Addon")), ("BOD2", U32(slots)), ("RNAM", U32(Human)), ("MOD2", Z(male)), ("MOD3", Z(female)),
        });

    private static Rec Npc(uint id, uint flags, ushort templateFlags, uint template, params (string, byte[])[] extra)
    {
        var acbs = new byte[20];
        BitConverter.GetBytes(flags).CopyTo(acbs, 0);
        BitConverter.GetBytes(templateFlags).CopyTo(acbs, 14);
        var subs = new List<(string, byte[])> { ("EDID", Z("Npc")), ("ACBS", acbs), ("RNAM", U32(Human)) };
        if (template != 0) subs.Add(("TPLT", U32(template)));
        subs.AddRange(extra);
        return new Rec("NPC_", id, subs.ToArray());
    }

    private static byte[] Plugin() => Build(1, Array.Empty<string>(),
        Top("RACE", Race(), BotRace()),
        Top("ARMO", Armo(0x810, 0x8, 0x820, 0x821, 0x822), Armo(0x830, 0x8, 0x831)),
        Top("ARMA",
            Arma(0x820, 0x8, @"Actors\Character\CharacterAssets\MaleBody.nif", @"Actors\Character\CharacterAssets\FemaleBody.nif"),
            Arma(0x821, 0x30, @"Actors\Character\CharacterAssets\MaleHands.nif", @"Actors\Character\CharacterAssets\FemaleHands.nif"),
            Arma(0x822, 0x4, @"Actors\Character\CharacterAssets\MaleHead.nif", @"Actors\Character\CharacterAssets\FemaleHead.nif"),
            Arma(0x831, 0x8, @"Armor\Raider\RaiderMale.nif", @"Armor\Raider\RaiderFemale.nif")),
        Top("OTFT", new Rec("OTFT", 0x840, new (string, byte[])[] { ("EDID", Z("Outfit")), ("INAM", U32(0x830)) })),
        Top("LVLN", new Rec("LVLN", 0x860, new (string, byte[])[] { ("EDID", Z("Leveled")) })),
        Top("LVLN", new Rec("LVLN", 0x861, new (string, byte[])[]
        {
            ("EDID", Z("LeveledTier")), ("LVLD", new byte[] { 0 }), ("LVLM", new byte[] { 0 }),
            ("LVLF", new byte[] { 0 }), ("LLCT", new byte[] { 2 }),
            ("LVLO", Lvlo(1, 0x854)), ("LVLO", Lvlo(20, 0x855)),
        })),
        Top("LVLN", new Rec("LVLN", 0x862, new (string, byte[])[]
        {
            ("EDID", Z("LeveledAllTiers")), ("LVLD", new byte[] { 0 }), ("LVLM", new byte[] { 0 }),
            ("LVLF", new byte[] { 1 }), ("LLCT", new byte[] { 2 }),
            ("LVLO", Lvlo(1, 0x854)), ("LVLO", Lvlo(20, 0x855)),
        })),
        Top("NPC_",
            Npc(0x850, 1, 0, 0, ("DOFT", U32(0x840)), ("QNAM", F32(0.1f, 0.2f, 0.3f, 1f))),
            Npc(0x851, 0, 0x1, 0x860),
            Npc(0x852, 0, 0x1, 0x861),
            Npc(0x854, 0, 0, 0, ("QNAM", F32(0.1f, 0f, 0f, 1f))),
            Npc(0x855, 0, 0, 0, ("QNAM", F32(0.9f, 0f, 0f, 1f))),
            Npc(0x856, 0, 0x1, 0x862),
            new Rec("NPC_", 0x8A1, new (string, byte[])[]
            {
                ("EDID", Z("Robot")), ("ACBS", new byte[20]), ("RNAM", U32(Bot)), ("OBTS", ObtsData(0x8A0)),
            })),
        Top("OMOD", new Rec("OMOD", 0x8A0, new (string, byte[])[]
        {
            ("EDID", Z("Omod")), ("DATA", OmodData()), ("MODL", Z(@"Actors\Robot\Parts\TorsoHandy.nif")),
        })));

    [Fact]
    public void HumanFemaleHasFaceGenUncoveredSkinAndOutfitParts()
    {
        using var temp = new TempDirectory();
        var path = temp.Write("Base.esm", Plugin());
        using var env = RadicleEnvironment.Load(new[] { path });
        var visual = env.NpcVisual(env.Winner(new FormKey("Base.esm", 0x850))!.Value, null, 0);
        Assert.Equal(new FormKey("Base.esm", 0x850), visual.Npc);
        Assert.Equal(new FormKey("Base.esm", Human), visual.Race);
        Assert.Equal("HumanRaceT", visual.RaceEditorId);
        Assert.True(visual.Female);
        Assert.Equal(new[] { 0.1f, 0.2f, 0.3f, 1f }, visual.SkinTint!);
        Assert.Equal("meshes/actors/character/characterassets/skeleton.hkx", visual.SkeletonHkxEntry);
        Assert.Equal("meshes/actors/character/animations/h2h/idle.hkx", visual.IdleClipEntry);
        Assert.Equal(new[]
        {
            new NpcVisualPart("meshes/actors/character/facegendata/facegeom/base.esm/00000850.nif", 0, NpcVisualSource.FaceGen),
            new NpcVisualPart("meshes/actors/character/characterassets/femalehands.nif", 0x30, NpcVisualSource.Skin),
            new NpcVisualPart("meshes/armor/raider/raiderfemale.nif", 0x8, NpcVisualSource.Outfit),
        }, visual.Parts);
        Assert.Empty(visual.Stubs);
    }

    [Fact]
    public void ArmorAddonsPickTheSexMeshAndRefuseNonArmorRecords()
    {
        using var temp = new TempDirectory();
        var path = temp.Write("Base.esm", Plugin());
        using var env = RadicleEnvironment.Load(new[] { path });
        var armor = env.Winner(new FormKey("Base.esm", 0x830))!.Value;
        var female = env.ArmorAddons(armor, new FormKey("Base.esm", Human), true, false);
        Assert.Equal(new[] { new NpcVisualPart("meshes/armor/raider/raiderfemale.nif", 0x8, NpcVisualSource.Outfit) }, female.Parts);
        Assert.Equal(0x8u, female.MatchedSlots);
        Assert.Empty(female.Stubs);
        var bot = env.ArmorAddons(armor, new FormKey("Base.esm", Bot), false, false);
        Assert.Empty(bot.Parts);
        Assert.Equal(0u, bot.MatchedSlots);
        var npc = env.Winner(new FormKey("Base.esm", 0x850))!.Value;
        Assert.Equal(Status.InvalidArgument,
            Assert.Throws<RadicleException>(() => env.ArmorAddons(npc, new FormKey("Base.esm", Human), false, false)).Status);
    }

    [Fact]
    public void LeveledTemplateIsReportedAsAStubAndNonNpcRecordsAreRefused()
    {
        using var temp = new TempDirectory();
        var path = temp.Write("Base.esm", Plugin());
        using var env = RadicleEnvironment.Load(new[] { path });
        var visual = env.NpcVisual(env.Winner(new FormKey("Base.esm", 0x851))!.Value, null, 0);
        Assert.False(visual.Female);
        Assert.Null(visual.SkinTint);
        Assert.Equal(new[] { "leveled list 000860:Base.esm has no entry at level 1" }, visual.Stubs);
        var race = env.Winner(new FormKey("Base.esm", Human))!.Value;
        Assert.Equal(Status.InvalidArgument, Assert.Throws<RadicleException>(() => env.NpcVisual(race, null, 0)).Status);
    }

    [Fact]
    public void OptionsCarryTheSpawnLevelIntoALevelledTemplate()
    {
        using var temp = new TempDirectory();
        var path = temp.Write("Base.esm", Plugin());
        using var env = RadicleEnvironment.Load(new[] { path });
        var npc = env.Winner(new FormKey("Base.esm", 0x852))!.Value;
        var low = env.NpcVisual(npc, (ushort)3, 0x5EEDUL);
        Assert.Equal(new[] { 0.1f, 0f, 0f, 1f }, low.SkinTint!);
        Assert.Empty(low.Stubs);
        var high = env.NpcVisual(npc, (ushort)25, 0x5EEDUL);
        Assert.Equal(new[] { 0.9f, 0f, 0f, 1f }, high.SkinTint!);
        Assert.Empty(high.Stubs);
    }

    [Fact]
    public void SeedSelectsAmongTheTiersOfALeveledTemplate()
    {
        using var temp = new TempDirectory();
        var path = temp.Write("Base.esm", Plugin());
        using var env = RadicleEnvironment.Load(new[] { path });
        var npc = env.Winner(new FormKey("Base.esm", 0x856))!.Value;
        var tints = new HashSet<float>();
        for (ulong seed = 0; seed < 64; seed++)
            tints.Add(env.NpcVisual(npc, (ushort)25, seed).SkinTint![0]);
        Assert.Equal(2, tints.Count);
    }

    [Fact]
    public void ObjectTemplatePartsArriveAsSourceThree()
    {
        using var temp = new TempDirectory();
        var path = temp.Write("Base.esm", Plugin());
        using var env = RadicleEnvironment.Load(new[] { path });
        var npc = env.Winner(new FormKey("Base.esm", 0x8A1))!.Value;
        var visual = env.NpcVisual(npc, null, 0);
        var part = Assert.Single(visual.Parts);
        Assert.Equal("meshes/actors/robot/parts/torsohandy.nif", part.MeshEntry);
        Assert.Equal(NpcVisualSource.ObjectTemplate, part.Source);
        Assert.DoesNotContain(visual.Stubs, stub => stub.Contains("did not decode"));
    }
}
