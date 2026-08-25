# GARDEN-TOOLS code notes

Explanation and rationale that would otherwise clutter source comments, anchored to `file:symbol`.

## PluginEditTool/native/src/app/ServiceTargets.cpp:CellTarget::geometryBatch — processOne

Each unique model path is resolved and handed to `niftool geo` on a bounded pool of `std::jthread` workers, writing only its own result slot (so `results` needs no lock; only the work index and progress counters are atomic).

The worker pool uses `std::jthread`, not `std::thread`, and the spawn loop is wrapped in `try/catch (std::system_error)`. Constructing a `std::thread` can throw (resource exhaustion); if a later worker's construction threw while earlier workers were already joinable, stack unwinding would destroy those joinable `std::thread` objects and call `std::terminate`. `std::jthread` joins on destruction, so a mid-spawn throw unwinds cleanly; catching the `system_error` lets the workers that did start drain the queue. Because a partial spawn can leave some slots unprocessed (and a total spawn failure all of them), a final single-threaded sweep runs `processOne` on any result slot still null after the pool has joined — guaranteeing every path is resolved exactly once. `processOne` always writes a non-null object, so `is_null()` reliably marks an untouched slot.

`resolveNif()` and `nif_.geometry()` both touch the filesystem and can throw. A worker thread that lets an exception escape its function calls `std::terminate` and takes the whole native host down. So every per-item failure is caught inside `processOne` and recorded as that slot's `{ "error": ... }` entry — the batch always completes and returns partial results. This mirrors the C# path, where a propagated `Parallel.ForEach` exception is caught by `CellInterop.GetGeometryBatch` and returned as error JSON rather than crashing the process.

The niftool `geo` output is embedded as-is when it is a JSON object (mirrors the C# `JRaw` path); otherwise it is surfaced as an error entry. Unlike the C#, the parse is validated (`parse(..., nullptr, false)` + `is_discarded()`) so a malformed but `{`-leading output cannot corrupt the whole batch payload.

## Native write model — eager persistence (intentional divergence from the C# oracle)

The native backend is **write-through**: a record-edit RPC (`CreateMergedPatch`, `RevertOverrides`, `AddListItem`, `RemoveListItem`, the FormLink-list ops, …) mutates a copy of the plugin and calls `fo4::write_plugin_file` before returning. The C# oracle instead mutates an in-memory Mutagen mod and defers persistence to a later `SavePlugin`. This is a deliberate architectural difference in the port.

Because of it, native response strings must **not** claim a save is still pending. Where the C# message ends in "save_plugin to persist" / "then save it", the native message states the truth ("Written to disk.") rather than copying the oracle text for string parity. Load-order advice that is still accurate (e.g. "Ensure X loads last") is kept. `RevertOverrides` already embeds a real save result in both the oracle and the native port, so its message needed no change.

The proper long-term fix — give the native backend an in-memory dirty-state model and make `SavePlugin` the only thing that touches disk, restoring full oracle semantics — is tracked as separate future work, not done in these PRs.

## PluginEditTool/native/src/plugin/RadiclePluginOverridePatch.cpp:createMergedPatch — overwrite

`copy_record_as_override` is called with `overwrite=false`. The C# oracle routes `CreateMergedPatch` through `ResolveConflict` with its default `overwrite=false`, which refuses a record the patch already contains (the result is not "Copied", so it is counted as a failure, not a forward). With `overwrite=false` the native op leaves an existing patch record intact and reports it as "already present in the patch" instead of silently replacing it.

## PluginEditTool/native/src/plugin/RadiclePluginOverridePatch.cpp:revertOverrides — overwrite

`copy_record_as_override` is also called with `overwrite=false` here. The C# oracle reverts through `AddOverride`, which is `GetOrAddAsOverride`: if the patch already contains the record it returns the existing one (non-null, so counted as reverted) rather than replacing its contents. Native therefore counts both `inserted` (a fresh revert) and `already_exists` (the patch already had that FormKey) as a successful revert, and never overwrites the patch's own version with the prior plugin's. This only differs from a plain revert when the patch loads before the reverted plugin and already carries an override of the same record.

## PluginEditTool/native/src/plugin/RadiclePluginOverridePatch.cpp:revertOverrides — skipped accounting

The apply loop increments `skipped` for any copy result other than `inserted`/`already_exists` — chiefly `source_containment_unsupported`, which `copy_record_as_override` returns for a nested record (e.g. a REFR inside a CELL) the patch does not already contain. Without this, a record the dry run listed as revertible could fail to copy during apply and disappear from the result entirely, so `Reverted N` under-counted with nothing explaining the difference; it is now reported in the `skipped` total, matching the C# apply loop (which increments `skipped` on an `AddOverride` failure).

In `RadiclePluginBackend::revertOverrides`, the `prior == nullptr` guard is a defensive `continue` that is **not** counted in `skipped`: the C# oracle has no equivalent (it holds the prior record directly), and the "no prior version" case is already excluded earlier during candidate-finding (`versions.size() < 2`), mirroring C#'s separate `noPrior` counter which is likewise absent from the reported `skipped` total. In current native control flow this guard is effectively unreachable — the prior comes from the loaded version stack under the same lock the apply loop holds.
## PluginEditTool/native/src/plugin/RadiclePluginFormLinkList.cpp:addListItem — target validation

The C# oracle (`WriteService.AddListItem`) stores whatever FormKey it is given: it wraps the value in a `FormLink<T>` without checking that the target resolves in the loaded environment or that its record type matches the list's element type. To stay at oracle parity, native `addListItem` does **not** require the target to be a current `graph_.winner`, and does **not** run its own `formlink_list_target_allowed` check.

Target validation is instead left to the list mutation primitive (`insert_record_formlink_list_element` → `validate_record_and_target`), which admits any target of the schema's declared kind — and for a `wildcard_target` list such as `FLST\Items`, any record at all. The one residual, shared with every other native list op, is that the primitive rejects a *dangling local* FormKey (one owned by the editing plugin with no backing record), and for a typed (non-wildcard) list a local target of the wrong type; the oracle would accept those. This is deliberate: it is the library's shared safety behavior, not specific to AddListItem.

The `field` argument is matched to a list via `formlink_list_descriptor_by_field`, which is ASCII case-insensitive to mirror C#'s `BindingFlags.IgnoreCase` reflection lookup (`Items`, `items`, `ITEMS` are equivalent).

## RadiclePluginFormLinkList.cpp:addListItem / removeListItem — external-owner masters

`encode_plugin_form_id` throws when a FormKey's owner is neither the plugin itself nor one of its declared masters. Adding an external FormLink (e.g. `Something:OtherMod.esp` to a patch that does not yet master `OtherMod.esp`) would therefore fail. The C# oracle never hits this because Mutagen implicitly adds the master when it serializes the FormLink at save time.

So `addListItem` calls `ensureOwnerMaster(mutated, targetKey->owner())` before inserting: if the owner is missing from the master table it is appended (kind taken from the loaded owner plugin's TES4 small-file flag, else the `.esl` filename heuristic) via `remap_plugin_masters`, which re-encodes the plugin's existing FormIDs against the new table. Local targets (`owner == plugin.key`) and null owners are left untouched.

`removeListItem` does the inverse: it never adds a master. If the target's owner is not already the plugin or one of its masters, no LNAM in the plugin could possibly encode to it, so the value cannot be present — it short-circuits to `ListItemNotFound` ("No entry matching …") instead of letting `encode_plugin_form_id` throw. This matches C#, whose removal compares FormKeys directly and needs no master.
