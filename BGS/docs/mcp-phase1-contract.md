# BGS MCP Phase 1 contract

## Purpose

Phase 1 exposes a small MCP server over the transport-neutral BGS inspection service defined in
`mcp-core-inspection-contract.md`. The current backend remains read-only; it may expose a
side-effect-free clip preview for assistant diagnosis.

This document owns only MCP host, transport, path-authorization, packaging, and protocol concerns. It deliberately does **not** duplicate the core DTO/error definitions. `mcp-core-inspection-contract.md` is the source of truth for result shapes, limits, and domain error codes.

Any source implementation must follow the Ponytail proof-first/reuse-first workflow.

## Project shape

Add a dedicated console project:

```text
BGS/
  src/
    BehaviourGraph.Core.csproj
  tools/
    mcp/
      BehaviourGraph.Mcp.csproj
      Program.cs
      McpTools.cs
      McpPathPolicy.cs
      packages.lock.json
```

Requirements:

- target `net8.0`
- reference only `../../src/BehaviourGraph.Core.csproj`
- no reference to `app/BehaviourStudio.csproj`
- no Avalonia dependency
- add project to `BehaviourGraphStudio.slnx` under `/tools/`
- use locked restore
- pin a stable `ModelContextProtocol` package
- current verified stable line for this spike: `2.2.0`
- stdio transport only
- no ASP.NET/HTTP transport package in Phase 1

The core library must not reference the MCP SDK.

## Host command

Initial command:

```text
BehaviourGraph.Mcp --root <allowed-root> [--root <allowed-root> ...]
```

Rules:

- at least one root is required
- canonicalize roots at startup
- missing/unreadable roots are startup errors
- collapse duplicate roots using platform path comparison
- never silently allow the current working directory
- no write or mutation flag exists
- no TCP listener
- stdin/stdout are MCP protocol only
- human diagnostics/logging go to stderr

## Tool catalog

Expose these six read tools and one read-only preview tool:

```text
bgs.inspect_behavior
bgs.resolve_project_chain
bgs.check_project
bgs.search_project
bgs.inspect_animation
bgs.inspect_object
bgs.preview_set_clip_animation
```

Each MCP method should be a thin adapter:

```text
bind MCP arguments
  -> authorize/canonicalize target path
  -> call AssistantInspection
  -> return typed result
```

No HKX parsing, project interpretation, result bounding, validator logic, or animation-domain logic belongs in `McpTools.cs`.

The preview method authorizes the path first, then calls the side-effect-free preview operation.
No MCP method publishes a file.

## Core result contract

Use the exact typed models from `mcp-core-inspection-contract.md`.

Do not introduce a second MCP-only domain envelope with different status/error semantics.

The common domain fields are:

```text
status
code
message
warnings[]
truncated
```

Stable domain failures remain structured. Protocol/SDK failures may use normal MCP protocol errors where appropriate.

Do not return:

- stack traces
- exception type names
- environment values
- credentials
- unrelated absolute paths
- duplicate prose copies of typed data

## Model-context bounds

Use the bounds owned by the core inspection contract:

```text
DefaultListLimit = 50
HardListLimit = 200
MaxDisplayString = 500
```

Do not create separate limits in the MCP host unless the host is making them stricter for a specifically documented protocol reason.

Rules:

- summaries before detail
- counts remain available when lists are truncated
- no raw HKX XML by default
- no raw HKX bytes
- no complete project object graph
- no complete animation frame/transform arrays
- stable object IDs, classes, fields, and paths remain available for drill-down

The intended model flow is:

```text
summary -> bounded search -> exact object/animation inspection
```

## Path authorization

All file-taking tools pass through `McpPathPolicy` before `AssistantInspection` is called.

Required sequence:

1. reject blank path
2. canonicalize with platform semantics
3. require target to exist for inspection tools
4. prove target is under one configured root using path-component semantics
5. reject sibling/common-prefix tricks
6. fail closed on static symlink/junction/reparse-point components in Phase 1
7. pass the canonical authorized path onward
8. let existing `InputFilePolicy` retain HKX/NIF size/read consistency authority

Do not authorize one spelling and reopen a different untrusted spelling later.

### Phase 1 link policy

If an existing component between the allowed root and target is a symbolic link, junction, or reparse point, refusal is acceptable and preferred until real-path resolution is explicitly implemented and tested on Windows and Linux.

### Threat-model boundary

Phase 1 covers static traversal and link escapes. It does not claim protection against a hostile local process racing filesystem metadata between authorization and open.

That limitation must not be broadened into a claim that path isolation is generally race-safe.

## Tool mapping

### `bgs.inspect_behavior`

Calls:

```text
AssistantInspection.InspectBehavior
```

Important implementation note: do not use `HkxBehaviorParser.LastObjects` as shared cross-call state. If the core operation uses the parser internally, copy any needed data into the returned DTO inside the call.

### `bgs.resolve_project_chain`

Calls:

```text
AssistantInspection.ResolveProjectChain
```

A plain MCP call only reports the project/game-data context the host actually has. Do not claim archive resolution when no matching `GameData` context was supplied.

Before this tool is considered complete, add a focused committed-character-fixture regression proving that bundled animation declarations used by `ProjectChain.DeclaredAnimations` are visible. The repository already contains a pristine `RadRoachCharacter.hkx` fixture. See `assistant-character-declaration-contract.md`.

### `bgs.check_project`

Calls:

```text
AssistantInspection.CheckProject
```

Preserve graph findings, unreadable-file errors, and round-trip losses as distinct typed concepts defined by the core contract.

### `bgs.search_project`

Calls:

```text
AssistantInspection.SearchProject
```

`ProjectSearch.DefaultResultLimit` remains the existing human/general core default. The model-facing façade passes its own lower bound. Do not change the desktop/general default to solve model-context concerns.

### `bgs.inspect_animation`

Calls:

```text
AssistantInspection.InspectAnimation
```

No frame-transform payloads in Phase 1. A future frame/track inspection tool must require explicit bounded track/frame ranges.

### `bgs.inspect_object`

Calls:

```text
AssistantInspection.InspectObject
```

Return bounded field samples and both incoming/outgoing reference sites according to the core contract. Do not recursively serialize the reachable graph.

## Deliberately absent tools

The backend continues to register none of these:

```text
generic set field
add/remove object
rename/add/remove symbol
connect/disconnect nodes
paste subtree
template application
set animation frame
save document
save animation
copy/move/delete arbitrary file
run process/shell
read arbitrary non-BGS file
```

The existence of a desktop code path is not authorization to expose it through MCP.

## Protocol tests

Tests must exercise the actual registered MCP surface, not only direct C# methods.

Required proof:

```text
tool discovery lists six reads and one read-only preview tool
valid call returns structured typed content
bad path returns bounded structured refusal
bad object id does not crash process
no write-capable or process tool is registered; a caller-supplied approval value cannot publish a file
stdout contains protocol traffic only
stderr logging does not corrupt protocol
```

Prefer an SDK-supported in-process/stdio test harness where practical rather than a fragile custom JSON-RPC simulator.

## Path-policy tests

Required cases:

```text
exact root allowed
child file allowed
relative traversal rejected
sibling path with common string prefix rejected
missing target refused
case behavior matches OS
multiple roots work independently
Linux symlink escape refused
Windows junction/reparse escape refused
```

Platform-specific tests may skip only when the runner genuinely cannot create the relevant link primitive.

## Fixture proof

Use committed fixtures rather than a local Fallout 4 install.

Representative existing corpus includes:

```text
WeaponBehavior.hkx
SingleAnimFurniture.hkx
RadRoachCharacter.hkx
TurnLeft90.hkx
PairedKill2HMBashKneeAndHead_AttackerLead.hkx
skeleton.hkx
FemaleHair04.hkx
```

Phase 1 additionally needs the focused character declaration-read regression described above because no-op save fidelity alone does not prove declaration extraction.

## CI integration

The BGS lane must restore/build/test the MCP project on Linux and Windows in addition to existing gates.

Expected gate set:

```text
locked restore: MCP project
build MCP project --no-restore
existing app build
symrm native regression harness
xUnit suite
Avalonia headless UI smoke
MCP protocol/path tests
existing publish/identity checks
```

Do not make the first MCP change responsible for bundling the server into the desktop release package. Shipping can be a later separately proven change.

## Phase 1 acceptance

Phase 1 is complete only when:

1. an external MCP client can start `BehaviourGraph.Mcp` over stdio
2. six documented reads and the read-only preview tool are discoverable
3. every file-taking tool enforces allowed roots
4. traversal and static link escapes have regression coverage
5. output respects the shared bounds and exposes truncation
6. no raw XML/bytes/full animation frames are returned by default
7. no registered tool can publish a file or launch processes
8. all domain behavior comes from `AssistantInspection` / existing BGS core
9. representative committed fixtures prove behavior, character, animation, and unsupported cases
10. Linux and Windows BGS CI are green

Only then should Phase 2 add the in-app provider/chat UI layer.
