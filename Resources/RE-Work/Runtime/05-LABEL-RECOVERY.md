# 05 — Packed-Exe Discovery & Label Recovery (2026-07-07)

What happened, why it matters, and how to redo the technique for a future version gap. This started as
a Version Tracking task (correlate 1.10.155 → 1.10.163) and ended up finding and fixing a much bigger
problem with the 1.10.163 project itself, then superseding VT entirely with a better naming source.

## 1. The discovery: `fwgh_proj\BethesdaGhidraScripts` was built from a packed exe

While doing a cheap feasibility check for Version Tracking (raw-byte matching a handful of known
1.10.155 functions against the 1.10.163 exe file, before committing to a full VT run), **zero of nine
candidates matched** — including a 5-byte trivial leaf accessor, which should never fail by pure chance.

Root cause, confirmed two independent ways:

1. **Shannon entropy.** Real x86-64 code reads ~5.5–6.5 bits/byte. Packed/encrypted data reads ~8.0
   (indistinguishable from random). Sampled broadly across the whole 44MB `.text` section:

   | | 1.10.155 (fine) | 1.10.163 original (`fwgh_proj`) |
   |---|---:|---:|
   | `.text`, whole-section sample | 6.33 | **7.9999** |
   | `.rdata` (vtables/RTTI/strings) | 4.23 | 4.23 (normal — not packed) |

2. **Decompile sanity check.** Three functions with "trusted" CommonLib-RTTI names —
   `PlayerCharacter::ServePrisonTime`, `TESObjectREFR::Predestroy`, `PlayerCharacter::GoToPrison` — all
   decompiled to nonsense: `in()`/`out()` (privileged ring-0 port-I/O instructions that cannot appear in
   real user-mode code), `halt_baddata()`, "Control flow encountered bad instruction data."

Confirmed via a small read-only diagnostic (`scripts/VT_ByteDump.java`) that the **Ghidra project's own
stored bytes**, not just the on-disk exe, are the packed data — so this isn't a case of the exe file
changing after import; whatever was imported was already packed.

**Why this matters:** because `.rdata` (where vtables/RTTI type descriptors live) is intact, the
CommonLib RTTI-vtable-slot matcher could still run and produce plausible-looking results — 199,088
functions, 14,009 named. The names/addresses from that matching are *not automatically wrong*, but
**any finding derived from actually reading/decompiling `.text`** in this project is unverified, and
(as it turned out) a non-trivial minority of the RTTI-matched names are flat-out wrong — see the
disagreement characterization below.

**Lesson for next time:** validate a new Ghidra import by *decompiling* 2-3 named functions and
eyeballing the output, not just checking function/naming counts. A big function count and a healthy
naming percentage are both fully achievable on top of packed garbage.

## 2. The fix: unpack + fresh import + fresh naming pass

The user supplied a Steamless-unpacked exe: `E:\F4SE OG\ReverseEngineering\Symbols\1.10.163\Fallout4.exe.unpacked.exe`
(v1.10.163.0 confirmed via FileVersion/InternalName, SHA256 `9A147A35...` — verified distinct from both
the packed original `55F57947...` and the 1.10.155 retail exe `886D67FC...`, so not an accidental
re-copy of either).

Rebuilt as a **fresh import** (not a clone-and-swap of the old project) into
`E:\F4SE OG\ReverseEngineering\Projects\fwgh_proj_1.10.163_unpacked\` — Ghidra ties analysis state to the originally
imported bytes, so starting clean is more reliable than trying to graft new bytes onto an existing
analyzed program. Same recipe as the 1.10.155 named project import (`ReverseEngineering\Symbols\GHIDRA_PROJECT.md`): `mkdir` the
project dir first, 12GB heap, pipe blank stdin to dodge the `pause`-hang gotcha (see `01-SETUP.md`).

Re-verified after import: `.text` entropy back to **6.334** (matches 1.10.155's 6.33), and the same
three functions decompile to sane C (real Win32 API calls, atomic-refcount `LOCK()/UNLOCK()` idioms,
real branches/loops — see `03-FINDINGS.md`'s 2026-07-07 entry for the actual decompiled snippet).

Then re-ran the existing CommonLib naming pass (`scripts/name_db_1.10.163_unpacked.py`, same technique
as `scripts/name_db.py`) fresh against the correct bytes: **209,817** total functions, **14,824** scoped
`Class::Method` names.

## 3. Layering on a second naming source: the community IDC label file

Separately, the user tracked down a community-maintained IDC label script keyed directly to 1.10.163
addresses: `E:\F4SE OG\ReverseEngineering\Symbols\F4_1_10_163_IDCLabels.idc`, format:

```
gen_name(0xADDR,"NAME");
```

This is a **version-exact** naming source (no cross-version correlation needed, unlike Version
Tracking) — real mangled MSVC C++ symbols at real addresses.

### The obfuscation gotcha

The quoted `NAME` strings are obfuscated: **every character byte-shifted +3** (e.g. `"bwovbhqg"`
decodes to `"_tls_end"` by subtracting 3 from each byte). The `0xADDR` values and surrounding IDC syntax
are already plain — only the quoted strings need decoding. This is a trivial scheme to spot once you
have one known plaintext pair (a recognizable linker-added symbol like `_tls_end` is a good anchor to
check against). Decoded to `E:\F4SE OG\ReverseEngineering\Symbols\F4_1_10_163_labels_decoded.csv`:

```
0xADDR,decoded_name
```

**698,074 rows.** Addresses are absolute VAs (already include the `0x140000000` base — no RVA math
needed). Names are a mix of MSVC-mangled (`?pCurrentAlphaGroup@BSBatchRenderer@@1PEAVGeometryGroup@1@EA`)
and a handful of already-plain linker symbols (`_tls_end`).

### Applying it: `scripts/ApplyDecodedLabels.java`

Key design points (see the script itself for the full implementation):

- **Uses Ghidra's own demangler** (`ghidra.app.util.demangler.DemanglerUtil.demangle(Program, String)`
  → `DemangledObject.applyTo(program, address, options, monitor)`) — the same mechanism symbol import
  uses internally. This gives applied functions real namespaces and signatures, not bare renames. Falls
  back to a sanitized plain label/rename when a string doesn't demangle (the plain linker symbols).
- **Precedence rule — never silently overwrite a real name.** An address whose *current* name doesn't
  match the Ghidra-default prefixes (`FUN_`, `sub_`, `DAT_`, `LAB_`, etc.) is assumed to already have a
  trustworthy name (from the CommonLib pass). If the CSV's name for that address disagrees, the
  disagreement is **logged to a CSV report**, not applied. Only default-named addresses get the CSV name
  directly.
- **Single transaction for ~700k rows.** Headless GhidraScript execution auto-wraps the whole `run()` in
  one transaction; the script doesn't manually start/commit per-symbol, which matters a lot at this
  scale.
- Usage: `-postScript ApplyDecodedLabels.java` with two optional args (CSV path, disagreement-report
  path) — defaults point at the paths above if omitted.

### Result

| Metric | Count |
|---|---:|
| CSV rows processed | 698,074 |
| Applied via demangler (real signature/namespace) | 369,895 |
| Applied via plain fallback | 288,085 |
| **Applied, total** | **657,980** |
| Already matching (CSV agreed with existing name) | 21 |
| **Disagreements logged, not applied** | **40,073** |
| Out of range / parse errors | 0 |

### Disagreement characterization (sampled, not resolved individually)

40,073 rows split ~24.6k `data-or-label-name-conflict` / ~15.5k `function-name-conflict`. Spot-checking
samples of each:

- **Most `data-or-label-name-conflict` rows are cosmetic, not real conflicts.** Ghidra's own
  auto-analysis already names vtables/RTTI descriptors it finds via its own PE/RTTI analyzer (e.g.
  `VTABLE_TESEffectShader`, `RTTI_BSTDerivedCreator_...`) independent of either naming pass; the CSV's
  demangled form for the *same* entity just looks different (`` const TESEffectShader::`vftable'{for_
  `TESForm'} ``). Same address, same object, different string formatting — not a real conflict.
- **Most `function-name-conflict` rows are CommonLib naming gaps, not wrong names.** CommonLib doesn't
  know the real method for many vtable slots and falls back to a generic `Class::FuncN` placeholder; the
  CSV supplies the real name — frequently an MSVC-generated `` scalar/vector deleting destructor' `` thunk,
  but also real methods (`NiTimeController::LoadBinary`, `hknpHybridBroadPhase::update`,
  `bnet::Parallel<...>::DoPump`). These are safe upgrades a human could apply; correctly not
  auto-applied by the conservative rule.
- **A genuine minority are real CommonLib RTTI-matching errors** — confirmed via the two functions from
  the original packed-binary discovery: `PlayerCharacter::ServePrisonTime` is actually
  `PlayerCharacter::Revert(BGSLoadFormBuffer*)`; `PlayerCharacter::GoToPrison` is actually
  `PlayerCharacter::FinishLoadGame(BGSLoadFormBuffer*)`; `TESObjectREFR::Predestroy` is actually
  `` TESObjectREFR::`scalar deleting destructor' ``. CommonLib's vtable-slot assumption was simply wrong
  for these. **Before trusting an old CommonLib-derived hook name/address, grep
  `label_apply_disagreements.csv` for it.**

## 4. Version Tracking was skipped

The original plan (before the packed-exe discovery) was to use Ghidra's Version Tracking feature
(`AutoVersionTrackingScript.java`, confirmed scriptable headlessly via positional `-postScript` args —
see that script's own header comment for the exact invocation) to correlate 1.10.155 → 1.10.163 and
transfer names across the version gap. Once the community IDC label file provided **657,980** direct,
version-exact names (vs. VT's cross-version correlation, which would have topped out lower and slower),
VT was deprioritized and not run. The investigation notes are preserved here in case a *future* version
gap has no equivalent direct label source and VT ends up being the right tool:

- VT is driven by `ghidra.feature.vt.gui.actions.AutoVersionTrackingTask` (see
  `Ghidra/Features/VersionTracking/lib/VersionTracking-src.zip`).
- `AutoVersionTrackingScript.java` (ships with Ghidra) is fully headless-scriptable: run it as a
  `-postScript` with three positional args (session folder, session name, source-program path) via
  `askValues()`, which consumes `-postScript` arguments positionally in headless mode instead of
  popping a GUI dialog (see `GhidraScript.askValues()`'s javadoc: "if script arguments have been set,
  this method will use the next arguments in the array").
- Correlators available: Exact Bytes, Exact Instructions (mnemonic-level, tolerant of operand/relocation
  differences), Data Reference, Symbol. A cheap feasibility check (byte-matching a handful of known
  small functions) is worth doing before committing to a full run — that's exactly what surfaced the
  packed-exe problem in the first place.

## 5. Redoing this for a future version gap

1. **Always sanity-check a fresh import by decompiling, not just naming stats.** Entropy check on
   `.text` (expect 5.5–6.5 bits/byte) is a fast first-pass filter.
2. If you find a community label source for the target version, check for an obfuscation scheme before
   assuming it's directly usable — a simple byte-shift is easy to spot with one known plaintext anchor.
3. Produce a CSV in the `0xADDR,name` format (absolute VAs) and run `ApplyDecodedLabels.java` against a
   freshly-imported, confirmed-unpacked program — it already handles the demangling and the
   don't-overwrite-real-names precedence rule.
4. Only reach for Version Tracking (section 4 above) when there's no direct, version-exact label source
   — it's a fallback correlation technique, not the first choice when real addresses are available.
