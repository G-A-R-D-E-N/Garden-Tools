# Papyrus VM, Quests, and Scripting RE Roadmap

**Targets:** `Fallout4.exe` 1.10.163 (OG) and 1.11.240 (AE)

**Scope:** the native `BSScript` VM, compiled-script loading/linking, native-function registration, task/stack execution, Papyrus persistence, and the quest/alias/story-manager bridges that expose game state to scripts.

This is a runtime workstream. It is separate from the Creation Kit Papyrus source-zip and batch-export findings, although the CK findings provide useful names and file-path anchors.

## Current evidence

The OG inventory already contains approximately 7,938 Papyrus-tagged functions and 6,064 quest/alias/dialogue/story-manager functions. The source-path dump identifies the core `bsscript` implementation files, including:

- `bsscriptinternalvirtualmachine.cpp`
- `bsscriptivirtualmachine.cpp`
- `bsscriptcompiledscriptloader.cpp`
- `bsscriptlinkerprocessor.cpp`
- `bsscriptinternalvdesctable.cpp`
- `bsscriptbytecode.cpp`
- `bsscriptstack.cpp`, `bsscriptobject.cpp`, and `bsscriptvariable.cpp`
- `bsscriptobjectbindpolicy.cpp` and `bsscriptobjecttypeinfo.cpp`

Already decompiled quest-side anchors include `TESQuest::SetStage`, both `TESQuestStage::SetDone` paths, `TESQuestStageItem::Resolve`, alias lookup/binding, `BGSStoryManagerQuestFinder`, cooldown handling, and the Papyrus `Quest.SetCurrentStageID` bridge. The CK-side Papyrus source ZIP extraction path and its Wine failure are documented separately in `dumps/decompile-papyrus-zip-array-population.md` and `docs/20-WINE-PAPYRUS-ZIP-CRASH.md`.

These facts are starting anchors, not proof that every CommonLib name or offset applies to AE. Every address and layout must be version-qualified.

## Phase P0 — Evidence and version control

1. Freeze the OG and AE Ghidra program identities, image bases, section maps, and symbol-import status.
2. Build a name/address inventory for `BSScript::*`, `GameScript::*`, `CSScript::*`, `TESQuest*`, aliases, and story-manager classes.
3. Record function RVAs, callers, vtable slots, source-file strings, and confidence (`confirmed`, `strong`, `inferred`).
4. Diff every high-value VM function against AE before publishing a field offset or hook signature.

**Deliverable:** `Runtime/Organized/1.10.163/README-Papyrus-VM.md` plus an OG/AE target table.

## Phase P1 — VM object model and execution core

Start with the functions that define one VM execution step and the state surrounding it:

1. `BSScript::Internal::VirtualMachine` constructor, initialization, shutdown, and update/tick entry.
2. `IAVirtualMachine`/`IVirtualMachine` interface implementations and the task-queue boundary.
3. `Internal::ScriptFunction`, `CodeTasklet`, `InstructionStream`, and `PackedInstructionStream` dispatch.
4. `Stack`, `Variable`, `Object`, `Struct`, and array/reference ownership operations.
5. `MemoryPagePolicy` allocation and the object/stack lifetime paths.
6. Error logging, runaway/instruction-budget handling, latent-function suspension, and resume.

**Questions to answer:**

- What is the exact bytecode instruction format and program-counter advance rule?
- Where are arguments, locals, return values, and temporary variables stored?
- How does a latent call become a suspended task and later resume?
- Which queue/thread owns VM execution, and what synchronization protects object state?
- What error or budget condition terminates a stack, an object, or the whole task?

**Deliverable:** VM state/layout table, instruction dispatch map, and a minimal pseudocode model of one tick.

## Phase P2 — Compiled script loading and linking

Trace the complete path from a script name on disk to a runnable linked object:

1. `CompiledScriptLoader` file lookup, cache lookup, and failure behavior.
2. `UnlinkedTypes` and `LinkerProcessor` type resolution.
3. `ObjectTypeInfo`, `PropertyTypeInfo`, `StructTypeInfo`, and `Struct` resolution.
4. Function/property tables, native versus script functions, and vdesc/vtable construction.
5. Import dependency ordering and the behavior for missing, stale, or incompatible `.pex` files.
6. `ObjectBindPolicy` and the mapping from a Papyrus object to a native `TESForm`/handle.

**Questions to answer:**

- Which file roots and archive layers are searched, and in what order?
- Are scripts linked lazily per object, per type, or in a global load pass?
- How are native functions matched to script declarations?
- What exact conditions produce "cannot bind", "cannot link", or "unknown type" errors?
- Which metadata is persisted in a save and rebuilt on load?

**Deliverable:** `.pex` loader/linker flow, type-table layout, and failure matrix.

## Phase P3 — Native registration and call bridges

Map the registration path that turns a C++ function into a Papyrus callable:

1. `BSScript::NativeFunction*` and `NativeLatentFunction*` construction.
2. Class/function registration tables and duplicate-name handling.
3. `GameScript::*` registration for `Quest`, `ObjectReference`, `Actor`, `Utility`, and global helpers.
4. Argument marshalling, `Variable` conversion, handles, arrays, and return-value ownership.
5. Latent native callbacks and completion/event dispatch.

Use the already confirmed `GameScript::Quest_SetCurrentStageID` bridge as the first end-to-end sample, then expand to `WaitForQuestStage`, `WaitForQuestStart`, alias access, and reference activation.

**Deliverable:** native registration table schema and a C++-like signature table for the first 50 high-value natives.

## Phase P4 — Quest, alias, and story-manager runtime

Extend the existing quest notes from individual functions into a state-machine model:

1. Quest start/stop/complete/fail transitions and event broadcasts.
2. Stage and log-entry processing, including `OnStageSet`-adjacent fragment dispatch.
3. Alias definition versus runtime binding arrays, fill/clear/revert behavior, and handle invalidation.
4. Quest targets, objectives, instance data, and save/load restoration.
5. Story Manager event intake, branch traversal, cooldowns, selection, and waiting-stage queues.
6. Scene/dialogue action dispatch and the Papyrus event path for scenes and topics.

**Deliverable:** quest lifecycle diagram, alias-binding layout, event-to-Papyrus callback table, and save/load notes.

## Phase P5 — Persistence, scheduling, and diagnostics

1. Locate the VM save/load interface and identify serialized script/object/stack records.
2. Trace task scheduling, per-frame budgets, delayed calls, latent waits, and cancellation.
3. Map error logger categories and the exact conditions that produce stack dumps.
4. Build small offline parsers for any recoverable `.pex`, save-script, or debug-log structures.
5. Add cross-version assertions for fields used by plugins.

**Deliverable:** persistence format notes, scheduler model, and a diagnostic checklist for stuck or runaway scripts.

## First decompile queue

| Priority | Target | Why |
|---|---|---|
| 1 | `BSScript::Internal::VirtualMachine` update/tick | establishes the execution loop and budget boundary |
| 2 | `BSScript::Internal::CodeTasklet` execute/resume | pins instruction dispatch and latent suspension |
| 3 | `BSScript::CompiledScriptLoader` load entry | establishes file lookup/cache/failure behavior |
| 4 | `BSScript::LinkerProcessor` link entry | pins type/function/property resolution |
| 5 | `BSScript::ObjectBindPolicy` bind/unbind | connects VM objects to native game forms |
| 6 | `BSScript::NativeFunction` invoke path | establishes argument and return marshalling |
| 7 | `GameScript::QuestMgrInternal::WaitForQuestStage` | first latent quest bridge |
| 8 | `TESQuest::FillAliases` and alias clear/revert paths | completes the runtime alias lifecycle |
| 9 | VM save/load interface implementation | identifies persisted script state |
| 10 | error logger and runaway-script termination | gives the practical failure matrix |

## Method and guardrails

- Decompile from the real Ghidra program and retain the function RVA plus version in every finding.
- Prefer call-pattern and vtable evidence over a symbol name alone.
- Use source-file strings only as ownership anchors; they do not prove the function boundary.
- Do not infer a field's semantic name from one read. Confirm with a constructor, setter, serializer, and at least one caller.
- Treat OG 1.10.163 and AE 1.11.240 as separate layouts until a structural diff proves otherwise.
- Keep binaries, Ghidra databases, raw PDB dumps, and decompiled function bodies local. Publish only derived facts, small tables, and reproducible scripts.

## Success milestones

- **P1:** explain one VM tick and one latent resume without hand-waving.
- **P2:** load a real `.pex` and document its native/linker failure paths.
- **P3:** produce a verified native registration map for quest and reference APIs.
- **P4:** explain a quest stage transition from native call through alias/event dispatch.
- **P5:** identify which Papyrus state survives save/load and why scripts become stuck or terminate.

The practical end state is a versioned, evidence-backed model that can support CommonLib/F4SE hooks, Papyrus diagnostics, quest tooling, and eventually an offline script/plugin inspection tool without requiring the CK to run under Wine.
