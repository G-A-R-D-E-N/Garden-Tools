using Radicle.Interop.Native;
using Xunit;
using static Radicle.Interop.Tests.GameplayFixture;

namespace Radicle.Interop.Tests;

public class GameplayRulesTests
{
    [Fact]
    public void SpawnLevelReportsTheStoredLevelAndTheZoneLevel()
    {
        using var temp = new TempDirectory();
        using var env = Load(temp, RichNpc());
        var npc = env.Winner(Key(Npc))!.Value;
        Assert.Equal(new ActorLevelResult(12, 12, LevelReason.Fixed, 0), env.ActorLevelForSpawn(npc, 50, 0));
        var zoned = env.ActorLevelForSpawn(npc, 50, 0, Key(Zone));
        Assert.Equal(12u, zoned.Level);
        Assert.Equal(34u, zoned.ZoneLevel);
        Assert.Equal(LevelReason.Fixed, zoned.Reason);
    }

    [Fact]
    public void SpawnLevelRefusesAKeyThatIsNotAZone()
    {
        using var temp = new TempDirectory();
        using var env = Load(temp, RichNpc());
        var npc = env.Winner(Key(Npc))!.Value;
        Assert.Equal(Status.InvalidArgument,
            Assert.Throws<RadicleException>(() => env.ActorLevelForSpawn(npc, 50, 0, Key(FactionD))).Status);
    }

    [Fact]
    public void CombatProfileReportsTheStoredStyleOrTheUnprovenFallback()
    {
        using var temp = new TempDirectory();
        using var env = Load(temp, RichNpc(), NpcWithoutStyle());
        var own = env.CombatProfile(env.Winner(Key(Npc))!.Value);
        Assert.Equal(new CombatProfileResult(Key(Npc), Key(Style), CombatStyleSource.OwnRecord), own);
        var absent = env.CombatProfile(env.Winner(Key(NoStyleNpc))!.Value);
        Assert.Null(absent.Style);
        Assert.Equal(CombatStyleSource.AbsentFallbackUnproven, absent.Source);
    }

    [Fact]
    public void CrimeRulesDecodeTheStoredValues()
    {
        using var temp = new TempDirectory();
        using var env = Load(temp, RichNpc());
        var rules = env.CrimeRules(env.Winner(Key(CrimeFaction))!.Value);
        Assert.Equal(Key(CrimeFaction), rules.Faction);
        Assert.Equal(new CrimeValuesResult(1, 1, 10, 20, 30, 40, 50), rules.Values);
        Assert.Equal(0.5f, rules.StealMultiplier);
        Assert.Equal((ushort)60, rules.Escape);
        Assert.Equal((ushort)70, rules.Werewolf);
        Assert.Null(rules.SharedCrimeList);
    }

    [Fact]
    public void OwnershipFollowsTheRequiredFactionRank()
    {
        using var temp = new TempDirectory();
        using var env = Load(temp, RichNpc());
        var members = new[] { new NpcFaction(Key(FactionA), 3) };
        Assert.True(env.OwnershipAllowed(Key(FactionA), Key(Npc), members, 2));
        Assert.False(env.OwnershipAllowed(Key(FactionA), Key(Npc), members, 4));
        Assert.True(env.OwnershipAllowed(Key(Npc), Key(Npc), System.Array.Empty<NpcFaction>(), 99));
    }
}
