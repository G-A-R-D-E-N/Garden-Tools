using System;
using Radicle.Interop.Native;
using Xunit;
using static Radicle.Interop.Tests.Bytes;
using static Radicle.Interop.Tests.PluginWriter;

namespace Radicle.Interop.Tests;

public class FactionReactionTests
{
    [Fact]
    public void EmptyFactionListsReportNoRelationWithoutDecider()
    {
        using var temp = new TempDirectory();
        var path = temp.Write("Base.esm", Build(0x1, Array.Empty<string>(),
            Top("FACT", new Rec("FACT", 0x830, new[] { ("EDID", Z("Owners")) }))));
        using var env = RadicleEnvironment.Load(new[] { path });
        var outcome = env.FactionReaction(Array.Empty<FormKey>(), Array.Empty<FormKey>());
        Assert.Equal(CombatReaction.Neutral, outcome.Reaction);
        Assert.Equal(ReactionReason.NoRelation, outcome.Reason);
        Assert.Null(outcome.DeciderActorFaction);
        Assert.Null(outcome.DeciderTargetFaction);
    }

    [Fact]
    public void SingleRelationReportsEnemyWithDeciderKeys()
    {
        using var temp = new TempDirectory();
        using var env = GameplayFixture.Load(temp, GameplayFixture.RichNpc());
        var outcome = env.FactionReaction(new[] { GameplayFixture.Key(GameplayFixture.FactionA) },
            new[] { GameplayFixture.Key(GameplayFixture.FactionB) });
        Assert.Equal(CombatReaction.Enemy, outcome.Reaction);
        Assert.Equal(ReactionReason.Relation, outcome.Reason);
        Assert.Equal(GameplayFixture.Key(GameplayFixture.FactionA), outcome.DeciderActorFaction);
        Assert.Equal(GameplayFixture.Key(GameplayFixture.FactionB), outcome.DeciderTargetFaction);
    }

    [Fact]
    public void CrossPairsKeepTheMinimumModifierDecider()
    {
        using var temp = new TempDirectory();
        using var env = GameplayFixture.Load(temp, GameplayFixture.RichNpc());
        var outcome = env.FactionReaction(new[] { GameplayFixture.Key(GameplayFixture.FactionC) },
            new[] { GameplayFixture.Key(GameplayFixture.FactionD), GameplayFixture.Key(GameplayFixture.FactionE) });
        Assert.Equal(CombatReaction.Enemy, outcome.Reaction);
        Assert.Equal(ReactionReason.Relation, outcome.Reason);
        Assert.Equal(GameplayFixture.Key(GameplayFixture.FactionC), outcome.DeciderActorFaction);
        Assert.Equal(GameplayFixture.Key(GameplayFixture.FactionE), outcome.DeciderTargetFaction);
    }
}
