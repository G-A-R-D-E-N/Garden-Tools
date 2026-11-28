using System;
using System.Collections.Generic;
using System.Linq;
using Radicle.Interop.Native;
using Xunit;
using static Radicle.Interop.Tests.Bytes;
using static Radicle.Interop.Tests.PluginWriter;

namespace Radicle.Interop.Tests;

public class NpcTemplateSourceTests
{
    private const ushort KeywordsBit = 0x1000;
    private const uint Compressed = 0x00040000;
    private const uint Child = 0xA00, Parent = 0xA01, Grandparent = 0xA02, Plain = 0xA04, Dangling = 0xA05, ToRace = 0xA06,
        ToLeveled = 0xA07, Loop = 0xA08, ToCorrupt = 0xA09, NoLink = 0xA0A, Race = 0xA10, Leveled = 0xA11, Corrupt = 0xA12,
        Missing = 0xAFF, TargetBase = 0xB00, HostBase = 0xB20, PkA = 0xC10, PkB = 0xC11, PkC = 0xC12, Flst = 0xC20,
        PkOwn = 0xC30, PkChild = 0xC31, PkAiStuck = 0xC33, VA = 0xC50, VB = 0xC51, VParent = 0xC60, VChild = 0xC62,
        VFlaggedOwn = 0xC63, VToLeveled = 0xC66, GlobConst = 0xC70, GlobPlain = 0xC71, GlobNoValue = 0xC72;
    private const ushort AiPackagesBit = 0x20, DefPackListBit = 0x400, TraitsBit = 0x1;
    private const uint ConstantFlag = 0x40;

    private static FormKey Key(uint local) => new("Base.esm", local);

    private static byte[] Acbs(ushort templateFlags) =>
        new byte[14].Concat(BitConverter.GetBytes(templateFlags)).Concat(new byte[4]).ToArray();

    private static Rec Npc(uint id, ushort templateFlags, params (string, byte[])[] extra) =>
        new("NPC_", id, new[] { ("EDID", Z("Npc")), ("ACBS", Acbs(templateFlags)) }.Concat(extra).ToArray());

    private static (string, byte[]) Tplt(uint target) => ("TPLT", U32(target));

    private static (string, byte[]) TptaAt(int slot, uint target)
    {
        var links = new uint[13];
        links[slot] = target;
        return ("TPTA", U32(links));
    }

    private static RadicleEnvironment Load(TempDirectory temp)
    {
        var npcs = new List<Rec>
        {
            Npc(Grandparent, 0),
            Npc(Parent, KeywordsBit, Tplt(Grandparent)),
            Npc(Plain, 0, Tplt(Parent)),
            Npc(Child, KeywordsBit, Tplt(Parent)),
            Npc(Dangling, KeywordsBit, Tplt(Missing)),
            Npc(ToRace, KeywordsBit, Tplt(Race)),
            Npc(ToLeveled, KeywordsBit, Tplt(Leveled)),
            Npc(Loop, KeywordsBit, Tplt(Loop)),
            Npc(ToCorrupt, KeywordsBit, Tplt(Corrupt)),
            Npc(NoLink, KeywordsBit),
            new("NPC_", Corrupt, new[] { ("EDID", Z("Corrupt")) }, Compressed),
            Npc(PkOwn, 0, ("PKID", U32(PkA)), ("PKID", U32(PkA)), ("DPLT", U32(Flst))),
            Npc(PkChild, (ushort)(AiPackagesBit | DefPackListBit), Tplt(PkOwn)),
            Npc(PkAiStuck, AiPackagesBit, Tplt(Leveled)),
            Npc(VParent, 0, ("VTCK", U32(VB))),
            Npc(VChild, 0, Tplt(VParent)),
            Npc(VFlaggedOwn, TraitsBit, ("VTCK", U32(VA)), Tplt(VParent)),
            Npc(VToLeveled, 0, Tplt(Leveled)),
        };
        for (uint slot = 0; slot < 13; ++slot)
        {
            npcs.Add(Npc(TargetBase + slot, 0));
            npcs.Add(Npc(HostBase + slot, (ushort)(1u << (int)slot), TptaAt((int)slot, TargetBase + slot)));
        }
        return RadicleEnvironment.Load(new[] { temp.Write("Base.esm", Build(0x1, Array.Empty<string>(),
            Top("RACE", new Rec("RACE", Race, new[] { ("EDID", Z("Race")) })),
            Top("LVLN", new Rec("LVLN", Leveled, new[] { ("EDID", Z("Leveled")) })),
            Top("FLST", new Rec("FLST", Flst, new[] { ("EDID", Z("Defaults")), ("LNAM", U32(PkB)), ("LNAM", U32(0)), ("LNAM", U32(PkC)) })),
            Top("GLOB",
                new Rec("GLOB", GlobConst, new[] { ("EDID", Z("Const")), ("FNAM", new byte[] { (byte)'f' }), ("FLTV", BitConverter.GetBytes(0.1f)) }, ConstantFlag),
                new Rec("GLOB", GlobPlain, new[] { ("EDID", Z("Plain")), ("FNAM", new byte[] { (byte)'f' }), ("FLTV", BitConverter.GetBytes(-2.25f)) }),
                new Rec("GLOB", GlobNoValue, new[] { ("EDID", Z("NoValue")), ("FNAM", new byte[] { (byte)'f' }) }, ConstantFlag)),
            Top("NPC_", npcs.ToArray()))) });
    }

    private static NpcTemplateSourceResult Resolve(RadicleEnvironment env, uint local, NpcTemplateSlot slot) =>
        env.NpcTemplateSource(env.Winner(Key(local))!.Value, slot);

    [Fact]
    public void ClearFlagMeansTheNpcOwnsTheSlotEvenWithATemplateLink()
    {
        using var temp = new TempDirectory();
        using var env = Load(temp);
        Assert.Equal(new NpcTemplateSourceResult(Key(Plain), NpcTemplateReason.Own, 0, null),
            Resolve(env, Plain, NpcTemplateSlot.Keywords));
    }

    [Fact]
    public void SetFlagFollowsTpltToTheFirstTemplateThatOwnsTheSlot()
    {
        using var temp = new TempDirectory();
        using var env = Load(temp);
        Assert.Equal(new NpcTemplateSourceResult(Key(Grandparent), NpcTemplateReason.TemplateNpc, 2, null),
            Resolve(env, Child, NpcTemplateSlot.Keywords));
        Assert.Equal(new NpcTemplateSourceResult(Key(Child), NpcTemplateReason.Own, 0, null),
            Resolve(env, Child, NpcTemplateSlot.Traits));
    }

    [Fact]
    public void BrokenChainsKeepTheLastNpcAndNameTheBlocker()
    {
        using var temp = new TempDirectory();
        using var env = Load(temp);
        Assert.Equal(new NpcTemplateSourceResult(Key(NoLink), NpcTemplateReason.NoLink, 0, null),
            Resolve(env, NoLink, NpcTemplateSlot.Keywords));
        Assert.Equal(new NpcTemplateSourceResult(Key(Dangling), NpcTemplateReason.NotFound, 0, Key(Missing)),
            Resolve(env, Dangling, NpcTemplateSlot.Keywords));
        var race = Resolve(env, ToRace, NpcTemplateSlot.Keywords);
        Assert.Equal((NpcTemplateReason.NotNpc, (FormKey?)Key(Race)), (race.Reason, race.Blocker));
        var leveled = Resolve(env, ToLeveled, NpcTemplateSlot.Keywords);
        Assert.Equal((NpcTemplateReason.LeveledList, Key(ToLeveled), (FormKey?)Key(Leveled)),
            (leveled.Reason, leveled.Source, leveled.Blocker));
        var corrupt = Resolve(env, ToCorrupt, NpcTemplateSlot.Keywords);
        Assert.Equal((NpcTemplateReason.Undecodable, (FormKey?)Key(Corrupt)), (corrupt.Reason, corrupt.Blocker));
    }

    [Fact]
    public void ACycleStopsAfterEightHops()
    {
        using var temp = new TempDirectory();
        using var env = Load(temp);
        Assert.Equal(new NpcTemplateSourceResult(Key(Loop), NpcTemplateReason.TooDeep, 8, Key(Loop)),
            Resolve(env, Loop, NpcTemplateSlot.Keywords));
    }

    [Fact]
    public void EveryOfTheThirteenSlotsPairsItsFlagBitWithItsTptaLink()
    {
        using var temp = new TempDirectory();
        using var env = Load(temp);
        for (uint host = 0; host < 13; ++host)
            for (uint slot = 0; slot < 13; ++slot)
            {
                var result = Resolve(env, HostBase + host, (NpcTemplateSlot)slot);
                Assert.Equal(slot == host
                    ? new NpcTemplateSourceResult(Key(TargetBase + slot), NpcTemplateReason.TemplateNpc, 1, null)
                    : new NpcTemplateSourceResult(Key(HostBase + host), NpcTemplateReason.Own, 0, null), result);
            }
    }

    [Fact]
    public void ASlotPastTheTableAndANonNpcAreRefused()
    {
        using var temp = new TempDirectory();
        using var env = Load(temp);
        Assert.Equal(Status.InvalidArgument,
            Assert.Throws<RadicleException>(() => Resolve(env, Child, (NpcTemplateSlot)13)).Status);
        Assert.Equal(Status.InvalidArgument,
            Assert.Throws<RadicleException>(() => Resolve(env, Race, NpcTemplateSlot.Traits)).Status);
    }

    [Fact]
    public void PackageListIsPkidThenFlstMembersWithNullLinksDroppedAndUndecidedWhenAChainStops()
    {
        using var temp = new TempDirectory();
        using var env = Load(temp);
        var expected = new[] { Key(PkA), Key(PkA), Key(PkB), Key(PkC) };
        var own = env.NpcPackageList(env.Winner(Key(PkOwn))!.Value);
        Assert.Equal(NpcTemplateReason.Own, own.Origin.Reason);
        Assert.Equal(expected, own.Keys);
        var child = env.NpcPackageList(env.Winner(Key(PkChild))!.Value);
        Assert.Equal((NpcTemplateReason.TemplateNpc, Key(PkOwn)), (child.Origin.Reason, child.Origin.Source));
        Assert.Equal(expected, child.Keys);
        var stuck = env.NpcPackageList(env.Winner(Key(PkAiStuck))!.Value);
        Assert.Null(stuck.Keys);
        Assert.Equal((NpcTemplateReason.LeveledList, (FormKey?)Key(Leveled)), (stuck.Origin.Reason, stuck.Origin.Blocker));
        Assert.Equal(Status.InvalidArgument,
            Assert.Throws<RadicleException>(() => env.NpcPackageList(env.Winner(Key(Race))!.Value)).Status);
    }

    [Fact]
    public void VoiceTypeFollowsTpltWithoutTemplateFlagsAndOwnVtckWins()
    {
        using var temp = new TempDirectory();
        using var env = Load(temp);
        var child = env.NpcVoiceType(env.Winner(Key(VChild))!.Value);
        Assert.Equal((Key(VB), NpcTemplateReason.TemplateNpc, Key(VParent)), (child.Voice, child.Origin.Reason, child.Origin.Source));
        var flagged = env.NpcVoiceType(env.Winner(Key(VFlaggedOwn))!.Value);
        Assert.Equal((Key(VA), NpcTemplateReason.Own), (flagged.Voice, flagged.Origin.Reason));
        var leveled = env.NpcVoiceType(env.Winner(Key(VToLeveled))!.Value);
        Assert.Null(leveled.Voice);
        Assert.Equal((NpcTemplateReason.LeveledList, (FormKey?)Key(Leveled)), (leveled.Origin.Reason, leveled.Origin.Blocker));
    }

    [Fact]
    public void GlobalValueRequiresTheConstantFlagOnlyWhenAskedAndWidensTheStoredFloat()
    {
        using var temp = new TempDirectory();
        using var env = Load(temp);
        Assert.Equal((double)0.1f, env.GlobalValue(env.Winner(Key(GlobConst))!.Value, true).Value);
        var plain = env.Winner(Key(GlobPlain))!.Value;
        Assert.Equal(GlobReason.NotConstant, env.GlobalValue(plain, true).Reason);
        Assert.Null(env.GlobalValue(plain, true).Value);
        Assert.Equal(-2.25, env.GlobalValue(plain, false).Value);
        Assert.Equal(GlobReason.NoValue, env.GlobalValue(env.Winner(Key(GlobNoValue))!.Value, false).Reason);
        Assert.Equal(GlobReason.NotGlob, env.GlobalValue(env.Winner(Key(Race))!.Value, false).Reason);
    }
}
