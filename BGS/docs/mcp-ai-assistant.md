# BGS MCP and model assistant integration

## Status

The backend and assistant drawer are present. The desktop assistant also exposes approved
interactions with existing editor controls; typed semantic mutation tools remain separate.

### Desktop control access

`bgs.editor_state` returns visible controls, field values, choices and supported actions across
the main editor, authoring windows and open menus. Filter by label/type with `query`, paginate
controls with `offset`/`limit`, and paginate choices or scene items with `itemOffset`.
Graph nodes/ports and skeleton bones/bodies report their current viewport coordinates.

`bgs.editor_action` proposes one observed control interaction: buttons, text fields, selections,
checkboxes, numeric values, expansion, scrolling, keys or viewport gestures. It requires explicit
approval bound to that proposal and chat. Changed documents, fields, control availability, cameras
or gesture geometry invalidate approval. Pause playback/simulation before selecting moving geometry.

`bgs.editor_file` replaces native file pickers with explicit path proposals for HKX/NIF loading,
comparison, archive browsing, script/game/mod folders and diff export. Export destinations stay
inside the active project. Use the Window control ID returned by `editor_state`; native picker
buttons have no direct assistant click action.

These tools reuse desktop handlers and their validation, undo, backup and save behavior. Dispatch
only means an interaction was sent. The assistant inspects state after approval before reporting
the result, and any next action requires another approval. Queued prompts wait while approval is
pending. Modal dialogs receive a separate approval window so their controls remain accessible.
Account settings, password controls, chat contents, approval controls and clipboard shortcuts
are excluded. No shell access is added.

The desktop provider sessions share this tool catalog, including the authenticated CLI bridge.
The separate headless MCP server retains its inspection-only catalog; it cannot operate an editor.

Source implementation must follow the Ponytail proof-first/reuse-first workflow: https://github.com/dietrichgebert/ponytail.

Any new Fallout 4 runtime/layout interpretation or reverse-engineered semantic claim must be checked against `PRISMA-USER-INTERFACE-FRAMEWORK/commonlib_NonVR` before implementation. That repository is reference-only.

## Goal

Add two related capabilities without building a second editor:

1. a local read-only MCP server exposing safe typed BGS inspection operations
2. an optional in-app assistant that uses the same operations plus bounded editor context

Later write authority is added one semantic operation at a time only after the corresponding editor/native path is proven.

## Verified repository boundaries

### Core

`BGS/src` owns reusable HKX/NIF/archive parsing, graph models, project analysis, validation, native mutation, save verification, and file safety.

High-value existing surfaces include:

```text
HkxTextEdit
BehaviourGraphModel
HkxBehaviorParser
GraphValidator
ProjectChain
ProjectCheck
ProjectSearch
HkxBinaryReader
RoundTripReport
HkReferences
ElementSummary
NativeAuthoringPlan
BehaviourAuthoringSession
AnimationEdit
```

### Desktop editor

`BGS/app` owns Avalonia UI and active editor-session state.

Important boundaries:

- `MainWindow.History.cs`: `Commit`, undo/redo, dirty state, document revision
- `MainWindow.Edit.cs`: existing graph/symbol edits enter the normal commit path
- `MainWindow.Document.cs`: explicit save and disk publication
- `MainWindow.Animation.cs`: animation-frame state is still UI-owned and is not yet a reusable model-write surface
- `EditorShell`: existing Home/Graph/Inspect/Animation/Project activities

### Publication

Existing verified publication paths include:

```text
DocumentSaveTransaction
AnimationSaveTransaction
GraphMutationTransaction
SaveVerifier
FileSafety.ReplaceChecked
```

These remain authoritative. Model integration does not bypass them.

### Proof infrastructure

Existing proof includes:

- xUnit regressions under `BGS/tools/tests`
- native/Havok regression harness under `BGS/tools/symrm`
- Avalonia headless UI smoke under `BGS/tools/uismoke`
- committed pristine Fallout 4 behavior/character/animation/skeleton fixtures
- byte/structural/refusal save-fidelity contracts

BGS targets `.NET 8`, pinned to SDK `8.0.424`, and the desktop uses Avalonia `12.1.2`.

## Architecture

```text
External MCP client                       BGS desktop assistant
        |                                         |
        v                                         v
  stdio MCP adapter                         IChatClient pipeline
        |                                         |
        +----------------+------------------------+
                         |
                         v
                 typed BGS operations
                         |
              +----------+----------+
              |                     |
              v                     v
      AssistantInspection     editor-session ops
       read-only core         approved writes
              |                     |
              +----------+----------+
                         |
                         v
                       BGS core
                         |
                 verified save gates
```

The MCP server and assistant share domain operations, not transport/provider implementation.

## Phase 1: read-only inspection + MCP

### Core boundary

`AssistantInspection` is the single transport/provider-neutral inspection façade.

It has:

- no Avalonia dependency
- no MCP dependency
- no model-provider dependency
- bounded typed results
- stable structured domain failures

Exact façade, DTO, bounds, cancellation, concurrency, and error behavior are defined in:

`mcp-core-inspection-contract.md`

The MCP adapter may build a side-effect-free clip preview through `AssistantDiskMutation`, but it
does not expose that type's disk publication path.

### MCP boundary

A separate `BGS/tools/mcp` .NET 8 console project exposes:

```text
bgs.inspect_behavior
bgs.resolve_project_chain
bgs.check_project
bgs.search_project
bgs.inspect_animation
bgs.inspect_object
bgs.preview_set_clip_animation
```

The host uses stdio only, authorizes paths against configured roots, keeps stdout protocol-clean,
and calls `AssistantInspection` for reads or the read-only preview adapter.

Exact project/transport/path/protocol/CI behavior is defined in:

`mcp-phase1-contract.md`

### Context rules

Core model-facing defaults are:

```text
list/search default: 50
hard cap: 200
display strings: 500 characters
```

No raw XML, raw bytes, whole project graph, or full animation frame-transform dump is returned by default.

## Phase 2: in-app assistant

The desktop assistant uses `Microsoft.Extensions.AI.IChatClient` as the provider-neutral chat abstraction rather than defining a BGS-specific provider framework.

Initial direction:

- OpenAI-compatible HTTP first
- right-side Avalonia drawer, not a sixth primary activity
- same BGS read operations called in-process
- bounded typed editor context
- explicit distinction between disk and unsaved editor state
- last 8 normal conversation turns initially
- maximum 6 tool iterations per user request initially
- sequential tool calls initially
- API key values from environment variables, not plaintext `settings.cfg`
- deterministic fake-client tests; normal CI never requires a paid/live endpoint

Exact provider/session/context/UI behavior is defined in:

`assistant-phase2-contract.md`

## Phase 3: first approved editor mutation

The first write is deliberately narrow:

```text
bgs.set_clip_animation
```

It may only change `animationName` on an existing `hkbClipGenerator` in the active editor document.

Required behavior:

```text
build candidate
  -> compare validation
  -> show preview
  -> explicit approval
  -> bind approval to exact document revision/object/old/new value
  -> recheck stale state
  -> apply through normal editor Commit semantics
  -> exactly one undo step
  -> leave dirty and unsaved
```

No automatic Save.

Exact contract:

`assistant-phase3-first-write.md`

The desktop active-document mutation remains undoable and unsaved. External MCP remains read-only;
it does not access desktop editor state or publish disk changes.

## Character declaration boundary

Character animation declarations require separate native-format proof.

Shipped Fallout 4 metadata defines:

```text
hkbCharacterStringData.animationBundleNameData
  -> array<hkbAssetBundleStringData>

hkbAssetBundleStringData.assetNames
  -> array<stringptr>
```

The existing `ProjectChain.DeclaredAnimations` read path is internally consistent because `BehaviourGraphModel` flattens nested `hkcstring` asset values from the bundle array.

Writing the declaration is not currently covered by `NativeAuthoringPlan.SetTextArray` or `SetStructMember`. The nested text-array member needs its own verified native writer and verifier coverage before a semantic declaration operation exists.

Exact proof/write boundary:

`assistant-character-declaration-contract.md`

## Full animation-set progression

Do not expose all native capabilities at once.

Authority grows through this order:

```text
1. read-only project/behavior/animation diagnosis
2. set_clip_animation
3. narrow symbols/bindings/graph edits
4. typed graph authoring through proven BGS sessions
5. semantic animation-content operations such as trim/retime
6. character declaration/cross-document operations
7. multi-document repair coordination
8. explicit model-driven Save
```

The full problem taxonomy, context strategy, and acceptance definition are in:

`animation-set-assistant-roadmap.md`

## Security model

### MCP

Phase 1:

- local stdio only
- user/client-configured allowed roots
- canonical component-based containment checks
- static symlink/junction/reparse escapes fail closed initially
- existing `InputFilePolicy` remains authoritative after authorization
- no arbitrary shell/process execution
- no filesystem writes
- no raw binary patch operation

### Assistant

- provider credentials are not stored in plaintext BGS settings
- read tools remain bounded
- editor mutations require explicit semantic approval
- stale approvals fail closed
- model output never substitutes for BGS validation or save verification

### Cross-process writes

External MCP has no write authority. All model-requested writes in this slice stay inside the
desktop assistant, where BGS owns explicit UI approval, stale-state checks, editor undo, dirty
state, and the existing manual Save path.

## CI/proof rules

Source work must begin with proof of the existing behavior being reused or corrected.

Baseline BGS gates remain:

```text
locked restores
dotnet build app/BehaviourStudio.csproj --configuration Release --no-restore
dotnet run --project tools/symrm/symrm.csproj --configuration Release --no-restore -- test
dotnet test tools/tests/BehaviourGraph.Tests.csproj --configuration Release --no-restore
dotnet run --project tools/uismoke/uismoke.csproj --configuration Release --no-restore
```

Phase-specific gates add MCP protocol/path tests, deterministic assistant tests, or mutation approval/undo/save-reopen tests as authority expands.

## First source implementation stop point

The first implemented backend slice is intentionally small:

```text
AssistantInspection + typed models
focused core façade tests
path authorization tests
stdio MCP host
six bounded reads
one read-only clip-animation preview
IChatClient session/provider boundary
MCP discovery/protocol tests
full BGS CI-parity gates
```

The initial assistant drawer is now integrated around the existing shell. It opens closed, makes no
provider request until Send is pressed, shows the configured provider boundary, runs the bounded
in-process session, and presents explicit active-editor approval.

Do not mix this backend slice with:

- automatic Save
- animation frame editing
- character declaration writes
- cross-document repair

A small green backend slice plus the drawer lifecycle proof is the evidence required before expanding authority.

## Contract index

- `mcp-core-inspection-contract.md`: core façade, DTOs, error taxonomy, bounds
- `mcp-phase1-contract.md`: stdio host, path policy, protocol/CI
- `assistant-phase2-contract.md`: in-app `IChatClient`, context, drawer, session limits
- `assistant-phase3-first-write.md`: first approved active-document mutation
- `assistant-character-declaration-contract.md`: character bundle read/write boundary
- `animation-set-assistant-roadmap.md`: staged full animation-set capabilities and acceptance

These detailed contracts are authoritative where this overview is intentionally brief.
