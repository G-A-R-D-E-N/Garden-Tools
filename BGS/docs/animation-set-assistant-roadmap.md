# BGS animation-set assistant roadmap

## Goal

The long-term target is a truthful workflow for diagnosing and repairing a Fallout 4 animation set through existing BGS evidence and verified operations.

The assistant should progress from read-only diagnosis to narrow approved mutations. It must not expose generic Havok/native writes or claim a repair succeeded until BGS saved, reopened, and revalidated the affected files.

## End-state workflow

```text
inspect active clip/project
  -> identify actual BGS findings
  -> locate intended animation
  -> build a typed repair plan
  -> preview one approved operation at a time
  -> validate every candidate
  -> keep editor mutations undoable and unsaved
  -> save only through verified BGS transactions
  -> reopen affected HKX files
  -> rerun project validation
```

## Proven building blocks

### Behavior/project diagnosis

Existing core already provides:

- `HkxTextEdit.TextOf`
- `BehaviourGraphModel`
- `HkxBehaviorParser`
- `GraphValidator`
- `ProjectSearch`
- `ProjectCheck`
- `ProjectChain`
- `HkReferences`
- `ElementSummary`
- `StateEditor` / `StateRoutes`
- `SymbolEditor`
- `BindingEditor`
- `GameData` / subgraph diagnostics

These cover graph structure, references, symbols, project relationships, missing assets, load-order-aware animation resolution, and per-weapon/subgraph gaps.

### Animation data

`HkxBinaryReader` / `HkxAnimationData` expose supported decoded animation classes, timing, frames/tracks, annotations, binding metadata, and skeleton data.

`AnimationEdit` already contains semantic operations for trim, retime/resample, and associated root-motion handling.

These are the preferred future animation-edit primitives. Do not expose arbitrary transform-array replacement first.

### Native behavior authoring

`NativeAuthoringPlan` provides typed native mutation primitives and verified application. `BehaviourAuthoringSession` provides higher-level behavior authoring including clip creation.

Future assistant operations should use semantic wrappers such as:

```text
add_clip
add_state
add_transition
bind_variable
```

Do not expose `NativeAuthoringPlan` itself to a model.

## Problem classes and authority

### A. Wrong clip animation

Evidence:

- selected `hkbClipGenerator`
- current `animationName`
- asset existence through `ProjectChain` / `GameData`
- declaration membership
- validator delta

First approved write:

```text
bgs.set_clip_animation
```

Defined in `assistant-phase3-first-write.md`.

### B. Animation exists but is not declared by the character

This is a separate character-file operation.

The proven Fallout 4 declaration shape is not a top-level text array. Shipped metadata defines:

```text
hkbCharacterStringData.animationBundleNameData
  -> array<hkbAssetBundleStringData>

hkbAssetBundleStringData.assetNames
  -> array<stringptr>
```

The current read path intentionally flattens nested `hkcstring` asset names for `ProjectChain.DeclaredAnimations`, but the native writer does not yet support writing a nested text array inside a struct-array element.

Therefore the future semantic operation:

```text
add_character_animation_declaration
```

is blocked on the proof gates in `assistant-character-declaration-contract.md`.

Do not implement it with `SetTextArray`, `SetStructMember`, raw XML, or a binary patch.

### C. Character declares a missing animation

Possible repairs are distinct:

```text
place/import the missing asset
change/remove declaration
change consuming clip references
```

Search consumers before removal. Do not convert one diagnosed problem into another.

### D. Unsupported animation/class

Use `HkxBinaryReader` support state and `RoundTripReport`.

Policy:

- report the unsupported boundary
- refuse authoring that requires unknown reconstruction
- never synthesize unknown native fields

### E. Animation timing/content change

Future semantic operations:

```text
preview_animation_trim
apply_animation_trim
preview_animation_retime
apply_animation_retime
```

These require extraction of the current UI-owned animation edit state into a reusable session with preview, undo, save, reopen, and fidelity proof.

### F. Behavior graph authoring

Use typed editor/native sessions for operations such as:

```text
add_clip
add_state
connect_generator
add_transition
```

Every operation needs object/link preview, validation delta, one editor undo step where applicable, and explicit save.

### G. Symbols/bindings

Potential semantic operations:

```text
add_event
add_variable
bind_variable
remove_binding
rename_symbol
```

External name consumers make rename operations higher risk. Show reference impact before approval.

### H. Weapon/subgraph coverage

Use current `GameData`, subgraph index, crash-hash, and validator evidence first.

If the real repair is an asset placement under a weapon-specific path, do not approximate it by rewriting behavior references.

## Multi-document boundary

A full repair can span:

```text
behavior.hkx
character.hkx
animation.hkx
filesystem asset placement
```

The current desktop editor owns one active document. Until BGS has a proven multi-document transaction coordinator, use separately approved steps with independent source stamps and explicit saves.

Initial safe sequence:

```text
1. diagnose all affected targets
2. preview behavior edit
3. approve/apply behavior edit to active editor session
4. preview separate character/asset operation when supported
5. validate combined project state where possible
6. explicitly save each target
7. reopen every modified HKX
8. rerun ProjectChain/GraphValidator acceptance
```

A later coordinator needs dedicated failure-injection proof for source changes, partial publication, backups, and recovery. Do not build one just to make the assistant API appear atomic.

## Asset import boundary

The MCP backend remains narrow: it exposes bounded reads and the exact-source clip-animation
operation only. The in-app assistant should not gain a generic filesystem API.

If BGS later owns asset import, expose a narrow operation such as:

```text
import_animation_asset(source, projectRelativeTarget)
```

Requirements:

- user-authorized source
- target constrained to resolved project animation root
- canonical path validation
- no overwrite by default
- hash/size preview
- supported HKX validation where applicable
- verified copy result

Otherwise the assistant should report the expected destination instead of inventing file movement.

## Typed repair plan

Use a small stable representation rather than relying on prose alone:

```text
RepairPlan
  summary
  evidence[]
  steps[]
    operationId
    target
    read/write
    requiresApproval
    dependencies[]
  acceptanceGates[]
```

The plan is advisory. Each write step still executes through a real BGS operation and its own approval/stale-state checks.

## Context strategy

For large projects, drill down instead of dumping data:

```text
project summary
  -> bounded diagnostics
  -> targeted search
  -> exact object inspection
  -> exact animation inspection
  -> repair proposal
```

Rules:

- aggregate counts first
- group repeated findings
- cap examples
- return stable object IDs/paths
- do not return every graph object
- do not return every declared animation without paging
- do not return animation transforms without an explicit narrow frame/track request

## Operation progression

### Tier 1: read-only inspection

```text
inspect_behavior
resolve_project_chain
check_project
search_project
inspect_animation
inspect_object
```

### Tier 2: first reversible active-document write

```text
set_clip_animation
```

### Tier 3: symbols/bindings and narrow graph edits

Only operations with proven editor-session and validator behavior.

### Tier 4: native graph authoring

Higher-level operations backed by `BehaviourAuthoringSession`, never arbitrary native field access.

### Tier 5: animation-content operations

Trim/retime and later semantic edits after animation-session extraction and fidelity proof.

### Tier 6: character declaration and cross-document repair

Character declaration work first requires:

```text
real-fixture declaration read regression
nested struct text-array native writer
save-verifier coverage
semantic approval operation
cross-document source-stamp handling
```

See `assistant-character-declaration-contract.md`.

### Tier 7: explicit save automation

Last. Approval must name exact dirty documents, source identity, and validation state. External MCP
remains limited to the separately proven exact-source clip operation; desktop editor state and
broader cross-document writes remain out of process.

## Full-workflow acceptance gate

Do not call BGS capable of full animation-set repair until a representative fixture workflow proves:

1. diagnose an intentionally broken relationship
2. locate the intended asset through bounded tools
3. preview and approve each required operation
4. validate unsaved candidates
5. save through existing verified transaction paths
6. reopen every modified HKX
7. rerun project validation with the same project/game-data context
8. verify backup/rollback artifacts
9. verify unrelated content against the appropriate fidelity contract

A model answer or synthetic DTO test is not enough.

## Truthfulness rule

Good:

```text
BGS found clip #123 references Animations\Foo.hkx. The file is missing from the attached project/game-data view. I can change this clip to the existing Animations\Bar.hkx. The candidate removes this clip's missing-animation finding and would remain unsaved until you save it.
```

Bad:

```text
I fixed the animation set.
```

unless BGS actually executed every required operation, saved, reopened, and revalidated the result.
