# AI subsystem — architecture notes

Companion doc to [`AI_Combat.txt`](AI_Combat.txt) (37,230 functions in 1.10.163, ~6.2% still
placeholder-named). This file is the narrative layer: what the classes actually do, based on live
decompiles against `fwgh_proj_1.10.163_unpacked`, not just names.

The category predicate was widened 2026-07-11 (`organize_functions.py`, category `AI_Combat`) to
also catch any `AI*`-prefixed top-level class plus `AimModel`/`BGSAimModel`/`TESAIForm` — previously
it only matched `Combat`/`Detection`/`AIProcess`/`Package`/`ProcessLists`/`*Process` substrings, which
missed `AIFormulas` (117 funcs), `AITimer`/`AITimeStamp` (43 funcs), and the aim-model classes
(34 funcs). `TESPackage`/`ActorPackage`/etc. were already covered by the pre-existing `Package`
substring match.

## Naming correction: there is no `DetectionManager` class

Worth stating explicitly since it's the natural guess: CommonLibF4 has no `DetectionManager` header
(`RE/D/` only has `DETECTION_FORMULA_TYPE`, `DETECTION_PRIORITY`, `DETECTION_TYPE`, `DetectionData`,
`DetectionEvent`, `DetectionFormulaData`, `DetectionLevels` — all plain data types, no owning-manager
class). Confirmed by decompile: **detection math lives directly on `Actor`**
(`Actor::CalculateDetectionFormula`, `Actor::CalculateDetectionRange`), with group-level aggregation
on `CombatGroup` (`CombatGroup::CalculateDetectionLevel`). There is no separate detection-manager
singleton to hook — hooking detection means hooking `Actor`'s own methods.

## Architecture, confirmed by decompile

**`Actor` owns per-frame identity/flags; `AIProcess` (at `Actor+0x300`) is the live per-actor AI
state — current package, target, command state; `TESPackage` is the data-defined behavior
(editor-authored, or quest-injected via aliases).** AI "thinking" is not a per-frame from-scratch
decision — it's event-triggered re-evaluation of which package currently wins, via
`Actor::EvaluatePackage`.

### `Actor::EvaluatePackage(bool immediate, bool force)` — RVA `0xd7f980`

The real central AI decision-trigger. Full decompile confirms:

- **Early-outs:** a vtable check (likely disabled/deleted-actor guard), an ActorValue check (skip if
  positive — reads like a paralysis/incapacitation gate) unless `force`, and
  `AIProcess::QBlockPackageEvalForCompanionCommand` (respects a player's explicit companion
  wait/stay command — package eval backs off rather than overriding it).
- **Player special-case:** package eval only runs on the player actor if
  `PlayerCharacter::GetUnderAIControl()` is true (possessed/scripted player) and the player isn't
  currently in furniture.
- **`immediate == false` path (deferred/lightweight):** just sets dirty bits (`Actor+0x2d0 |=
  0x10000`, conditionally `0x20000`) and calls `AIProcess::SetLastTimeProcessed` — schedules a
  re-eval rather than doing one synchronously.
- **`immediate == true` path (full reset):** tears down current interaction/command state
  (`SetCommandState`, `AIProcess::SetCommandPackage(null)`, `EndInterruptPackage`), unlocks
  end-of-package doors if flagged, then calls **`AIProcess::CheckforNewPackage`** — this is the
  actual package-picker entrypoint (walks the actor's package stack and commits the winner via
  `AIProcess::ChangePackage`). Followed by `AIProcess::SetTargetActivated(false)` and
  `AIProcess::SetSandboxLocation(null)` at the shared exit path.
- **The `force`-false, `immediate`-true "light" branch** (bottom `else`) skips re-eval entirely if
  the actor is mid-instant-interaction or running an interrupt package
  (`TESPackage::IsInterruptPackage`) — interrupt packages (e.g. a scripted reaction) aren't
  preempted by a routine re-evaluation.

**Practical implication for hooking:** any mod that wants to force an NPC to drop its current
behavior and re-pick should call/trigger `Actor::EvaluatePackage(true, true)` (the `force` param
bypasses both the paralysis-AV check and the companion-command block), not try to manipulate
`AIProcess`'s package pointer directly — this function already does the correct teardown sequence
(door-unlock, interrupt-package end, sandbox-location clear) that a raw package swap would skip.

### Combat decisions are a data-driven expression tree, not a hardcoded state machine

`CombatController::*` (14 matches) is built on a `CombatBehaviorTree` — boolean expression nodes
(`CombatBehaviorExpression<CombatBehaviorMemberFunc1<CombatController, bool(Actor*) const,
CombatBehaviorAttacker>, OpNot>`, etc.) wrapping actual `CombatController` member-function predicates
combined with logic operators (`OpNot` confirmed; presumably `OpAnd`/`OpOr` siblings). This confirms
FO4's combat AI decision-making is tree/expression-evaluated against member-function predicates
rather than a hand-written if/else state machine — consistent with the `TreeBuilder`/
`AddValueNode<...>` construction functions found alongside it. Not yet decompiled: the actual
tree-evaluation/scoring loop that walks these nodes per-actor per-update.

`CombatController` also processes real combat pathing events directly
(`CombatController::ProcessEvent(CombatPathStartedEvent const&)` /
`ProcessEvent(CombatPathStateChangeEvent const&)`) via a `CombatDelegate`/`EvaluateFunction<>`
dispatch template — event-driven, not polled.

## Confirmed: the `TESNPC` package-override helpers and escort-target resolution

Three more support functions decompiled, closing out references from `CheckforNewPackage_High`/
`SetCurrentPackageFromSchedule`/`CheckforNewPackage_Priv`:

- **`TESNPC::ComputeEnterCombatOverrideInteruptPackage`** — checks the actor's own
  `ExtraAliasInstanceArray` first (a quest-alias-injected override, via
  `FindEnterCombatOverrideInteruptPackage` — confirms quest aliases can inject a HIGHER-priority
  combat-interrupt package onto a specific actor instance), and only if that's absent falls back to
  `ComputePackage(npc, actor, npc+0x1e0)` — a CK-authored FormList field on the base `TESNPC` record.
  Two independent override sources, quest-alias taking precedence.
- **`TESNPC::ComputePlayerOnElevatorOverridePackage`** — simpler, just `ComputePackage(npc, actor,
  npc+0x1f0)` against a *different* FormList field (the elevator-specific override list). Both funnel
  through a shared, not-yet-decompiled `TESNPC::ComputePackage(npc, actor, packageList)` — presumably
  a condition-evaluated pick over the list, the same general shape as everything else in this doc.
- **`TESPackage::GetEscortTargetForPackage`** — resolves the escort/follow target through four
  distinct `PackageTarget` type branches (direct reference, object-type lookup, nearby-reference
  search via `TESDataHandler::EnumReferencesCloseToPoint` + an `EvaluateEscortReference` callback, and
  linked-reference). If the resolved target is the player and the package isn't flagged with a
  specific bit (`package+0x20 & 0x800`), it calls **`Actor::AddFollower`** — confirming escort
  packages can dynamically register the actor as a companion-system follower of the target, with a
  real cap/dedup check (skips if the player already has a commanding actor, is already a follower, or
  is already at/over `PlayerCharacter::GetNumberOfFollowers()`'s configured max).

## Confirmed entrypoints (RVAs, for future hook work)

| Function | RVA | Notes |
|---|---|---|
| `Actor::EvaluatePackage` | `0xd7f980` | Decompiled in full above |
| `Actor::EvaluatePackageAfter3DLoaded` | `0xd7ff20` | Deferred variant, waits for the actor's 3D to be loaded first — not yet decompiled |
| `Actor::CalculateDetectionFormula` | `0xdabb50` | Per-actor detection math; a free-function variant also exists at `0x65c500` taking `(Actor*, Actor*, DetectionFormulaData*)` |
| `Actor::CalculateDetectionRange` | `0xdac090` | Free-function sibling at `0x65ca10` |
| `CombatGroup::CalculateDetectionLevel` | `0x1030020` | Group-level detection aggregation, ties individual `Actor` detection into `CombatGroup`/`CombatTarget` |
| `TESPackage::IsActorAtLocation` | `0x71f2c0` | Package-location arrival check |
| `TESPackage::GetUnlockDoorsAtEnd` / `GetInitialTargetCount` | (seen inline in `EvaluatePackage`) | Package metadata queried during the immediate-reeval path |

## Confirmed: `AIProcess::CheckforNewPackage` — the real tiered package selector

Full decompile (recorded in `DECOMPILE-RECORDS.md`, category `AI`) confirms this is a **dispatcher**,
not the selection logic itself — it routes to one of three tiered implementations based on which
per-tier data pointers (`this+0x10`, `this+8`) are populated on the `AIProcess`:

- Rejects immediately: null actor, a specific actor flag (`actor+0x440` bit 4), or the player actor
  (matches `Actor::EvaluatePackage`'s own player special-case — the player never goes through the
  tiered dispatcher).
- Reads an ActorValue via a vtable call on `actor+0x58` (the same call shape seen in
  `EvaluatePackage`) — **>= 1.0 blocks package re-evaluation** (this sharpens the earlier "positive
  blocks" note to an exact threshold; reads like a paralysis/incapacitation value).
  routes to **`CheckforNewPackage_Priv`** (base/low tier — the common case, no extra per-tier data),
  **`CheckforNewPackage_MiddleHigh`** (named but not yet decompiled), or **`CheckforNewPackage_High`**
  depending on tier.

This is FO4's concrete implementation of the classic Bethesda "process list" perf tiering
(High/MidHigh/Middle/MidLow/Low from `RE::ProcessLists`) applied specifically to AI package
re-evaluation cost: nearby/important actors get the expensive tier, distant ones get the cheap one.

### `CheckforNewPackage_Priv` (RVA `0xe2f1c0`) — the actual package-scheduling algorithm

The real "which package wins" logic (large function, fully decompiled):

- **Alarm lock-in:** if the actor is alarmed (`Actor::IsAlarmed`) and this isn't a forced re-eval,
  refuses to switch packages at all — an alarmed actor's current (presumably combat/alert) package
  is protected from routine schedule-driven interruption.
- **No current package:** falls straight to `SetCurrentPackageFromSchedule` (pulls the next
  editor-authored/AI-schedule package).
- **Schedule-time math:** compares the current package's configured duration/start-time
  (`PackageSchedule::GetPackageStartTime`) against `Calendar::GetHour`, with explicit day-wraparound
  handling — this is the actual NPC daily-schedule rotation (go to work / eat / sleep at their
  configured times).
- **Exceptions that block/allow a switch:** `TESPackage::GetOncePerDay` (+ `Actor::
  SetRunOnceExtraPackage`, so a once-per-day package doesn't re-fire same-day), `TESPackage::
  IsScriptPackage` (a running script package resists being swapped out), `TESPackage::
  IsInterruptPackage`.
- **Door-unlock side effect:** when starting or ending a package flagged `GetUnlockDoorsAtEnd`/
  `GetUnlockDoorsAtStart`, calls `TESDataHandler::EnumReferencesCloseToPoint` in a ~5000-unit radius
  around the actor to unlock nearby doors — package transitions can have this as a genuine gameplay
  side effect, not just a state change.
- **Escort targets:** resolves `TESPackage::GetEscortTargetForPackage` and stores it as the process's
  live target when the incoming package targets an actor.
- Commits via `StartNewPackage`/`SetCurrentPackage`.

### `CheckforNewPackage_High` (RVA `0xe54780`) — adds combat-interrupt override + weapon equip

Falls through to `CheckforNewPackage_Priv` for the base logic, but High-tier actors additionally get,
**before** the base check: if the actor is in a "just entered combat"-style state (a vtable check on
`actor+0x7f0` and `!Actor::IsIgnoringCombat`), computes
`TESNPC::ComputeEnterCombatOverrideInteruptPackage` and, if it differs from the current package,
immediately installs it via `InitiateCombatInterruptOverridePackage_MiddleHigh` — this is the
mechanism behind e.g. a sandboxing shopkeeper instantly switching to a flee/reaction package the
moment a fight starts nearby. **After** a successful package change, High-tier actors also get
`Actor::EquipBestWeapon` called automatically — confirming weapon-readiness is only auto-managed for
the high-detail process tier; distant/low-tier actors do not get this treatment on package change.

## Confirmed: `Actor::CalculateDetectionFormula` — two-stage, event-driven, not a single formula

Full decompile substantially revises the earlier "detection math lives on Actor" framing: **`Actor`
is only the entry point; the actual math lives in a separate `AiFormulas` namespace** (which is
exactly the `AIFormulas` class this session's widened category predicate started catching — see
above). Confirmed two-stage pipeline:

1. **Baseline formula:** `GatherDetectionFormulaData` + `AiFormulas::CalculateDetectionFormula` —
   produces baseline min/max detection thresholds. Skipped entirely (early return) unless faction
   reaction between the two actors is hostile-ish (`GetFactionFightReaction` outside the
   neutral/ally range) AND at least one side has a live `CombatController` (`actor+0x328`) — or the
   target is the player and is hostile.
2. **Event-driven refinement:** pulls the target's `AIProcess::GetActorsDetectionEvent` (the actual
   sound/sight stimulus that triggered this check — e.g. gunshot, footstep, spotted), computes
   distance from the event position against a radius threshold that **differs for the player vs. NPCs**
   (two separate float constants), then gates on line-of-sight (`CalculateLOS`) and a field-of-view
   check (`CalculateViewDelta` against a fixed half-angle), and finally calls `AiFormulas::
   CalculateDetectionEventFormula` to refine the thresholds using the specific stimulus.

Net effect: an actor's "am I detected" state is not one continuous formula — it's a baseline
hostility-gated formula PLUS a separate, LOS/FOV-gated event-driven spike triggered by specific
stimuli, matching the classic stealth-game "detection meter climbs faster on gunfire/footsteps within
sight" design. `AiFormulas::CalculateDetectionFormula` / `CalculateDetectionEventFormula` themselves
are the next real target if the exact numeric formula (light level / sneak skill / distance weights)
is ever needed — not decompiled this pass.

## Dead end, then resolved: `CombatBehaviorTreeNode::Func1` is the destructor, not `Evaluate` — because there is no `Evaluate`

`CombatBehaviorTreeNode::Func1` (RVA `0x10cc310`) was the strongest placeholder-named candidate for
the tree's evaluation dispatch. Decompiled and it's the class's own scalar deleting destructor
(resets the vtable pointer, releases child nodes via their own vtable slot 0, releases a
`BSStringPool::Entry` label, conditionally frees `this`).

The real answer, found by dumping `CombatBehaviorTreeNode`'s full vtable
(`FindDumpVtable.java VTABLE_CombatBehaviorTreeNode`): the base class doesn't have an `Evaluate`
slot at all — it has a **behavior-tree node lifecycle**: `[0] dtor, [1] GetName, [2] Enter, [3] Exit,
[4] Update, [5] Abort, [6] SaveGame, [7] LoadGame, [8] Validate, [9] GetType`. The per-tick evaluation
happens in **`Update`**, not a method named "Evaluate" — a naming assumption, not a fact, and worth
recording as a lesson: don't search for the term a *design pattern* would suggest, dump the vtable
and read the real slot names.

## Confirmed: the combat behavior tree is a genuine ~160-node production behavior tree, execution-state-resumable via `CombatBehaviorThread`

The same vtable dump revealed **~160 distinct concrete node types** sharing this vtable shape —
`CombatBehaviorAttack`, `AcquireOptimalRange`, `AttackFromCover`, `Bash`, `Block`, `CastImmediateSpell`,
`Chase`, `ChildSelector`, `Sequence`, and dozens more covering essentially every discrete combat
action and control-flow primitive (composite/branching nodes plus leaf action nodes) — this is a
full, CK/engine-authored production behavior tree, not a simplified boolean-expression evaluator as
the first pass's `CombatBehaviorExpression<>` sighting suggested (that's a *predicate-building*
sub-system used *within* node condition checks, not the tree's own control flow).

Decompiled `Update` for three representative node types plus the shared base `Enter`/`GetName`:

- **`CombatBehaviorTreeNode::Enter`** (base, shared) — literally an empty function (`return;`). The
  base class provides no default entry behavior; each node type that needs one overrides it.
- **`CombatBehaviorTreeNode::GetName`** (base, shared) — trivial accessor, returns the `BSFixedString`
  stored inline at `node+8` (the same field the destructor releases as a `BSStringPool::Entry`).
- **`CombatBehaviorTreeNodeObjectBase<CombatBehaviorSequence>::Update`** — the real "run children in
  order" logic: reads a per-thread child-index counter (stored in **`CombatBehaviorThread`**, NOT on
  the node itself — see below), increments it, and if there are more children, **descends** into the
  next one (sets the thread's current-node pointer to the child); once children are exhausted, calls
  **`CombatBehaviorThread::Ascend()`** to pop back to the parent. This is a resumable, one-step-per-
  tick execution model, not a full top-to-bottom tree walk every update.
- **`CombatBehaviorTreeNodeObjectBase<CombatBehaviorChildSelector<RandomChanceChildSelector>>::Update`**
  — for this specific selector policy, `Update` is JUST an `Ascend()` call. This round found the real
  answer: it's not on `ChildSelector` at all, it's on the SIBLING template `CombatBehaviorFallbackSelector<Policy>`
  — see below, fully resolved.

## Resolved: the random-child-selection chain — `FallbackSelector::Update` → `::Enter` → `Policy::SelectChild`

Followed the chain one level at a time until it hit real logic:

1. **`CombatBehaviorTreeNodeObjectBase<CombatBehaviorFallbackSelector<RandomChanceChildSelector>>::Update`**
   (RVA `0x11d6a80`) — checks a thread state field (`thread+0x148`); if not in the right state, just
   `Ascend()`s (bails). Otherwise calls `CombatBehaviorThread::SetFailed(thread, false)` and a
   **non-virtual, class-specific** `CombatBehaviorFallbackSelector<RandomChanceChildSelector>::Enter`
   (i.e. a plain method, not another vtable dispatch — this is why the vtable dump alone couldn't find
   it).
2. **`CombatBehaviorFallbackSelector<RandomChanceChildSelector>::Enter`** (RVA `0x11d0270`) — calls
   **`RandomChanceChildSelector::SelectChild(node, lastPickedIndex)`**. On success (`!= 0xFFFFFFFF`),
   calls `CombatBehaviorThread::Descend(thread, childIndex)` — confirming `Descend` is `Ascend`'s
   counterpart, moving execution INTO the picked child. On failure, marks the thread failed and
   `Ascend()`s (the fallback selector itself fails, its own parent decides what happens next).
3. **`RandomChanceChildSelector::SelectChild`** (RVA `0x10c3970`) — the actual algorithm, and it's
   NOT a single weighted roll: **each non-excluded child independently rolls its own probability
   check.** For every child not already marked excluded (a per-instance bitmask on `this`), it calls
   a per-child vtable method (slot `0x50` — presumably `GetChance()` on the child's own condition
   data) to get a `0.0-1.0` chance value, rolls `BSRandom::Float0To1()`, and includes the child in a
   scratch "passed" array if the roll is `<=` its chance. **Multiple children can pass, or none.** If
   any passed, one is picked **uniformly at random** among the passers (`BSRandom::UnsignedInt()`
   indexes the scratch array) and that child's bit is added to the exclusion mask (won't be
   re-offered again this tree walk without a reset). Returns the fail sentinel if nobody passed.

**Also decompiled the sibling `WeightedRandomChildSelector::SelectChild`** (RVA `0x10c3b90`) for
comparison — genuinely different algorithm, true roulette-wheel weighted selection: sums each
eligible child's weight (same vtable-slot-`0x50` query, but requires `> 0.0` rather than a
probability check) into a running total, then rolls a single `BSRandom::Float(0, totalWeight)` and
walks the accumulated-weight buckets to land on one child — i.e. higher-weight children are
proportionally more likely, and exactly one child is always picked if any are eligible (unlike
`RandomChanceChildSelector`, which can produce zero OR multiple candidates before its final uniform
pick).

## Confirmed: the complete 8-policy child-selector family

All eight `SelectChild` implementations decompiled — small functions, full picture. Each takes
`(node, lastPickedIndex)` and returns the next child index or the `0xFFFFFFFF` fail sentinel; all
per-child queries go through the same per-child vtable slot `0x50` (a condition/value/priority/chance
accessor whose exact meaning is type-dependent — CK-authored per-child data, name not recovered):

| Policy | Behavior |
|---|---|
| `NextChildSelector` | Pure sequential: returns `lastIndex + 1`, fails past the end. Round-robin/in-order iteration — no per-child query at all. |
| `PriorityChildSelector` | Scans remaining children, queries each one's priority (int, via slot `0x50`), tracks the highest seen. **Short-circuits immediately on a priority of exactly `100`** — a "max priority, stop searching" sentinel value. |
| `ValueChildSelector` | Queries the **node's own** value (slot `0x50` on the node itself, not per-child), then returns the first child whose own value (slot `0x50`) matches — a value-based dispatch, functionally a switch/case over children. |
| `ConditionalChildSelector` | Returns the first remaining child whose condition (slot `0x50`, nonzero = true) passes — an if/elseif chain over children. |
| `IndexChildSelector` | Ignores `lastPickedIndex` and children entirely — just returns whatever the **node's own** slot `0x50` reports. A fixed/CK-authored index selector with no runtime evaluation. |
| `RandomChanceChildSelector` | Independent per-child probability roll (own section above) — 0, 1, or many candidates, uniform pick among passers. |
| `WeightedRandomChildSelector` | True roulette-wheel weighted selection (own section above) — always picks exactly one if any eligible. |
| `RandomChoiceChildSelector` | The simplest random policy: queries a **single** chance value from the **node itself** (not per-child), rolls once, returns `0` or `1` — a binary coin-flip choosing between exactly two children, fundamentally different from the two per-child-random policies above. |

This is the complete picture of how FO4's combat behavior tree resolves "which child runs next" at
every composite node: six deterministic-or-simple strategies (sequential, priority, value-switch,
first-true-conditional, fixed-index) plus three genuinely different flavors of randomness (binary
coin-flip, independent multi-child chance, and magnitude-weighted). Each is presumably one CK-facing
composite node type. None of the eight `SelectChild` bodies reveal what the per-child slot-`0x50`
data actually represents in CK terms (condition record? raw float field? — not confirmed), but the
selection ALGORITHMS themselves are now fully known.
- **`CombatBehaviorTreeNodeObjectBase<CombatBehaviorAttack>::Update`** — a real LEAF action node:
  the templated `Update` is just glue (resolves the concrete `CombatBehaviorAttack*` instance's
  location within a per-thread data blob at a byte offset stored in the thread, `thread+0x120`) that
  forwards to a plain, non-virtual **`CombatBehaviorAttack::Update()`** — the actual per-tick attack
  logic lives one layer deeper still, in that plain method, not decompiled this pass.

**Architectural conclusion:** `CombatBehaviorThread` (not the tree nodes themselves) owns the live
execution state — current node, per-node child-index counters, a data blob for concrete node
instances — meaning the SAME static tree definition can be walked independently and concurrently by
multiple actors' threads without any per-actor tree copy. This is a real, efficient, resumable
behavior-tree implementation, and now confirmed rather than assumed.

## Confirmed: `AIProcess::SetCurrentPackageFromSchedule` and the third tier, `CheckforNewPackage_MiddleHigh`

- **`SetCurrentPackageFromSchedule`** (RVA `0xe2d660`) — the function `CheckforNewPackage_Priv` calls
  when there's no current package. Two distinct paths: if the player is riding an elevator-type
  object (`player+0xe02` bit 2), computes `TESNPC::ComputePlayerOnElevatorOverridePackage` (a special
  package for NPCs reacting to the player using an elevator). Otherwise, under a set of conditions
  (actor sitting/sleeping, not following the player within double a distance threshold, or the
  player's camera isn't in a specific mode) falls through to `Actor::InitiateEscortFollowerPackage` —
  i.e. this is also where **companion/follower package assignment** is decided, not just generic
  schedule packages. Commits via `SetCurrentPackage` + `SetupNewPackage`, and clears the actor's
  look-at target unless it's actively in combat.
- **`CheckforNewPackage_MiddleHigh`** (RVA `0xe80650`) — the third tier. Rejects re-evaluation
  outright if the actor is in dialogue, alarmed, or running a "run once" package. Has its own
  "objects to acquire" list teardown (clearing item-acquisition state via `ActorPackage::Clear`) not
  present in the `_Priv` tier. Falls through to `CheckforNewPackage_Priv` for the core logic, then on
  success resets several per-actor idle/target/continuation flags and calls
  `TESObjectREFR::InitDefaultWorn` — re-equips the actor's default outfit when appropriate. This tier
  sits between `_Priv` (cheapest) and `_High` (combat-interrupt + weapon-equip, see above) in cost and
  behavior scope.

## Confirmed: `AiFormulas::CalculateDetectionFormula`/`CalculateDetectionEventFormula` — structure, not exact tuning values

Both decompiled in full. They're dense floating-point formulas built almost entirely from unnamed
`DAT_...` float constants (raw game-balance tuning literals with no symbol names in this build — the
STRUCTURE is confirmed, the exact numeric weights are not meaningfully recoverable without a separate
constant-naming pass). What the structure confirms:

- **Distance falloff**: both formulas normalize a distance value (`(range - actualDistance) / range`,
  clamped to ≥0) and raise it to a power (`powf(x, exponent)`) — a classic nonlinear falloff curve,
  not a linear one.
- **A dozen-plus independent boolean toggle terms** (checked via byte-sized flags at many distinct
  offsets on the shared `DetectionFormulaData`/`DetectionEvent` struct) each additively or
  multiplicatively contribute a bonus/penalty constant when set — consistent with per-state modifiers
  like sneaking / running / weapon-drawn / light-level bucket / player-vs-NPC, though the exact
  flag→meaning mapping isn't confirmed (no CommonLibF4 `DetectionFormulaData` layout exists to check
  against).
- **Two separate output channels** on the data struct: one field (`+0x60`) accumulates a
  distance/stat-based score, a second (`+0x5c`) accumulates an angle-of-view-gated score — confirming
  the earlier finding that FOV/heading is a first-class input to the numeric formula, not just a
  binary LOS/FOV gate applied outside it. The event-formula variant explicitly re-checks a heading
  angle against a `DEG_1_133`-scaled threshold near the end.
- Queries a `BGSObjectInstance` (constructed from the observing actor) for what looks like an
  equipment-based modifier (armor/clothing concealment rating is the natural candidate given FO4's
  known sneak mechanics, but the specific queried property is not confirmed from this decompile alone).

## Confirmed: `CombatBehaviorAttack::Update` — a real leaf node's self-scheduling execution

The plain (non-templated) leaf method, one layer below the `CombatBehaviorTreeNodeObjectBase<
CombatBehaviorAttack>::Update` glue decompiled in round 1. Key confirmed facts:

- **Reads the live combat target from thread-local context**, not from the node itself: walks
  `CombatBehaviorThread` (found via TLS) → a nested context struct → the current target's actor
  handle — meaning target state is thread-owned, consistent with round 2's finding that
  `CombatBehaviorThread` (not the node) owns execution state.
- **Self-scheduling via a per-node timer**: the node stores its own `nextAllowedTime`
  (`this+0x14`) and `startTime` (`this+0x10`), compared against a global `AITimer::fTimer` each
  `Update` — so `Update` is called every tick regardless, but does nothing until its own timer
  elapses. This is the concrete mechanism behind the resumable, cheap-per-tick execution model
  inferred in round 2.
- **Early-exit via `Ascend()` when the actor has left the relevant combat state** (a 2-bit state field
  at `actor+0x130` bits 0x19-0x1a) — calls `CombatBehaviorContextMelee::FinishedAttack` then
  `CombatBehaviorThread::Ascend()`, confirming leaf nodes use the exact same "pop back to parent"
  signal as composite nodes (`Sequence`/`ChildSelector`) when they naturally complete — `Ascend()` is
  a universal tree-navigation primitive, not composite-node-specific.
- **Real attack logic**: attempts `CombatBehaviorContextMelee::StartChargeAttack`; if that doesn't
  produce attack data, checks whether the target is within a 180°-ish heading cone
  (`Actor::IsPointInHeadingCone`) and either arms a short retry timer (target briefly out of cone) or
  plays a specific combat animation (`CombatAnimation::Execute(actor, 5, 0)` — animation ID 5, not
  further identified) when the target isn't reachable at all.

This confirms the behavior-tree model end-to-end: composite nodes (`Sequence`, `ChildSelector`)
manage control flow via `CombatBehaviorThread`'s current-node pointer and per-thread child-index
counters; leaf nodes do real work gated by their own per-instance timers, and both use `Ascend()` as
the shared "done, return control to parent" signal.
