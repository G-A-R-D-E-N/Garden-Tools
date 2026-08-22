# FO4IDE: Concurrency Model & Audit

How this tool stays safe while multiple callers touch the same plugin graphs and the same
environment. The editor started as a desktop single-caller app and grew HTTP RPC, an MCP server, an
AI-chat surface, and background workers, so ownership boundaries that used to be implicit
("one user, one thread at a time") became races. This document describes the three synchronization
layers that replaced those assumptions, what each one guarantees, what is deliberately **not**
synchronized, and the contracts contributors must keep.

The layers all live in **Core** (`FO4RecordEditor.Core/Services/`), so the WPF shell, the Linux
server, and the MCP process share exactly the same enforcement. None of them is a global RPC
semaphore: plugins stay independent of each other, and the environment has its own lifecycle
serialization separate from per-plugin graph access.

---

## The three layers at a glance

| Layer | Type | What it serializes | Key files |
|---|---|---|---|
| 1. Per-plugin graph gate | `PluginGraphGate` | Mutations, traversals, and serialization of **one plugin's live Mutagen graph** at a time | `PluginGraphGate.cs`, `WriteService*.cs`, `ElementService.cs`, `CellService.cs`, `MutagenLoader.cs`, `ConflictScanner.cs` |
| 2. Environment lifecycle gate + coordinator | `EnvironmentLifecycleGate`, `EnvironmentLifecycleCoordinator` | **Whole-environment rebuilds** (load order, LinkCache, masters, asset roots) | `EnvironmentLifecycleGate.cs`, `EnvironmentLifecycleCoordinator.cs`, `MutagenLoader.cs` (builders), `Mo2ProfileLoader.cs`, `ShellViewModel.cs` |
| 3. Atomic environment snapshot | `EnvironmentSnapshot`, `EnvironmentState` | Readers of environment state never see a **mixed generation** | `EnvironmentSnapshot.cs`, `EnvironmentState.cs`, `MutagenLoader.cs` (accessors) |

Layer 1 is the original #183 fix plus its reader-sweep follow-up; layers 2 and 3 came out of the
same audit. They were developed on separate branches (`fix/write-plugin-graph-lock`,
`fix/environment-lifecycle-gate`, `fix/graph-reader-snapshot`, `fix/environment-snapshot`) and
describe the model as it exists when merged.

---

## Layer 1 — the per-plugin graph gate

### The problem

A `Fallout4Mod` instance (and the `ModIndex` derived from it) is not safe to mutate, enumerate, and
serialize at the same time. GUI actions, HTTP RPC calls, MCP tools, and the background
`ShellViewModel` tree refresh could all reach the same live graph. A global lock was rejected
because different plugins are genuinely independent — locking them all behind one object would
serialize unrelated work.

### The mechanism

`PluginGraphGate` owns **one lock object per canonical plugin filename**:

- The registry is a `ConcurrentDictionary<string, object>` with `OrdinalIgnoreCase`, so
  `"MyMod.esp"` and `"c:\mods\myMod.esp"` are the same lock. `KeyOf` collapses rooted paths to the
  bare filename.
- Entries are created with `GetOrAdd` and **never evicted** (ABA-safe: a plugin can be discarded and
  re-created, but the lock identity never changes).
- The gate is Monitor-based, so it is **reentrant** — a gated operation may call another gated
  operation on the same or a different plugin and compose correctly.

API:

```csharp
PluginGraphGate.Run(plugin, () => /* whole operation, lock held */);
PluginGraphGate.Run(new[] { source, patch }, () => /* both locks, sorted order */);
PluginGraphGate.RunEditableSnapshot(
    () => EditableMods.Keys.Concat(LooseMods.Keys), keys => /* captured keys only */);
```

`Acquire(IEnumerable)` canonicalizes, dedupes case-insensitively, **sorts**, and acquires in order
(releasing in reverse). Any multi-plugin operation — a `[source, patch]` copy, a scan-all — is
therefore deadlock-free against single-plugin operations by construction. Single-plugin operations
hold exactly one lock and never wait on another while holding it.

### What is inside the gate

| Consumer | Coverage |
|---|---|
| `WriteService` + its partials | Every public entry point (58 in the sweep): record create/set/delete, condition/component editing, script properties, `CopyAsOverride`/`CopyAsOverrideMany`, `RevertOverridesFrom`, `SavePlugin` and the other three `WriteToBinary` serialization sites, `ReloadPlugin`/`DiscardScriptPatch`/lifecycle |
| `ElementService` | The four direct `GetMutableFor` list mutations (add/remove/move/clear) |
| `CellService` | The `CleanupPlacedReferencesJson` mutation span |
| `MutagenLoader` | Traversal entry points (`GetGroups`, `GetRecords`, `PopulateNode`, `Query*`, `Search*`, `CheckPlugin`, `Scan*`) and, in the reader sweep, the indirect live-graph readers: `GetRecordContexts` (the hub), `GetReferencedBy`, `SearchCellRecords`, `SearchWorldspaceRecords`, `ResolveEditorIdToFormKey`, `ResolveId`, `GetRecordProblems`, `PopulateNodeAllVersions`, `BuildConflictMatrix` (its `WalkObject` runs on contexts records, so it cannot just inherit). `GetConflictingPlugins` inherits through `GetRecordContexts`. |
| `ConflictScanner` | `Scan`, which swaps live editable graphs into the load order while it runs |

Serialization follows the **hold-or-snapshot rule**: `SavePlugin`'s full `WriteToBinary` traversal
stays inside the gate rather than being released mid-serialization. Notifications fire inside the
gated body; the `ShellViewModel` handler is fully async, so it never blocks the gate.

### Captured-set scans (`RunEditableSnapshot`)

Readers that enumerate the *whole* editable set use a point-in-time capture, never the live
registry:

1. **Snapshot** the editable+loose key set (before taking any lock).
2. **Acquire** that captured set in canonical sorted order.
3. Resolve/traverse **only the captured keys** while held.
4. Plugins registered after the snapshot are invisible to this scan until the next one.

Keys are never resolved to mod objects before acquisition — a lifecycle replacement cannot swap a
graph out from under the locking.

### The raw escape hatches (transitional)

`GetMutable`, `GetMutableFor`, and `ResolveModPublic` still hand out a live mod object with no lock.
They are the documented escape hatches the audit accepted; every public consumer is wrapped instead.
The long-term target is an operation-shaped API:

```csharp
PluginGraphGate.Run(plugin, mod => { /* complete graph operation */ });
```

so lock lifetime and graph lifetime are the same lexical operation and future audits have nothing to
hunt for.

---

## Layer 2 — the environment lifecycle gate + coordinator

### The problem

Rebuilding the environment mutates shared statics incrementally — `LinkCache`, `MasterIsEsl`,
`PluginSourcePaths`, the mod-index cache, and the asset roots. Two concurrent rebuilds could
interleave those writes. The old guard was `ShellViewModel._isLoadingEsp`, a plain boolean with a
read-then-write race and no coverage for callers outside the shell.

### The mechanism

`EnvironmentLifecycleGate` is a Core-owned `SemaphoreSlim(1, 1)` with sync (`Run`) and async
(`RunAsync`) helpers. The `SemaphoreSlim` is **not reentrant**, so the contract is strict: the
private `*Core` methods never acquire the gate, and the internal call graph has no
gated-env → gated-env nesting (the four builders are standalone). The sync variants block and are
for startup/test paths only.

Every public environment rebuild entry point funnels through the gate, sync **and** async:

| Public (sync / async) | Private core |
|---|---|
| `BuildEnvironment` / `BuildEnvironmentAsync` | `BuildEnvironmentCore` |
| `LoadEnvironment` / `LoadEnvironmentAsync` | `LoadEnvironmentCore` |
| `LoadEsp` / `LoadEspAsync` | `LoadEspCore` |
| `Mo2ProfileLoader.Load` / `LoadAsync` | `LoadCore` |

`LoadEsp` additionally takes the **per-plugin** gate on `Path.GetFileName(espPath)`, because
`ReplaceLooseMod` writes the `LooseMods` store that graph readers enumerate — the same store
`WriteService.Register` writes under the plugin gate. `Mo2ProfileLoader.TryReplaceLoadedPluginFile`
(the audit's last ungated environment writer — it mutates the live MO2 load order and the static
`LinkCache`) also runs under the gate.

### The shell coordinator

`ShellViewModel` no longer owns any correctness lock. It holds one
`EnvironmentLifecycleCoordinator` with a single slot:

- **Identical in-flight request** (same operation key, same parameters) → returns the **same**
  `Task`; the work runs once.
- **Different request** while one is running → starts a new task; the Core gate serializes it (the
  old behavior silently skipped it).
- **Completed slot** → the next request starts fresh.

The coordinator lives in Core so it is unit-testable in the Linux CI (the WPF shell is not).

### Off-to-the-side builds

Each builder computes its replacement state (`masterIsEsl`, `pluginSourcePaths`, asset roots) into
**locals** and touches shared statics only in one final publish block. A failed build leaves the
previous generation intact. Readers holding the old environment object are untouched — the
`envObj`/`GameEnvironment` reference is an immutable snapshot by design.

---

## Layer 3 — the atomic environment snapshot

### The problem

Layer 2 minimized the partial-state window but the publish block still cleared and refilled the
statics in place, so an ungated reader could observe a **mixed generation** (new `LinkCache`, old
`MasterIsEsl`).

### The mechanism

`EnvironmentSnapshot` is an immutable value carrying all four fields — `LinkCache`, `MasterIsEsl`,
`PluginSourcePaths`, `AssetRoots` — copied at construction. `EnvironmentState` holds it behind a
single `volatile` reference; `Publish` is a one-instruction swap. A reader therefore captures one
complete generation, never a blend. Writers stay under the environment gate; readers never take it.

- `ApplyEnvironmentPublish(linkCache, masterIsEsl, pluginSourcePaths, assetRoots)` clears the
  self-validating mod-index cache, publishes the snapshot, then applies the canonical roots to
  `TextureService` (itself a single immutable-array swap). `AssetResolver` keeps its caller-side
  derived roots (MO2's differ from the canonical set).
- `MutagenLoader.LinkCache` / `MasterIsEsl` / `PluginSourcePaths` are now **read-only compat
  accessors** backed by `EnvironmentState.Current` (empty-dictionary fallbacks preserve the
  pre-environment behavior), so all existing read sites are unchanged. There are no setters: the
  only sanctioned writers are the publish block and `PublishLinkCache`.
- `Mo2ProfileLoader.RebuildLinkCache` (the targeted refresh after `TryReplaceLoadedPluginFile`)
  publishes a `WithLinkCache` forward-carry snapshot, so the new `LinkCache` keeps the current
  `MasterIsEsl`/`PluginSourcePaths` generation instead of stranding them.
- Tests use `SeedEnvironmentForTest(...)` instead of writing the statics.

### Reader contract

A reader that needs one stable generation across **multiple** lookups must capture the snapshot
once:

```csharp
var gen = EnvironmentState.Current;
// read only through gen for the whole operation
```

Two separate reads can legally observe two different generations — that is not a bug.

---

## Lock ordering & deadlock analysis

- **Environment gate → plugin gate** is the only cross-layer order (via `LoadEsp`). Plugin-gated
  operations never call environment-gated operations, so there is no cycle.
- Multi-plugin acquisition is **sorted**; single-plugin operations hold exactly one lock, so no
  ABBA cycle is possible. Scan-alls acquire every editable+loose key in sorted order.
- The plugin gate's Monitor is reentrant, so nested paths (`CopyAsOverride → ResolveConflict`,
  gated `WriteService` → `MutagenLoader.GetGroups`, `GetRecordVersionCore → GetRecordContexts`)
  compose without deadlock.
- No lock is held across LLM/chat/network lifetimes. Disk serialization follows the
  hold-or-snapshot rule: hold the per-plugin lock for the whole `WriteToBinary` traversal.

## What is intentionally not synchronized

- **Per-plugin stores** (`EditableMods`, `LooseMods`, `LooseModPaths`, `_mutable`, `_sourcePath`)
  are concurrent dictionaries; their *contents* are protected by the plugin gate (every writer is
  gated — `Register` under the plugin gate, `LoadEsp` under both gates).
- **The mod-index cache** (`_modIndexCache`) is self-validating: every hit is checked with
  `ReferenceEquals(cached.Source, mod)`, so a stale entry from a previous generation is detected and
  rebuilt rather than trusted. It is cleared at each publish for hygiene.
- **The environment object itself** (`envObj` / `GameEnvironment`) is an immutable snapshot; rebuilds
  create new objects rather than mutating the old one.
- **Front-end correctness lint** (issue #149) is a separate workstream, not part of this model.

## Test inventory

| Layer | Deterministic coverage |
|---|---|
| Gate | Canonical identity across case/path spellings; per-plugin independence via handshakes (B enters while A holds, C blocked on A); **forced-overlap ABBA** (first holds `[a]`, second acquires `[b, a]` — the holder must never deadlock); reentrancy |
| Gate integration | Same-plugin concurrent mutations; mutation vs save with file round-trip; mutation vs `GetGroups` traversal; mutation vs `ReloadPlugin` |
| Reader sweep | Mutation vs `GetRecordContexts`/`GetReferencedBy`/searches/`GetRecordProblems`/resolve pair against a fake env double; **registration-during-scan** (a plugin registered while the scan waits must be absent from the captured set) |
| Env gate | Writer handshake (two writers never overlap); coordinator share/queue/fresh-slot semantics; publish-block consistency; `LoadEsp` blocked by a held gate |
| Snapshot | **Generation-consistency race** (readers must always see matching keys and roots across all four fields while writers hammer alternating snapshots); targeted-swap forward-carry; empty accessor behavior |

## Contracts for contributors

1. Never write `LinkCache`, `MasterIsEsl`, or `PluginSourcePaths` directly — publish a snapshot or
   use `SeedEnvironmentForTest` in tests.
2. `*Core` methods never acquire the environment gate (the semaphore is not reentrant) and never
   call a public gated environment operation.
3. Sync environment variants block and are for startup/test paths only; shell and server callers
   use the async variants.
4. A new reader of the editable set should go through `PluginGraphGate.RunEditableSnapshot` (or a
   single-plugin `Run`), never enumerate `EditableMods`/`LooseMods` directly.
5. A new environment writer must be added to the gate's entry-point table above.
6. Don't hold any gate across LLM/chat/network I/O.
