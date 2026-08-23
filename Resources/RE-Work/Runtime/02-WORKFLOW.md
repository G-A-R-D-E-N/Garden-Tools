# 02 — Workflow: how to actually use it

Two modes:
- **Queries** (search a name, decompile a function) → fast `.java` GhidraScript via `analyzeHeadless`. ~1–2 min.
- **Mutations** (apply names/types) → `.py` via pyghidra in-process. Slower; you rarely need this once the DB is named.

## A. Search for a function by name

`scripts\FW_Query.java` takes a list of args. A plain word = case-insensitive substring search over function
names. An arg prefixed `decompile:` = decompile that exact function and print its C.

### The one-liner (or use `scripts\run_query.ps1`)

```powershell
$env:JAVA_HOME = "C:\Program Files\Java\jdk-25"
$gh      = "E:\F4SE OG\ReverseEngineering\Ghidra\ghidra_12.1_PUBLIC\support\analyzeHeadless.bat"
$projLoc = "E:\F4SE OG\ReverseEngineering\Projects\fwgh_proj_1.10.163_unpacked"
$scripts = "E:\F4SE OG\ReverseEngineering\Scripts"
Remove-Item "$projLoc\Fallout4_163_unpacked.lock" -Force -ErrorAction SilentlyContinue
& $gh $projLoc Fallout4_163_unpacked -process -recursive -noanalysis `
      -scriptPath $scripts -postScript FW_Query.java  HUDCompass  NavMesh::Activate  *> out.log
Select-String out.log -Pattern 'FW_Query.java>' | % { ($_.Line -replace '.*FW_Query.java> ','' -replace ' \(GhidraScript\).*','') }
```

> **2026-07-07 correction:** the project moved from `fwgh_proj\BethesdaGhidraScripts` to
> `fwgh_proj_1.10.163_unpacked` (program `Fallout4_163_unpacked`) — the original was built from a
> **packed/DRM-encrypted exe** and any decompiled output from it is garbage. See `05-LABEL-RECOVERY.md`
> for the full story and `03-FINDINGS.md` for the entry. The old project still exists on disk but should
> not be used for anything beyond address/name lookups derived from `.rdata` (RTTI/vtables) — never trust
> a decompile from it.

Even simpler — the wrapper:

```powershell
& "E:\F4SE OG\ReverseEngineering\Scripts\run_query.ps1" "HUDCompass" "NavMesh::Activate"
& "E:\F4SE OG\ReverseEngineering\Scripts\run_query.ps1" "decompile:NavMesh::Activate"
```

### Reading the output

```
=== IMAGE BASE: 0x140000000 ===
### SEARCH: NavMesh::Activate
  NavMesh::Activate  @ 0x140749ea0  (RVA 0x749ea0)  :: bool NavMesh::Activate(NavMesh*, TESObjectREFR*, ...)
### MATCHES(NavMesh::Activate): 1
```

- **`@ 0x140…`** = absolute virtual address (image base `0x140000000`).
- **`RVA 0x…`** = `address - image base`. This is what you compare against a CommonLibF4 `REL::ID`/offset.

## B. Decompile a function

```powershell
& "…\scripts\run_query.ps1" "decompile:HUDCompassMarker::Func1"
```

Notes:
- CommonLib names many vtable slots as `Class::FuncN` (it knows the class from the vtable but not the method
  name). `Func1` is often a trivial dtor/RTTI thunk and decompiles to ~nothing — the *real* logic usually
  lives in unnamed `FUN_140…` bodies nearby. To find those, decompile the named neighbour and follow its calls,
  or look at adjacent addresses.
- Plain engine structs (e.g. `TESQuestTarget`) have **no vtable/RTTI**, so they get **no name** — you won't
  find them by name search. Derive their layout from a function that *uses* them, or cross-check CommonLibSSE
  (see 03-FINDINGS for the TESQuestTarget example).

## C. Confidence check: is the DB even named?

If searches return 0 matches for obviously-present classes, the DB may have lost its names. Run the diagnostic:

```powershell
& "…\scripts\run_query.ps1" -script FW_Stats.java
```

Healthy output (post-2026-07-07 fix, `fwgh_proj_1.10.163_unpacked`): `total ~209817`, CommonLib-scoped
`~14824`, plus ~657,980 more names applied from the decoded community label CSV (see
`05-LABEL-RECOVERY.md`) — so the vast majority of `total` should now resolve to a real name, not `FUN_`.
If `scoped` is ~0, re-run the naming pass (D). If entropy/decompile looks like garbage again on a fresh
import, see the packed-exe gotcha in `01-SETUP.md` and `05-LABEL-RECOVERY.md` before assuming the naming
pass just needs a re-run.

## D. Re-apply CommonLib names (Python / pyghidra)

Only needed if the DB is un-named (fresh import, or names lost). **`analyzeHeadless` cannot run `.py`** — use
the in-process driver:

```powershell
$env:JAVA_HOME = "C:\Program Files\Java\jdk-25"
python "E:\F4SE OG\ReverseEngineering\Scripts\name_db.py"
# -> Running CommonLibImport_F4_OG.py ... Saved program.
#    NAMING RESULT: total=199250  unnamed=184826  scoped(Class::Meth)=14009
```

`name_db.py` opens the copy project with the real Ghidra, runs the 23 MB `CommonLibImport_F4_OG.py`, saves, and
prints stats. It's modelled on the repo's `run_headless.py::_run_one`. Takes a few minutes.

## E. Optional: the GUI

For interactive browsing/decompiling, launch the real Ghidra and open the named copy:

```powershell
$env:JAVA_HOME = "C:\Program Files\Java\jdk-25"
& "E:\F4SE OG\ReverseEngineering\Ghidra\ghidra_12.1_PUBLIC\ghidraRun.bat"
# File > Open Project > E:\F4SE OG\ReverseEngineering\Projects\fwgh_proj_1.10.163_unpacked\Fallout4_163_unpacked.gpr
# double-click Fallout4.exe.unpacked.exe
```

There is also a `GhidraMCP` extension present in `%APPDATA%\ghidra\…\Extensions\` (manifest currently
malformed/harmless) — a possible future path to drive Ghidra over MCP instead of the headless wrapper.

## Rules of thumb

- Always `-noanalysis`. Always clear the `.lock` first.
- One headless run can take **many** args — batch your searches/decompiles to avoid paying startup cost repeatedly.
- The named copy is disposable; if a run wedges it, re-clone per 01-SETUP. The user's E: original is never touched.
- An RVA is only meaningful for **1.10.163**. Don't reuse these addresses for NG (1.11.x) — different binary.
