using Radicle.Interop.Native;
using Xunit;
using static Radicle.Interop.Tests.GameplayFixture;

namespace Radicle.Interop.Tests;

public class NpcGameplayTests
{
    [Fact]
    public void DecodesStoredFactionsPerksAndAi()
    {
        using var temp = new TempDirectory();
        using var env = Load(temp, RichNpc());
        var gameplay = env.NpcGameplay(env.Winner(Key(Npc))!.Value);
        Assert.Equal(Key(Npc), gameplay.Npc);
        Assert.Equal(12u, gameplay.LevelRaw);
        Assert.Equal(Key(Race), gameplay.Race);
        Assert.Equal(Key(Style), gameplay.CombatStyle);
        Assert.Null(gameplay.TemplateBase);
        Assert.Equal(new[] { new NpcFaction(Key(FactionA), 2) }, gameplay.Factions);
        Assert.Equal(new[] { new NpcPerk(Key(Perk), 3u) }, gameplay.Perks);
        Assert.Equal(new[] { Key(Spell) }, gameplay.Spells);
        Assert.Equal(new NpcAi(1, 2, 3, 4, 5, 6), gameplay.Ai);
        Assert.Equal<(uint, uint)?>((150u, 40u), gameplay.CalculatedStats);
        Assert.Empty(gameplay.Stubs);
    }

    [Fact]
    public void ActorValueBaseReadsStoredStatsAndLeavesOthersUnproven()
    {
        using var temp = new TempDirectory();
        using var env = Load(temp, RichNpc());
        var npc = env.Winner(Key(Npc))!.Value;
        var health = env.ActorValueBase(npc, Key(Health));
        Assert.Equal(new ActorValueBaseOutcome(150, AvSource.CalculatedHealth, AvEvidence.RecordStored), health);
        var points = env.ActorValueBase(npc, Key(ActionPoints));
        Assert.Equal(new ActorValueBaseOutcome(40, AvSource.CalculatedActionPoints, AvEvidence.RecordStored), points);
        var strength = env.ActorValueBase(npc, Key(Strength));
        Assert.Equal(new ActorValueBaseOutcome(0, AvSource.None, AvEvidence.Unproven), strength);
    }
}
