# BGS AI assistant and MCP integration

Status: architecture/proof proposal

This document defines a safe path for adding an in-app AI assistant and Model Context Protocol (MCP)
access to Behaviour Graph Studio without creating a second editing pipeline or letting a model bypass
BGS validation, undo/redo, save refusal, or format-safety rules.

## Goal

Make BGS usable both interactively and as a tool surface for AI models so a user can ask for help with
an entire Fallout 4 animation workflow, including graph inspection, project-chain analysis, missing
clip diagnosis, animation inspection, validation, guided fixes, preview, and explicitly approved edits.

The same BGS operations must power both:

- an Assistant workspace inside the desktop application; and
- external MCP clients that want to inspect BGS.

The AI layer is an orchestrator over BGS. It is not a new HKX parser, writer, validator, or graph
editor.

## Trace: existing production seams

The current application already contains the operations an assistant should compose:

| Existing surface | Evidence | AI/MCP use |
| --- | --- | --- |
| Desktop shell | `app/EditorShell.cs` owns the activity rail and workspace navigation | Add an Assistant activity without replacing the shell |
| Active document state | `app/MainWindow.cs` owns the loaded path, parsed objects, project chain, read-only state and selection | Build a bounded context snapshot |
| Graph navigation | `app/MainWindow.GraphApi.cs` exposes selection, canvas focus and diagnostics UI state | Inspect and focus objects without reimplementing graph traversal in the chat layer |
| Validation | `MainWindow` already routes Check graph and Check project through production validation | Expose validation as tools and return structured findings |
| Project analysis | `ProjectAnalysisController` is created from the current document stamp | Reuse stale-result protection for assistant work |
| Editing lifecycle | `MainWindow` owns field commits, dirty state, undo, redo and save | All model-requested edits must enter this lifecycle |
| Save safety | BGS refuses unsupported writes and backs up supported saves | Never add an AI-only write path |
| Headless scans | BGS already supports archive, modlist and clip scans with JSON output | Reuse for whole-mod/load-order diagnosis |
| UI regression path | `tools/uismoke` drives the production `MainWindow` | Add assistant UI coverage here instead of a disconnected test shell |

The application project targets .NET 8 and Avalonia. It currently has no MCP or model-provider package
references in `app/BehaviourStudio.csproj`. The first implementation should therefore add integration
at a new boundary rather than spreading provider code through `MainWindow` partials.

## Architecture

Use one typed operation layer shared by the desktop assistant and MCP.

```text
AI provider / external MCP client
              |
              v
       Assistant orchestration
              |
              v
        BgsToolRegistry
              |
      +-------+-------+
      |               |
 Read-only tools   Edit commands
      |               |
      +-------+-------+
              |
              v
 Existing BGS document, graph, validation,
 project-analysis, undo/redo and save paths
```

### 1. BgsToolRegistry

Create a small provider-neutral registry that describes each BGS capability with:

- stable tool name;
- short description;
- JSON-serializable input and output records;
- read-only or mutating classification;
- whether user approval is required;
- an execution delegate bound to the current BGS session.

The registry is the contract. MCP and the in-app assistant are adapters over it.

Do not put MCP annotations on `MainWindow` methods. Do not make provider-specific model types part of
core BGS APIs.

### 2. BgsSessionContext

Expose only a bounded snapshot of the current editor state. Initial fields should include:

- current document name and path when disclosure is allowed;
- read-only/dirty state;
- current project/character/behaviour chain summary;
- selected object ID, class and relevant editable fields;
- graph validation findings;
- current animation/clip summary;
- current activity/tab;
- recent operation result.

Do not send the entire HKX/XML representation to a model by default. Context expansion should be
explicit and tool-driven.

### 3. In-app Assistant workspace

Add `Assistant` to `EditorShell` as a first-class activity. Keep UI code in focused files rather than
growing `MainWindow.cs`.

Suggested workspace layout:

- conversation transcript;
- composer with Send/Stop;
- model/provider selector;
- context chips for Document, Selection, Diagnostics, Animation and Project;
- collapsible tool-call history showing inputs, outputs and approval state;
- explicit Approve/Reject controls for mutating operations;
- actions for Clear conversation and Copy diagnostic bundle.

The assistant should answer ordinary help questions with no loaded file. With a document loaded it can
attach only the context the user selected or that a tool call requested.

### 4. Model provider abstraction

Use a provider-neutral chat interface. `Microsoft.Extensions.AI.IChatClient` is a good fit for .NET 8
because it separates BGS from one vendor SDK and supports multiple model backends.

Initial provider targets:

1. OpenAI-compatible HTTP endpoint for hosted or local servers.
2. Ollama/local models.
3. Provider-specific adapters only where a capability cannot be represented cleanly through the
   neutral interface.

Secrets must not be written to ordinary BGS settings JSON. Prefer environment variables first, then
an OS credential store if persistent secret storage is added later. UI settings may store non-secret
values such as endpoint URL, model name and provider choice.

### 5. MCP transport

Prefer the official `ModelContextProtocol` C# SDK rather than implementing JSON-RPC/MCP by hand. At
the time of this trace (2026-09-15), the official stable package is 2.2.0 and targets .NET 8. Verify
the current stable package before pinning it in the lock file.

Start with an opt-in local server bound only to loopback. Do not expose BGS MCP on LAN interfaces by
default.

Recommended transport sequence:

1. Streamable HTTP/loopback host for a running BGS instance so external clients can operate on the
   active editor session.
2. Add a small stdio bridge later if clients require process-launched stdio. The bridge should proxy
   to the running BGS instance rather than create a separate hidden editor state.

The server must generate a per-session bearer token or equivalent local capability and surface the
connection details in the Assistant/MCP settings UI. A random port is preferable to a fixed globally
known port.

## Initial MCP tool contract

Phase 1 should be deliberately read-heavy.

### Workspace and help

- `bgs.get_workspace_context`
- `bgs.get_document_summary`
- `bgs.get_selection`
- `bgs.get_help`
- `bgs.get_capabilities`

### Graph and project diagnosis

- `bgs.inspect_object`
- `bgs.search_project`
- `bgs.validate_graph`
- `bgs.validate_project`
- `bgs.get_diagnostics`
- `bgs.list_project_chain`
- `bgs.list_clips`
- `bgs.find_missing_animations`

### Animation inspection

- `bgs.get_animation_summary`
- `bgs.get_animation_annotations`
- `bgs.get_skeleton_summary`
- `bgs.get_playback_state`

### Mutating tools after the read-only contract is proven

- `bgs.set_field`
- `bgs.rename_symbol`
- `bgs.relink_reference`
- `bgs.add_transition`
- `bgs.apply_template`
- `bgs.edit_animation_frame`
- `bgs.undo`
- `bgs.redo`

Saving should remain a separate, explicit user action initially. Do not expose `bgs.save` until edit
approval, transaction rollback and post-edit validation have dedicated regression coverage.

## Full animation-set workflow

A model should be able to orchestrate the following without inventing game data:

1. Read the active project/character/behaviour chain.
2. Enumerate clips and referenced animation files.
3. Run existing graph/project/load-order validation.
4. Explain findings with the affected object/file IDs.
5. Inspect the selected clip, animation, skeleton and annotations as requested.
6. Propose a bounded edit plan.
7. Ask for approval for each mutating batch.
8. Apply changes through existing BGS edit commands and transaction/undo state.
9. Re-run validation and show the before/after findings.
10. Preview the affected animation or graph state when BGS supports it.
11. Leave final save under explicit user control until the save tool is separately proven.

This gives the chatbox enough capability to help with an entire animation set while preserving BGS's
fail-closed format policy.

## Approval and safety model

Classify every tool as one of:

- `ReadOnly`: may run without confirmation.
- `EditorMutation`: changes in-memory BGS state and requires explicit approval unless the user enabled
  a session-scoped auto-approve policy for that exact tool class.
- `PersistentMutation`: writes files or external state and always requires explicit approval in the
  first implementation.

Additional boundaries:

- no arbitrary shell/PowerShell execution;
- no arbitrary filesystem read tool;
- no path outside the loaded document/project roots unless the user selected it;
- no direct XML/HKX write bypass;
- no silent save;
- no network bind beyond loopback by default;
- redact configured API keys/tokens from logs, transcript exports and MCP errors;
- tool results must carry document stamps so a stale model plan cannot mutate a newer document.

## Issue-help workflow

For "why is this broken?" requests, the assistant should collect evidence rather than guess:

1. current document/selection;
2. graph/project diagnostics;
3. relevant project-chain and clip existence checks;
4. bounded object details;
5. BGS help/documentation entries relevant to the finding.

The answer should distinguish observed data, BGS validation output and model inference. If BGS does not
have enough evidence for a Havok/runtime claim, the assistant should say so instead of generating a
plausible-looking fix.

Runtime/reverse-engineering claims are out of scope for the first MCP layer. If a later tool relies on
Fallout 4 runtime layout or engine behavior, verify that claim against the project's measured reverse-
engineering evidence, including `PRISMA-USER-INTERFACE-FRAMEWORK/commonlib_NonVR`, before exposing a
mutation based on it.

## Implementation sequence

### PR A: foundation

- add `BgsToolRegistry`, tool metadata and bounded context records;
- add read-only tools for workspace/document/selection/validation;
- add unit tests for schema stability and stale-document refusal;
- no network transport and no model SDK yet.

Proof gate: xUnit suite plus focused tests for each tool.

### PR B: Assistant workspace

- add the Assistant activity and chat UI;
- add provider-neutral chat service;
- support OpenAI-compatible and local model configuration;
- surface tool calls and context chips;
- keep tools read-only.

Proof gate: production-path `uismoke`, xUnit, `symrm`, Release build.

### PR C: MCP server

- add official C# MCP SDK;
- adapt `BgsToolRegistry` to MCP tools/resources;
- loopback-only transport, random port and per-session token;
- connection UI and deterministic shutdown.

Proof gate: MCP protocol/integration tests plus the full BGS CI parity surface.

### PR D: approved editing

- expose a small set of editor mutations through existing BGS edit/undo paths;
- preview tool inputs and resulting diff before approval;
- attach document stamp to every proposed mutation;
- auto-run validation after every approved batch;
- prove undo returns the exact pre-edit state.

Proof gate: transaction tests, UI approval smoke, save-fidelity regressions and full CI parity.

### PR E: animation workflow expansion

- expose animation-frame, annotation, template and project-level helpers only where existing BGS
  production operations can be reused safely;
- add higher-level assistant recipes for diagnose/fix/validate/preview flows.

Each new mutation requires a focused before/after test. Unsupported operations must remain refused.

## Testing requirements

Any runtime implementation should use BGS's existing three-part surface:

- `tools/tests` for tool contracts, context serialization, transactions and provider-independent
  orchestration;
- `tools/uismoke` for Assistant workspace and approval behavior through the production `MainWindow`;
- `tools/symrm` when a feature touches native/Havok behavior.

MCP tests should additionally prove:

- tool discovery exposes only intended tools;
- malformed JSON inputs fail without mutating editor state;
- stale document stamps are rejected;
- read-only tools cannot dirty the document;
- mutating tools cannot execute without approval;
- disconnect/reconnect does not leak a token or stale session;
- server shutdown releases the port and leaves BGS responsive.

## Non-goals for the first implementation

- autonomous saving or bulk rewriting of HKX files;
- arbitrary code/shell execution;
- exposing every internal `MainWindow` method as a tool;
- sending full project contents to a remote model by default;
- claiming Fallout 4 runtime behavior from static BGS data;
- replacing BGS validation with model judgment.

## Dependency references

Verify versions at implementation time and update BGS lock files intentionally.

- Official MCP C# SDK: https://github.com/modelcontextprotocol/csharp-sdk
- MCP NuGet profile: https://www.nuget.org/profiles/ModelContextProtocol
- Microsoft.Extensions.AI: https://learn.microsoft.com/dotnet/ai/

## Decision

Proceed with the shared `BgsToolRegistry` architecture. Build the in-app Assistant and read-only
external MCP adapter on the same typed BGS operations. Keep model-driven mutations inside the
desktop approval boundary.
