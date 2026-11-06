using Xunit;

namespace Radicle.Interop.Tests;

// Detects: managed marshalling that must keep inputs, reports and gate callbacks identical to the native quest lifecycle.
public class PapyrusQuestLifecycleTests
{
    private static PexFile Script(bool withoutParent = true) =>
        PexFile.FromBytes(PexModelTests.SyntheticPex(withoutParent: withoutParent));

    private static QuestStartupInput Quest(string quest, int fragments = 0) =>
        new(quest, true, new[] { "SyntheticObject" }, fragments);

    [Fact]
    public void AQuestStartsItsScriptsAndStopsCleanly()
    {
        using var pex = Script();
        using var lifecycle = new PapyrusQuestLifecycle(new[] { pex });

        var started = lifecycle.Start(new[] { Quest("Q1") });

        Assert.True(started.Applied);
        Assert.Equal(1, started.Started);
        Assert.Equal(1, started.AttachedScripts);
        Assert.True(lifecycle.IsStarted("Q1"));
        Assert.Single(lifecycle.InstancesFor("Q1"));
        Assert.Equal(1, lifecycle.Counters().Started);

        Assert.True(lifecycle.Stop("Q1"));
        Assert.False(lifecycle.IsStarted("Q1"));
        Assert.Empty(lifecycle.InstancesFor("Q1"));
        Assert.True(lifecycle.Start(new[] { Quest("Q1") }).Applied);
    }

    [Fact]
    public void AnUnprovenGateBlocksTheWholeBatchBeforeAnyAttach()
    {
        using var pex = Script();
        using var lifecycle = new PapyrusQuestLifecycle(new[] { pex }, key => key != "quest.runtime.fragment_dispatch_timing");

        var plan = lifecycle.Preflight(new[] { Quest("QA"), Quest("QB", fragments: 2) });
        var result = lifecycle.Start(new[] { Quest("QA"), Quest("QB", fragments: 2) });

        Assert.Equal(1, plan.Ready);
        Assert.Equal(1, plan.Blocked);
        Assert.False(result.Applied);
        Assert.Contains(result.Reasons, reason => reason.StartsWith("QB: the dispatch timing"));
        Assert.Empty(lifecycle.InstancesFor("QA"));
        Assert.False(lifecycle.IsStarted("QA"));
    }

    [Fact]
    public void AScriptWhoseParentIsNotLoadedIsRefusedByName()
    {
        using var pex = Script(withoutParent: false);
        using var lifecycle = new PapyrusQuestLifecycle(new[] { pex });

        var result = lifecycle.Start(new[] { Quest("Q1") });

        Assert.False(result.Applied);
        Assert.Equal("Q1: missing or cyclic parent chain in a quest script", Assert.Single(result.Reasons));
        Assert.Empty(lifecycle.InstancesFor("Q1"));
    }

    [Fact]
    public void AliasScriptsAttachWithObjectivesAndClearOnRequest()
    {
        using var pex = Script();
        using var lifecycle = new PapyrusQuestLifecycle(new[] { pex });
        var aliases = new[]
        {
            new AliasScriptInput("Q1", 2, "SyntheticObject", Objectives: new[] { 3, 1 }),
            new AliasScriptInput("Q1", 3, null),
        };

        var report = lifecycle.AttachAliases(aliases);

        Assert.Equal(1, report.Attached);
        Assert.Equal(1, report.Skipped);
        Assert.Equal(0, report.Refused);
        Assert.Equal(1, report.Natives);
        Assert.Equal(2, report.Objectives);
        Assert.Equal(1, lifecycle.ObjectiveOf("Q1", 2, 3));
        Assert.Equal(-1, lifecycle.ObjectiveOf("Q1", 2, 2));
        Assert.NotNull(lifecycle.AliasInstance("Q1", 2));
        Assert.Null(lifecycle.AliasInstance("Q1", 3));

        Assert.True(lifecycle.ClearAlias("Q1", 2));
        Assert.Null(lifecycle.AliasInstance("Q1", 2));
        Assert.Equal(-1, lifecycle.ObjectiveOf("Q1", 2, 3));
    }

    [Fact]
    public void ActorAliasesStayUnboundUntilTheTargetIsProven()
    {
        using var pex = Script();
        using var lifecycle = new PapyrusQuestLifecycle(new[] { pex });
        var inputs = new[]
        {
            new ActorAliasInput(new AliasScriptInput("Q1", 1, "SyntheticObject"), true, "Actor:0A"),
            new ActorAliasInput(new AliasScriptInput("Q1", 2, "SyntheticObject"), false, null),
        };

        var report = lifecycle.BindActors(inputs);

        Assert.Equal(1, report.Bound);
        Assert.Equal(1, report.Unbound);
        Assert.Equal(1, report.Attached);
        Assert.NotNull(lifecycle.AliasInstance("Q1", 1));
        Assert.Null(lifecycle.AliasInstance("Q1", 2));
    }

    [Fact]
    public void FanOutReportsEachOwnerAndScriptWithItsOwnOutcome()
    {
        using var pex = Script();
        using var lifecycle = new PapyrusQuestLifecycle(new[] { pex });
        lifecycle.Start(new[] { Quest("QB"), Quest("QA") });

        var broadcast = lifecycle.Broadcast("Actor.OnDeath", 7, "text", true, null);

        Assert.Equal("::remote_Actor_OnDeath", broadcast.Handler);
        Assert.Equal(2, broadcast.NoHandler);
        Assert.Equal(0, broadcast.Delivered);
        Assert.Equal(2, broadcast.Outcomes.Count);
        Assert.Contains("QA", broadcast.Outcomes[0].OwnerKey);
        Assert.Contains("QB", broadcast.Outcomes[1].OwnerKey);

        lifecycle.ParkOwner(EventOwner.Form("QA"), true);
        var parked = lifecycle.FanOut(EventOwner.Form("QA"), "OnHit");
        Assert.Equal(1, parked.SkippedParked);

        lifecycle.ParkOwner(EventOwner.Form("QA"), false);
        Assert.Equal(1, lifecycle.FanOut(EventOwner.Form("QA"), "OnHit").NoHandler);
        Assert.Equal(1, lifecycle.FanOut(EventOwner.Form("Nobody"), "OnHit").UnknownOwner);
    }

    [Fact]
    public void AnEventTheGateRefusesIsRefusedForEveryOwner()
    {
        using var pex = Script();
        using var lifecycle = new PapyrusQuestLifecycle(new[] { pex }, key => key != "event.OnHit");
        lifecycle.Start(new[] { Quest("QA") });

        var report = lifecycle.Broadcast("OnHit");

        Assert.Equal(1, report.Refused);
        Assert.Equal(0, report.NoHandler);
    }
}
