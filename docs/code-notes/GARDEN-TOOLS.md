# GARDEN-TOOLS code notes

Explanation and rationale that would otherwise clutter source comments, anchored to `file:symbol`.

## PluginEditTool/native/src/app/ServiceTargets.cpp:CellTarget::geometryBatch — processOne

Each unique model path is resolved and handed to `niftool geo` on a bounded pool of `std::jthread` workers, writing only its own result slot (so `results` needs no lock; only the work index and progress counters are atomic).

The worker pool uses `std::jthread`, not `std::thread`, and the spawn loop is wrapped in `try/catch (std::system_error)`. Constructing a `std::thread` can throw (resource exhaustion); if a later worker's construction threw while earlier workers were already joinable, stack unwinding would destroy those joinable `std::thread` objects and call `std::terminate`. `std::jthread` joins on destruction, so a mid-spawn throw unwinds cleanly; catching the `system_error` lets the workers that did start drain the queue. Because a partial spawn can leave some slots unprocessed (and a total spawn failure all of them), a final single-threaded sweep runs `processOne` on any result slot still null after the pool has joined — guaranteeing every path is resolved exactly once. `processOne` always writes a non-null object, so `is_null()` reliably marks an untouched slot.

`resolveNif()` and `nif_.geometry()` both touch the filesystem and can throw. A worker thread that lets an exception escape its function calls `std::terminate` and takes the whole native host down. So every per-item failure is caught inside `processOne` and recorded as that slot's `{ "error": ... }` entry — the batch always completes and returns partial results. This mirrors the C# path, where a propagated `Parallel.ForEach` exception is caught by `CellInterop.GetGeometryBatch` and returned as error JSON rather than crashing the process.

The niftool `geo` output is embedded as-is when it is a JSON object (mirrors the C# `JRaw` path); otherwise it is surfaced as an error entry. Unlike the C#, the parse is validated (`parse(..., nullptr, false)` + `is_discarded()`) so a malformed but `{`-leading output cannot corrupt the whole batch payload.
