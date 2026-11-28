using System;
using System.Linq;
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

    // Detects: a lifecycle that must attach into the given runtime building a private one the engine cannot see.
    [Fact]
    public void ALifecycleOverASharedRuntimeAttachesIntoThatRuntimeAndLeavesItAlive()
    {
        using var runtime = new PapyrusRuntime();
        runtime.AddPex(PexModelTests.SyntheticPex(withoutParent: true));
        var lifecycle = new PapyrusQuestLifecycle(runtime);

        Assert.True(lifecycle.Start(new[] { Quest("Q1") }).Applied);
        var instance = Assert.Single(lifecycle.InstancesFor("Q1"));
        Assert.Contains(instance, runtime.ListInstances());
        Assert.Equal("SyntheticObject", runtime.InstanceScript(instance));
        Assert.Equal(0, lifecycle.Broadcast("Actor.OnDeath").Delivered);
        _ = lifecycle.Step(10);

        lifecycle.Dispose();

        Assert.Contains(instance, runtime.ListInstances());
        Assert.True(runtime.Detach(instance));
    }

    // Detects: a shared lifecycle that must use the runtime's host gate ignoring it.
    [Fact]
    public void ASharedLifecycleUsesTheRuntimeHostGate()
    {
        using var runtime = new PapyrusRuntime(new PapyrusHostCallbacks(Gate: key => key != "quest.runtime.fragment_dispatch_timing"));
        runtime.AddPex(PexModelTests.SyntheticPex(withoutParent: true));
        using var lifecycle = new PapyrusQuestLifecycle(runtime);

        var result = lifecycle.Start(new[] { Quest("QB", fragments: 2) });

        Assert.False(result.Applied);
        Assert.Contains(result.Reasons, reason => reason.StartsWith("QB: the dispatch timing"));
        Assert.Empty(runtime.ListInstances());
    }

    // Detects: native calls on a freed runtime, which must be refused once the engine disposes it first.
    [Fact]
    public void ASharedLifecycleRefusesCallsOnceItsRuntimeIsDisposed()
    {
        var runtime = new PapyrusRuntime();
        var lifecycle = new PapyrusQuestLifecycle(runtime);
        runtime.Dispose();

        Assert.Throws<ObjectDisposedException>(() => lifecycle.IsStarted("Q1"));
        Assert.Throws<ObjectDisposedException>(() => lifecycle.Broadcast("OnHit"));
        Assert.Throws<ObjectDisposedException>(() => lifecycle.Step(1));
        lifecycle.Dispose();
    }

    [Fact]
    public void ASharedLifecycleNeedsARuntime()
    {
        Assert.Throws<ArgumentNullException>(() => new PapyrusQuestLifecycle((PapyrusRuntime)null!));
    }

    // Detects: stage identification that must never guess between none or several marked stages, or loses the stage on the way back.
    [Fact]
    public void StartupStagesAreIdentifiedOnlyWhenExactlyOneIsMarked()
    {
        var report = PapyrusQuestLifecycle.IdentifyStartupStages(new[]
        {
            new QuestStageSet("Two", new[] { new QuestStageInput(1, true), new QuestStageInput(2, true) }),
            new QuestStageSet("Q1", new[] { new QuestStageInput(0, false), new QuestStageInput(5, true, 2), new QuestStageInput(6, false) }),
            new QuestStageSet("None", new[] { new QuestStageInput(1, false) }),
        });

        Assert.Equal(new[] { "None", "Q1", "Two" }, report.Outcomes.Select(o => o.Quest));
        Assert.Equal(1, report.Identified);
        Assert.Equal(2, report.Unresolved);
        Assert.Equal(0, report.Refused);
        var q1 = report.Outcomes.Single(o => o.Quest == "Q1");
        Assert.Equal(StartupStageState.Identified, q1.State);
        Assert.Equal(5, q1.Stage);
        Assert.Equal(StartupStageState.Unidentified, report.Outcomes.Single(o => o.Quest == "None").State);
        Assert.Null(report.Outcomes.Single(o => o.Quest == "None").Stage);
        Assert.Equal(StartupStageState.Ambiguous, report.Outcomes.Single(o => o.Quest == "Two").State);
        Assert.Contains("identified=1", report.Report);
        Assert.NotEmpty(report.Digest);
    }

    [Fact]
    public void NoQuestsGiveAnEmptyStageReport()
    {
        var report = PapyrusQuestLifecycle.IdentifyStartupStages(Array.Empty<QuestStageSet>());

        Assert.Empty(report.Outcomes);
        Assert.Equal(0, report.Identified);
        Assert.Throws<ArgumentNullException>(() => PapyrusQuestLifecycle.IdentifyStartupStages(null!));
    }

    // Detects: a gate exception swallowed into a refusal that then pops out of an unrelated Step, which must surface on the call that caused it.
    [Fact]
    public void AHostGateFaultSurfacesOnTheLifecycleCallThatRanIt()
    {
        using var runtime = new PapyrusRuntime(new PapyrusHostCallbacks(Gate: _ => throw new InvalidOperationException("gate boom")));
        runtime.AddPex(PexModelTests.SyntheticPex(withoutParent: true));
        using var lifecycle = new PapyrusQuestLifecycle(runtime);

        Assert.Equal("gate boom", Assert.Throws<InvalidOperationException>(() => lifecycle.Start(new[] { Quest("Q1") })).Message);
        Assert.Equal("gate boom", Assert.Throws<InvalidOperationException>(() => lifecycle.Preflight(new[] { Quest("Q1") })).Message);
    }

    [Fact]
    public void AHostGateFaultSurfacesOnAttachAliasesAndBroadcast()
    {
        var armed = false;
        using var runtime = new PapyrusRuntime(new PapyrusHostCallbacks(Gate: _ => armed ? throw new InvalidOperationException("gate boom") : true));
        runtime.AddPex(PexModelTests.SyntheticPex(withoutParent: true));
        using var lifecycle = new PapyrusQuestLifecycle(runtime);
        Assert.True(lifecycle.Start(new[] { Quest("Q1") }).Applied);
        armed = true;

        var alias = new[] { new AliasScriptInput("Q1", 2, "SyntheticObject") };
        Assert.Equal("gate boom", Assert.Throws<InvalidOperationException>(() => lifecycle.AttachAliases(alias)).Message);
        Assert.Equal("gate boom", Assert.Throws<InvalidOperationException>(() => lifecycle.Broadcast("Actor.OnDeath", 7)).Message);

        armed = false;
        Assert.Equal(1, lifecycle.AttachAliases(alias).Attached);
        Assert.Equal(2, lifecycle.Broadcast("Actor.OnDeath", 7).Outcomes.Count);
    }

    // Detects: a shared Step that must run the runtime's queued work dropping it.
    [Fact]
    public void ASharedLifecycleStepRunsWorkQueuedOnTheRuntime()
    {
        const string source = "Scriptname Give\nInt Function Value()\n  Return 41\nEndFunction\n";
        using var compiled = PapyrusCompiler.CompileSource(System.Text.Encoding.UTF8.GetBytes(source), "Give.psc").Pex;
        using var runtime = new PapyrusRuntime();
        runtime.AddPex(compiled!.ToBytes());
        using var lifecycle = new PapyrusQuestLifecycle(runtime);
        var instance = runtime.Attach("Give", new PapyrusOwner(Native.PapyrusOwnerKind.Form, "000800:Give.esp"));
        Assert.Equal(Native.PapyrusStatus.Ok, runtime.Initialize(instance));
        var (ticket, status) = runtime.CallFunction(instance, "Value");
        Assert.Equal(Native.PapyrusStatus.Ok, status);

        var stepped = lifecycle.Step(1000);

        Assert.True(stepped.Spent > 0);
        Assert.Equal(41L, Convert.ToInt64(runtime.CallResult(ticket, consume: true).Value));
    }
}
