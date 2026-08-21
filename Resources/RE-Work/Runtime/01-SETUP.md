# 01 — Setup & Environment

Everything you need to know about where things live and how the pipeline was built. If the named project
copy ever gets lost, the "Rebuild from scratch" section at the bottom recreates it.

## Running queries on Linux (quick reference)

Two shell wrappers automate the headless-query dance (symlink + stale-lock cleanup +
`analyzeHeadless -noanalysis`) and print just the script's own output. Both mirror the Windows
wrappers' interface (`run_query.ps1` for 163, `run_query_155.ps1` for 155):

| Project | Wrapper | Default script | Owner rewrite? |
|---|---|---|---|
| 1.10.163 `fwgh_proj_1.10.163_unpacked` | `Scripts/run_query_163.sh` | `FW_Query.java` | no (already `ciusr`) |
| 1.10.155 named project `fwgh_proj_1.10.155` | `Scripts/run_query_155.sh` | `Query.java` | yes (`Game`→you, restored on exit) |

```bash
Scripts/run_query_163.sh "HUDCompass"                    # substring search
Scripts/run_query_163.sh "decompile:NavMesh::Activate"   # decompile a function
Scripts/run_query_155.sh -script Stats.java          # naming/health diagnostic
```

Per-project manual steps and failure reference: "Opening this project on Linux" below (163) and
`Symbols/GHIDRA_PROJECT.md` (155).

## ⚠️ USE THIS PROJECT: `fwgh_proj_1.10.163_unpacked` (corrected, 2026-07-07)

**The original `fwgh_proj\BethesdaGhidraScripts` documented below was built from a packed/DRM-encrypted
exe — its `.text` is not real code.** See `03-FINDINGS.md`'s 2026-07-07 entry for the full discovery
(entropy ~8.0 bits/byte, decompiled output full of `in()`/`out()`/`halt_baddata()`). It's kept only for
reference/comparison, not deleted, and **must not be used for anything beyond addresses/names** — even
some of *those* turned out wrong (see below).

**The corrected, primary 1.10.163 project going forward:**

| Component | Path | Notes |
|-----------|------|-------|
| **Project** | `E:\F4SE OG\ReverseEngineering\Projects\fwgh_proj_1.10.163_unpacked\` | Program name `Fallout4_163_unpacked`, program path `/Fallout4.exe.unpacked.exe`. Fresh import (not a clone/reimport of the old one — Ghidra ties analysis to the originally-imported bytes, a clean import was more reliable). |
| **Source exe** | `E:\F4SE OG\ReverseEngineering\Symbols\1.10.163\Fallout4.exe.unpacked.exe` | Steamless-unpacked, 65,319,936 bytes, FileVersion/ProductVersion 1.10.163.0 (InternalName "Institute"), SHA256 `9A147A35686691AC6C52C2B3BC24027FC26119C2EB77716E10900AF29A61609E`. Verified distinct from both the packed original (`55F57947...`) and the 1.10.155 retail exe (`886D67FC...`). |
| **`.text` entropy** | ~6.334 bits/byte | Matches the 1.10.155 named project project's 6.33 — confirmed real code, not packed. |
| **Naming: CommonLib RTTI pass** | 209,817 total functions, 14,824 scoped `Class::Method` | Re-ran `CommonLibImport_F4_OG.py` (via `scripts\name_db_1.10.163_unpacked.py`) fresh against the correct bytes. |
| **Naming: community IDC labels** | 657,980 additional names applied, 40,073 disagreements logged | See "Community label recovery" below — a second, independent, version-exact naming source layered on top of CommonLib's names. |

Query it exactly like the old project (same `FW_Query.java`/`FW_Stats.java`, just point at the new
project location/name):

```powershell
$env:JAVA_HOME = "C:\Program Files\Java\jdk-25"
$gh      = "E:\F4SE OG\ReverseEngineering\Ghidra\ghidra_12.1_PUBLIC\support\analyzeHeadless.bat"
$projLoc = "E:\F4SE OG\ReverseEngineering\Projects\fwgh_proj_1.10.163_unpacked"
$scripts = "E:\F4SE OG\ReverseEngineering\Scripts"
Remove-Item "$projLoc\Fallout4_163_unpacked.lock" -Force -ErrorAction SilentlyContinue
"" | & $gh $projLoc "Fallout4_163_unpacked" -process -recursive -noanalysis `
      -scriptPath $scripts -postScript FW_Query.java "PlayerCharacter::Revert" *> out.log
```

### Opening this project on Linux (path + unpacked-binary gotchas)

Opened read-only on Linux 2026-08-13 (the `ButtonInfo` / `BUTTON_INDEX` work in `03-FINDINGS.md`).
Unlike the 1.10.155 named project project, this one **does not need the owner rewrite** — its `project.prp`
already records `OWNER="ciusr"`, so it opens directly as the current user. (If a Windows checkout ever
re-owns it to `Game`, the `NotOwnerException` workaround in
[`Symbols/GHIDRA_PROJECT.md`](./Symbols/GHIDRA_PROJECT.md) applies the same way.)

> **You usually don't need to do these by hand** — [`Scripts/run_query_163.sh`](Scripts/run_query_163.sh)
> automates the symlink + stale-lock cleanup + query (same interface as `run_query.ps1`; no owner
> rewrite needed). The steps below stay as the documented manual procedure and failure reference.

1. **`&` in the path still breaks `<project_location>`** — `analyzeHeadless` rejects the
   `Tools & Reverse Engineering/...` prefix in the project-location argument (the `&` in the Ghidra
   install/binary path is fine; only the `<project_location>` positional arg is strict). Symlink the
   project and scripts dir to clean paths and pass those:

   ```bash
   ln -sfn "/run/media/user/Games-Storage/Projects/Tools & Reverse Engineering/ReverseEngineering/Projects/fwgh_proj_1.10.163_unpacked" /tmp/fwgh_proj
   ln -sfn "/run/media/user/Games-Storage/Projects/Tools & Reverse Engineering/ReverseEngineering/Scripts" /tmp/fwgh_scripts
   ```

2. **Program name is `Fallout4_163_unpacked`**, imported from the **Steamless-unpacked** exe —
   `Symbols/1.10.163/Fallout4.exe.unpacked.exe` (65,319,936 bytes, program path
   `/Fallout4.exe.unpacked.exe`). The sibling `Symbols/Fallout4.exe` is the Steam-DRM-packed original
   (encrypted `.text`) and must never be re-imported — this project exists precisely because of that.
   See the "USE THIS PROJECT" table above and `05-LABEL-RECOVERY.md`.

3. **There is no debug file on this project.** 1.10.163 has no usable private symbols, so names came from the
   CommonLib RTTI pass + community IDC labels, and `debugFileLoaded()` is false. Consequence: Ghidra's
   enum/type records are demangler skeletons (name + size only, no enumerator values) — that is why
   `BUTTON_INDEX`'s enumerator names had to be read from the 1.10.155 named project project instead.

4. **Invocation** — always `-process -recursive -noanalysis` (never let a query re-run the multi-hour
   auto-analysis), and remove any stale `Fallout4_163_unpacked.lock` first:

   ```bash
   rm -f /tmp/fwgh_proj/Fallout4_163_unpacked.lock
   "/run/media/user/Games-Storage/Projects/Tools & Reverse Engineering/ReverseEngineering/Ghidra/ghidra_12.1_PUBLIC/support/analyzeHeadless" \
     /tmp/fwgh_proj Fallout4_163_unpacked -process -recursive -noanalysis \
     -scriptPath /tmp/fwgh_scripts -postScript DecompileAt.java b7b680
   ```

   Query scripts for **this** project are `FW_Query.java` / `FW_Stats.java` (the `Query.java` /
   `Stats.java` pair is 1.10.155-only); `DecompileAt.java`, `DisasmAt.java`, `DumpBytesAt.java` and
   `VTableDump.java` take raw 1.10.163 RVAs (hex, `0x` optional).

### Community label recovery (durable technique — reusable for future version gaps)

A community-sourced IDC label script (`ReverseEngineering\Symbols\F4_1_10_163_IDCLabels.idc`, format
`gen_name(0xADDR,"NAME");`) gives real, version-exact MSVC-mangled names keyed directly to 1.10.163
addresses. **Gotcha:** the quoted NAME strings are obfuscated — each character byte-shifted **+3**
(e.g. `"bwovbhqg"` decodes to `"_tls_end"` by subtracting 3 from each byte); the `0xADDR` values and
surrounding IDC syntax are plain. Decoded to `ReverseEngineering\Symbols\F4_1_10_163_labels_decoded.csv`
(`0xADDR,decoded_name` per line, 698,074 rows, addresses are absolute VAs already including the
`0x140000000` base — no RVA conversion needed).

Applied via `scripts\ApplyDecodedLabels.java`, which:
- Uses Ghidra's own demangler (`DemanglerUtil`/`DemangledObject.applyTo`) so applied names get proper
  namespaces/signatures, the same mechanism symbol import uses — not bare labels.
- **Never silently overwrites an existing real (non-`FUN_`/`DAT_`) name.** If the CSV disagrees with an
  existing name at the same address, it logs the disagreement to a CSV report instead. This caught real
  errors in the original CommonLib RTTI matching — see `03-FINDINGS.md`'s 2026-07-07 entry for two
  confirmed examples where CommonLib had matched the wrong vtable slot entirely.
- Runs inside the script's single ambient transaction (headless auto-wraps the whole script execution
  in one transaction) for performance across ~700k rows — no manual per-symbol commits.

**Disagreement characterization** (spot-checked a sample of the 40,073 logged rows, not resolved
individually): the two conflict categories split roughly 24.6k `data-or-label-name-conflict` / 15.5k
`function-name-conflict`.
- **Most `data-or-label-name-conflict` rows are cosmetic, not real conflicts**: Ghidra's own
  auto-analysis already names vtables/RTTI descriptors it finds via its own PE/RTTI analyzer (e.g.
  `VTABLE_TESEffectShader`, `RTTI_BSTDerivedCreator_...`) *before* either naming pass runs; the CSV's
  demangled form for the *same* entity just looks different (`` const TESEffectShader::`vftable'{for_
  `TESForm'} ``) — same address, same object, different string formatting.
- **Most `function-name-conflict` rows are CommonLib naming gaps, not wrong names**: CommonLib doesn't
  know the real method for many vtable slots and falls back to a generic `Class::FuncN` placeholder;
  the CSV supplies the real name (frequently an MSVC-generated `` scalar deleting destructor' ``/`` vector
  deleting destructor' `` thunk, but also real methods like `NiTimeController::LoadBinary`,
  `hknpHybridBroadPhase::update`). These are safe upgrades a human could apply, correctly not
  auto-applied by the conservative precedence rule.
- **A genuine minority are real CommonLib RTTI-matching errors** — the wrong-vtable-slot kind found
  during the original packed-binary investigation (`PlayerCharacter::ServePrisonTime` is actually
  `PlayerCharacter::Revert`; `TESObjectREFR::Predestroy` is actually a scalar deleting destructor).
  Worth grep-ing `label_apply_disagreements.csv` for a specific function name before trusting an old
  CommonLib-derived hook address.

**To redo this for a future version gap:** decode whatever label source you find (check for a similar
obfuscation scheme — a simple byte-shift is easy to spot by eye once you know one plaintext pair, e.g. a
recognizable linker-added symbol like `_tls_end`), produce a CSV in the same `0xADDR,name` format, and
run `ApplyDecodedLabels.java` with the CSV path as its first script arg (defaults to the path above if
omitted) against any freshly-imported, *unpacked* program.

## Components (original packed project — reference only, do not use for anything beyond addresses/names)

| Component | Path | Notes |
|-----------|------|-------|
| **Ghidra 12.1 PUBLIC** | `E:\F4SE OG\ReverseEngineering\Ghidra\ghidra_12.1_PUBLIC` | The real install (has the `.jar` files). Downloaded `ghidra_12.1_PUBLIC_20260513.zip` from the official NSA GitHub release. |
| **Named project (packed - see caveat above)** | `E:\F4SE OG\ReverseEngineering\Projects\fwgh_proj_OLD_packed_reference_only\BethesdaGhidraScripts` | A clone with CommonLib names applied + a repaired user-data store. Program = `/f4/og/Fallout4.exe`. |
| **JDK** | `C:\Program Files\Java\jdk-25` | Ghidra 12.1 needs JDK 21+; 25 works. Set `JAVA_HOME` when launching. |
| **pyghidra** | Python 3.14 site-packages | `pyghidra 3.1.0` + `jpype 1.5.2` (`pip install pyghidra`). Only needed to run `.py` scripts (e.g. the naming pass). |
| **Game exe (packed - do not use for byte-level analysis)** | `…\BethesdaGhidraScripts\exes\f4\og\Fallout4.exe` | 1.10.163.0, 65,503,104 bytes. Already imported into the project. |
| **CommonLib naming script** | `…\BethesdaGhidraScripts\ghidrascripts\CommonLibImport_F4_OG.py` | 23 MB, pre-generated. Applies struct types + `Class::Method` names. `clang` is NOT needed to *run* it (only to regenerate). Also used fresh against the corrected project — see above. |
| **Original BethesdaGhidraScripts tree** | `E:\F4SE OG\docs\Knowledge Materials\Reference\BethesdaGhidraScripts` | The address library, `SETUP_OG.md`, `run_headless.py`, the (broken) bundled Ghidra, and the original un-named project. |

> Paths reflect the **2026-06-30 workspace reorg**: `Knowledge Materials` now lives under `docs\`, and
> `Prisma` under `Projects\`. See the `project_workspace_reorg_2026_06_30` memory.

## The two traps we fell into (so you don't)

1. **The repo's bundled Ghidra is jar-stripped.**
   `…\BethesdaGhidraScripts\tools\ghidra` has **0 `.jar` files** — only `-src.zip` source archives. It cannot
   launch; you get `ClassNotFoundException: ghidra.GhidraClassLoader` at JVM init. That's why we downloaded a
   real 12.1 install to `E:\F4SE OG\ReverseEngineering\Ghidra\ghidra_12.1_PUBLIC`. (pyghidra can't use the stripped copy either — it loads the
   same missing jars.)

2. **The "ready" project was never actually named.**
   The original project had been imported + auto-analyzed (243 MB DB, 199,088 functions) but the CommonLib
   naming pass had silently failed — only **12** functions had names. We had to run `CommonLibImport_F4_OG.py`
   ourselves. After that: **14,009** `Class::Method` functions (CommonLib only names the documented engine API
   it has RTTI/vtable/REL data for, ~14k — not all 199k internal helpers; that's expected and enough).

Other gotchas:

- **Spaces in the install path are FINE** on Ghidra 12.1 — verified running `analyzeHeadless` from
  `E:\F4SE OG\ReverseEngineering\…`. (The original `GhidraClassLoader` failure was the jar-stripped bundled copy, not
  spaces; we initially misattributed it. No junction needed.)
- **`.bat` + `.py` postScript = "Ghidra was not started with PyGhidra. Python is not available."** The native
  launcher runs `.java` scripts fine but not Python. Use the pyghidra in-process driver for `.py` (02-WORKFLOW).
- **A killed analyzeHeadless leaves `BethesdaGhidraScripts.lock`** in the project dir. Delete it before re-running.
- **Always pass `-noanalysis`** for queries — the DB is already analyzed; omitting it triggers a 20–40 min
  re-analysis of the whole binary for nothing.
- **The source exe can be packed/DRM-encrypted without any obvious sign** — headers parse fine, Ghidra
  imports without error, auto-analysis finds a plausible-looking function count, and CommonLib RTTI
  matching can even succeed (RTTI/vtables live in `.rdata`, which may be untouched even when `.text` is
  encrypted). The only way this surfaced was *decompiling* a "successfully named" function and noticing
  `in()`/`out()` (privileged instructions) and `halt_baddata()`. **Sanity-check any new import by
  decompiling 2-3 named functions, not just checking naming/function counts** — see `03-FINDINGS.md`'s
  2026-07-07 entry. A quick Shannon-entropy check on `.text` also works: real x86-64 code reads
  ~5.5–6.5 bits/byte; packed/encrypted data reads ~8.0 (max/random).
- **A killed headless run invoked via `cmd /c "..."` (background-task wrappers, `Start-Process`) can hang
  forever on a bare `pause`**, not error out. `launch.bat` sets `DOUBLE_CLICKED=y` whenever it detects
  `cmd /c` invocation and calls `pause` on any non-zero exit — with no console attached this blocks
  indefinitely at ~0% CPU, silently holding any redirected log file open. Diagnose by comparing CPU time
  on the `java.exe` PID across two samples a minute apart (zero delta = stuck, not slow). Fix: pipe blank
  stdin through the call, e.g. `"" | & $gh $proj ...`.

## Why a *copy* (`E:\F4SE OG\ReverseEngineering\Projects\fwgh_proj_OLD_packed_reference_only`) and not the original

- The original project's **user-data sub-DB was corrupt** (`user\00\~00000000.db\db.0.gbf` missing — lost in
  the folder move). Opening it errors. We cloned the project to `E:\F4SE OG\ReverseEngineering\Projects\fwgh_proj_OLD_packed_reference_only`, cleared the broken user store
  (Ghidra recreates it), and named *that*. The user's original under `docs\Knowledge Materials\…` is untouched.

## Rebuild from scratch (if the copy is ever lost)

```powershell
$env:JAVA_HOME = "C:\Program Files\Java\jdk-25"

# 1. Clone the original project to the Tools location
robocopy "E:\F4SE OG\docs\Knowledge Materials\Reference\BethesdaGhidraScripts\ghidraprojects\BethesdaGhidraScripts" `
         "E:\F4SE OG\ReverseEngineering\Projects\fwgh_proj_OLD_packed_reference_only\BethesdaGhidraScripts" /E

# 2. Repair the user-data store in the COPY (Ghidra recreates it on open)
$rep = "E:\F4SE OG\ReverseEngineering\Projects\fwgh_proj_OLD_packed_reference_only\BethesdaGhidraScripts\BethesdaGhidraScripts.rep"
Remove-Item "$rep\user\00" -Recurse -Force
Remove-Item "$rep\user\~index.dat","$rep\user\~index.bak","$rep\user\~journal.bak" -Force
Remove-Item "E:\F4SE OG\ReverseEngineering\Projects\fwgh_proj_OLD_packed_reference_only\BethesdaGhidraScripts\BethesdaGhidraScripts.lock" -Force -ErrorAction SilentlyContinue

# 3. Apply CommonLib names (pyghidra in-process — see scripts\name_db.py)
python "E:\F4SE OG\ReverseEngineering\Scripts\name_db.py"
# -> expect: NAMING RESULT: total=~199250  scoped(Class::Meth)=~14009
```

If the *original* project is also gone, you'd re-import + re-name from the exe:
`python "…\BethesdaGhidraScripts\scripts\run_headless.py" f4 og` (but first point its `GHIDRA_DIR` at the real
install, since the bundled one is stripped). The copy path above is far faster — prefer it.

**Note:** this whole "Rebuild from scratch" recipe rebuilds the OLD, packed project — useful only if you
specifically need the historical comparison. To rebuild the corrected `fwgh_proj_1.10.163_unpacked`
project, see the steps below instead.

## Rebuild `fwgh_proj_1.10.163_unpacked` from scratch (if that copy is ever lost)

```powershell
$env:JAVA_HOME = "C:\Program Files\Java\jdk-25"
$env:GHIDRA_HEADLESS_MAXMEM = "12G"
$gh   = "E:\F4SE OG\ReverseEngineering\Ghidra\ghidra_12.1_PUBLIC\support\analyzeHeadless.bat"
$proj = "E:\F4SE OG\ReverseEngineering\Projects\fwgh_proj_1.10.163_unpacked"
$exe  = "E:\F4SE OG\ReverseEngineering\Symbols\1.10.163\Fallout4.exe.unpacked.exe"

# 1. Project location must exist as a directory BEFORE -import (Ghidra creates the .gpr/.rep inside it,
#    but errors "Directory not found" if the directory itself doesn't pre-exist).
New-Item -ItemType Directory -Force $proj | Out-Null

# 2. Import + full auto-analysis (piped blank stdin dodges the pause-hang gotcha above; no debug file for this
#    version, so no -preScript needed, unlike the 1.10.155 project in ReverseEngineering\Symbols\).
"" | & $gh $proj "Fallout4_163_unpacked" -import $exe *> "$proj\import_analyze.log"
# -> expect ~14 min, "REPORT: Analysis succeeded" / "Import succeeded" at the end.

# 3. Re-apply CommonLib names (pyghidra in-process, same pattern as scripts\name_db.py)
python "E:\F4SE OG\ReverseEngineering\Scripts\name_db_1.10.163_unpacked.py"
# -> expect: NAMING RESULT: total=~209817  scoped(Class::Meth)=~14824

# 4. Layer the community IDC labels on top (decode once if F4_1_10_163_labels_decoded.csv doesn't
#    already exist - see "Community label recovery" above for the +3 byte-shift decode)
Remove-Item "$proj\Fallout4_163_unpacked.lock" -Force -ErrorAction SilentlyContinue
"" | & $gh $proj "Fallout4_163_unpacked" -process -recursive -noanalysis `
      -scriptPath "E:\F4SE OG\ReverseEngineering\Scripts" -postScript ApplyDecodedLabels.java `
      *> "$proj\apply_labels.log"
# -> expect: applied (total) ~657980, disagreements ~40073

# 5. Sanity-check before trusting it: entropy + decompile (see the packed-binary gotcha above)
```
