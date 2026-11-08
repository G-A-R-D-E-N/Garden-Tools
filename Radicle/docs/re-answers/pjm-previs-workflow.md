# PJM previsbine scripts (anti-pattern reference only)

**Garden does not ship or depend on Creation Kit for previs.** PJM’s bat/xEdit flow is useful only as a map of *engine expectations* (RVIS paths, XCRI layout, VISI vs on-disk `.uvd`). The FO4IDE workflow is CK-free and must not mirror PJM’s multi-plugin merge theater.

## What PJM does (why we are not copying it)

- Spawns **`CombinedObjects.esp`** and **`Previs.esp`** as throwaway intermediates, then **merges** into a patch — double bookkeeping and easy to ship half-merged state.
- Depends on **CK** for meshes, PSG/CDX, and Umbra tomes.
- Leaves **`PleaseRemove*`** scaffolding REFRs that authors must clean up.
- Runs **eight manual steps** (bat + xEdit + CK + BA2) with weak validation until late merges.

## Engine rules we still implement (measured in Radicle)

- Exterior **VISI** only commits when the cluster **`.uvd`** exists at the RVIS-resolved path.
- **XCRI** is word-pairs; empty **XCRI** when PCMB exists without mesh refs (vanilla-legal).
- **PCMB/VISI** stamps must match baked asset times (`is_precalc_stale`).

## Native replacement (authoritative)

See `Radicle/docs/PREVIS.md` § Write pipeline and `re-answers/2026-11-previs-generation.md`.

One patch plugin in the **FO4IDE journal** → **scan worklist** → **per-cell plan** → **native bake** → **stamp** → **container/tome writers** → **save + archive**. No CK, no merge-from-junk-esp.
