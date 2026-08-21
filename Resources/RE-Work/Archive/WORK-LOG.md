# Work Log — June through August 2026

A consolidated, dated record of everything completed in this workspace over the last three months.
Distilled from the primary sources rather than written fresh:

- `ReverseEngineering/03-FINDINGS.md` — the dated findings log (the engine/RE track)
- `ReverseEngineering/05-LABEL-RECOVERY.md`, `06-FUNCTION-INDEX.md`, `07-DECOMPILE-CANDIDATES.md`,
  `04-ROADMAP.md` — the naming-campaign records
- the product track (releases 1.0.0 → 1.0.5) — see `G-A-R-D-E-N/BGS` on GitHub; the BGS-side
  engineering records (write-path audit, reconciliation scoping) stay local with the product repo

Two tracks run through the whole period and meet in August: the **reverse-engineering track** (what
the engine does, address by address) and the **product track** (Behaviour Graph Studio, the editor
built on that knowledge). The log is organized by month, newest month first, with the release
history summarized at the end.

---

## August 2026 — the format work; the product ships

August is when the engine work stopped being about *names* and became about *bytes*: the Havok
packfile format itself was reverse-engineered from the binary, and BGS shipped its first five
releases.

### 2026-08-04 — BGS 1.0.0, initial release

Graph editing, filtering, selection, validation markers, undo, file comparison; animation controls,
variables, events, and behaviour-authoring tools. (CHANGELOG)

### 2026-08-05 — Fallout 4 ships the complete Havok packfile **writer**, not just a reader

The discovery that unlocked the native save pipeline. `hkSerializeUtil::savePackfile` and its whole
chain were decompiled from 1.10.163: `hkBinaryPackfileWriter::save`, `writeAllObjects`,
`doDeferredWrites`, `fillSectionTags`, `writeClassName`. The three fixup tables were pinned down
byte for byte — **local fixups are `(source, target)` pairs, global and virtual fixups are triples**,
each table padded to a 16-byte boundary with `0xFF`, offsets recorded after padding. `hkClassMember`
layout was confirmed byte for byte (stride 0x28; `m_offset` at +0x1E), and a full sweep of the
binary's dynamic class initializers produced a **950-class member-layout table** (2 unparseable
initializers and 1 template left as named gaps; two independent sweeps cross-checked — a member
cannot sit at or past its own class's object size). `hkClass` objects don't exist on disk (zeroed,
filled at startup), so class metadata only ever comes from the initializers' arguments.
(03-FINDINGS 2026-08-05)

### 2026-08-06 / 08-07 — BGS 1.0.1, 1.0.2

**1.0.1:** archive browsing so vanilla `.hkx` can be inspected from `.ba2` without extracting;
skeleton, mesh, and root-motion views; improved graph editing, symbol management, field editing,
save safeguards. **1.0.2:** native animation saving for supported frame edits; a frame browser with
bone filtering and direct frame navigation; improved animation details, playback controls, and
validation feedback. (CHANGELOG)

### 2026-08-08 — Workshop GRAB is one function; the highlight pick has its own ContextData

`Workshop::UpdateActiveEdit(ContextData const&, bool)` is a single toggle for grab and put-down, and
the trailing `bool` is **multi-select** (settled from call sites, not guessed). Confirmed *in game*
that `WorkshopMenu::HandleEvent` keeps running while the menu is suppressed — the reason
PrismaCrafting reverted its own grab implementation. The build-mode highlight pick runs through a
second `Workshop::ContextData` built in `JobListManager::AutoAim_PickRef` (a 600-unit Havok sphere
cast, filtered and written back through `SetSelectedEditItem` every frame — so the ContextData is
the only durable injection point). BGS **1.0.3** also shipped: graph simulation for event-driven
states, transition timing, and blend weights; subtree copy/paste; reusable templates; path
highlighting; animation trimming, retiming, and improved frame editing; root-motion display and
mesh previews. (03-FINDINGS 2026-08-08; CHANGELOG)

### 2026-08-09 — the BGS write-path audit; the Havok reference SDK arrives

Six findings measured over the 453-file sample corpus, each with its method recorded
(docs/findings/2026-08-09-write-path-and-class-table.md):

1. **Appended strings can land on an odd offset** — defect. All 37,545 local-fixup destinations in
   the corpus land on even offsets; `AppendData` doesn't round up where `AppendObject` does.
2. **Growing an empty array loses the do-not-free flag** — defect, blocks half of issue #44. All
   4,850 array capacity words in the corpus carry bit 31; four writers disagree on whether to
   preserve it.
3. **The class table records no default for any menu-valued field** — 430 members, 0 defaults.
4. **`hkbStateMachine` is not the stock shape** — size 328 with a non-convention member; left open
   for the executable to settle.
5. **Clip duration is one frame interval short of the frame count** — 76/76 Dogmeat animations
   confirm `(frames − 1) / fps`.
6. **A state machine often doesn't start where `startStateId` says** — 5 of 9 machines in
   `DogmeatRoot` use random start; the reachability check reads neither mode nor variable index.

Same day: the Havok SDK-2014.1.1 (the point release after the game's `hk_2014.1.0-r1`) and Content
Tools Compendium were acquired **for reading only** — confidential, never committed, conclusions
only ever written in the user's own words. (docs/findings; CLAUDE.md)

### 2026-08-13 — `MultiActivateManager::ButtonInfo` layout

`sizeof == 0x20`: `BSStringT` label at 0x00, `BSTSmartPointer<IMultiActivateUser>` listener at 0x10,
`BUTTON_INDEX` at 0x18, `valid` bool at 0x1C. `BUTTON_INDEX` has six entries (4 buttons + count
sentinel + alias). The listener is dispatched through vtable slot **0x18** — mirroring what
PrismaUI_F4's `VanillaUISuppressor` already assumed. (03-FINDINGS 2026-08-13)

### 2026-08-14 — address-library forensics; layout-identity checks

- **The OG Address Library is contiguous**: 1,582,975 entries, 100% density over its range, vs 5.6%
  for AE — so a wrong-version lookup never fails. That licenses a *provenance* test (~5% AE-hit ⇒
  OG file, ~100% ⇒ AE), validated against CommonLibF4's own 1,103-entry `VariantID` table and
  implemented as `file_provenance` in f4-re-mcp.
- **`og_ae_map.ae_rva` is skewed one entry low** — the AE side came from a different disassembly
  pass; matching on it returns the *previous* function (90 false findings before it was caught).
  Bridge in id space only: `ae_rel_id`/`og_rel_id`.
- **CommonLibF4's generated ID tables are AE-native**, not OG — all six vendored `IDs_VTABLE.h`
  copies measure 85–100% AE-hit; on OG/NG they resolve into `.rdata` at unrelated addresses. That's
  upstream's to fix; recorded so it's never re-derived as a local bug.
- **`HitData` and `ProcessLists` are layout-identical between OG and AE** (every probed offset
  matches; `sizeof(ProcessLists) == 0x218` on both) — good news for the multi-runtime plugin.
- **`Actor::Update`'s vtable slot index remains unverified on OG** — left as an open item with the
  method (RTTI reconstruction) to close it. (03-FINDINGS 2026-08-14)

### 2026-08-15 — workshop patch sites; BGS 1.0.5; the git reconciliation; the backup

- **Workshop placement patch sites re-derived** for 1.10.163: ground snap is *three* call sites in
  `Workshop::Update` (not a single JBE), object snap is the `MOVSS XMM6,[…]` snap-query-radius
  load, and the yellow/existing-object move gate is `MOV EBX,[RAX+0x14]` inside
  `WorkshopCanSelectRef` — with unique COUNT-1 signatures for scanning. The old `pat_gsnap`/`pat_Y`
  community patches don't exist in canonical 1.10.163 (byte-scan 0 matches; they rotted against a
  different build).
- **The "too many workshop menus" break is display-level, not data-level**: the native C++ path has
  no cap anywhere (dynamic `BSTArray`s, 32-bit counts throughout); a consumer reading the RootNode
  global bypasses the break entirely.
- **BGS 1.0.5** — "The Bridge" home tab (file, tools, search, drag-and-drop `.hkx`, recent files),
  a first-run tour, tab memory, and a **safer-save** overhaul: crash-safe replacement, detection of
  concurrent file modification, interrupted-save recovery with backup preservation. Large valid
  arrays load again; malformed arrays are rejected before allocation; `.ba2` extraction rejects
  unsafe paths.
- **Git reconciliation scoping** (docs/findings/reconcile-local-and-github-main.md): GitHub `main`
  is canonical and ahead of the local clone — pushing local would regress it. Local `main` backed up
  to a bundle (kept local, in `ReverseEngineering/`) ahead of any reset. (03-FINDINGS 2026-08-15;
  CHANGELOG; docs/findings)

### 2026-08-16 — HUD-widget vtable IDs are stable OG↔AE

All **17** HUD-widget + ActivateChoiceListener vtables resolve to the *same* Address Library id on
OG and AE (17/17) — so the existing single `REL::ID(<OG id>)` in `VanillaUISuppressor` is already
next-gen-correct and needs no change. Method note that matters: Ghidra's `::vftable` symbols on the
1.11.221 project are misaligned by 8–16 bytes for 4 of these classes; ground truth is the Address
Library `.bin` cross-checked against raw bytes (a real MSVC vftable has a `.rdata` COL pointer at
offset −8). (03-FINDINGS 2026-08-16)

### 2026-08-17 — the AnimTextData subgraph id; the local-map correction

- **The 64-bit `AnimationOffsets\<id>.txt` key is a composite of two CRC-32 hashes**:
  `crc32(sapt_paths joined by "|") << 32 | crc32(behavior_path)`, reflected 0xEDB88320 table, init 0,
  no final XOR, lowercased input. Verified numerically: **2,065 of 3,401** shipped base-game
  subgraph ids recompute exactly from `Fallout4.esm` race records. Implemented in BGS as
  `SubgraphHash.cs` and the `symrm hash` command. (03-FINDINGS 2026-08-17)
- **Correction to the 08-16 local-map entry**: the "missing" OG ids were renumbered, not absent —
  reverse-lookup by AE RVA finds them all (`GetLocalMapCameraExtents` 1020638→2224090, etc.), and
  the AE-side spike-teardown pair was pinned (`LocalMapRenderer::~LocalMapRenderer` id 2194684 +
  `MemoryManager::Deallocate`; `nsPipboy_LocalMap::DestroyRenderer` 2224096). Also the newest
  finding in the log: `LocalMapRenderer::DestroyRenderer`/destructor AE addresses derived for the
  PrismaUI_F4_NG port.

---

## July 2026 — the naming campaign

July fixed the foundation and then named the binary: the packed-exe discovery, two independent
naming sources layered onto the correct bytes, a cross-reference that caught 150 real CommonLib
RTTI-matching bugs, and nine rounds of subsystem deep-dives that ended with *every* naming
question in the project resolved.

### 2026-07-07 — the packed-exe discovery and the label recovery

The original 1.10.163 Ghidra project had been built from a **Steam-DRM-packed exe**. `.text` entropy
was ~8.0 bits/byte (max/random; real code reads 5.5–6.5), and every decompile came out as garbage
(`in()`/`out()`/`halt_baddata()`) — even for "successfully named" functions. The `.rdata` vtables
were intact, which is why CommonLib RTTI matching had still produced plausible-looking names on top
of encrypted code.

Fixed with a Steamless-unpacked exe, a **fresh import** (`fwgh_proj_1.10.163_unpacked`), and a
fresh CommonLibF4 RTTI pass: 209,817 functions, 14,824 `Class::Method` names, `.text` entropy back
to 6.334. Then a second, independent, version-exact naming source was layered on top: a
community-maintained IDC label file (698,074 rows, names byte-shifted +3 — the obfuscation was
decoded and the technique written down for future version gaps). Applied through Ghidra's own
demangler with a never-overwrite-a-real-name rule: **657,980 names applied, 40,073 disagreements
logged** rather than clobbered. Same day, the plugin side: the PrismaHUD compass was following the
wrong quests (real mechanism: `CalculateCompassMarkersFunctor` + `TESQuest::GetActive`'s "tracked"
bit), `HUDComponentBase::CanBeVisible`'s real vtable slot was found, and a vtable patch that had
shipped with RVAs short by 0x2000000 was caught and corrected. (05-LABEL-RECOVERY.md; 03-FINDINGS
2026-07-07)

### 2026-07-10 — the cross-reference and the 313-candidate verification

All of 1.10.155 (full private PDB) and 1.10.163 were exported and bucketed into 19 subsystem
categories, then cross-referenced: the 40,073 label disagreements were checked against the
independent 1.10.155 PDB, producing **313 candidates where CommonLib was confident and wrong** and
**2,803 safe naming-gap fills** (placeholders → real names). All 313 were decompile-verified the
same day: **150 confirmed real misnames**, 68 MSVC identical-COMDAT-folding artifacts (a newly
documented false-positive pattern — not bugs), 63 where the original name was actually right, 32
inconclusive. **137 corrections applied** to the Ghidra project; 13 reverted after a post-apply
cross-check against CommonLibF4's own hand-verified headers (methodology lesson: check the headers
*before* applying). Zero live plugin code depended on any of the wrong names. Same day: 1,959 safe
placeholder→real-name fills applied, 78 engine-class reassignments applied after decompile review,
and the discovery that FO4's shared `ID3D11Device` is *not* created with
`D3D11_CREATE_DEVICE_SINGLETHREADED`. (06-FUNCTION-INDEX.md; 07-DECOMPILE-CANDIDATES.md;
03-FINDINGS 2026-07-10)

### 2026-07-11 — the subsystem sweeps

A huge day: vtable-shift audit (negative — CommonLibF4 `// NN` comments ≠ raw vtable slots),
offset audit of deployed plugins (all hardcoded RVAs verified correct), the HUD widget-suppression
table completed (all 30 vtable RVAs resolved + the ammo path), `HUDMenu` lifecycle (not safe to
kill; `UI::CloseAllMenus` exempts it), quests + AI organized and decompile-verified (radiant-quest
picker `BGSStoryManagerQuestFinder`, the CombatBehaviorTree's actual execution model, the
random-child-selection chain, `TESQuestStageItem::Resolve`), Pipboy coverage (real
`PipboyWorkshopData` schema, `MiscStatManager` array layout), the new `SearchStrings` tool plus the
controller button-prompt glyph system, and weather rounds 1–6 (the general `weatherData[]` decode
rule, the region-weather blend algorithm, the wind pipeline, camera-effect thresholds, the aurora
crossfade). The hknpShape vtable slots were resolved via the 1.10.155 PDB's slot order (CommonLib
was shifted by 4 slots), and **every remaining inconclusive/deferred naming item from the whole
campaign was closed — nothing left open**. (03-FINDINGS 2026-07-11; 04-ROADMAP)

### 2026-07-14 — weather rounds 7–9

`Atmosphere`/`Stars` vtables closed; three guessed function names retracted (the log records the
retractions as loudly as the findings); `Sky::FindSkySound` confirmed dead/orphaned with the real
construction site in `Sky::ActivateWeatherSounds` (which also surfaced the undocumented
`TESWeather+0xDC8` per-weather sound list); the rain-occlusion system found real; the
`ucThunderCount` lifecycle closed; lightning can trigger a weather-attached spell.
(03-FINDINGS 2026-07-14)

### 2026-07-16 — the player death/reload pipeline

`PlayerCharacter::UpdateWhenAIControlledOrDead` owns the whole death outcome (the engine loads a
save out from under a mod ~5 s after death unless suppressed; writing the *countdown* is the clean
lever). `TESDeathEvent` fires twice per death (SendDying then SendDeath, same frame). `Actor::IsDead`
is already true at `kDying` — which caught two real Permadeath bugs (double GAME OVER dialog, grave
archived twice). `fPlayerDeathReloadTime` is a **GameSetting**, not an INI setting, and the
OG/NG/AE Address Library ID spaces are completely separate databases — a bare `REL::ID()` silently
resolves to an unrelated function on other runtimes. `GameSettingCollection`'s BTree is ordered by
interned *pointer*, not string content. (03-FINDINGS 2026-07-16)

### 2026-07-17 / 07-18 — the AE runtime projects

`fwgh_proj_1.11.191_unpacked` and `fwgh_proj_1.11.221_unpacked` added (vtables auto-propagated
191→221). CommonLibF4's own classifier puts 1.10.163 = `kOG`, >1.10.163 ≤ 1.10.984 = `kNG`,
>1.10.984 = `kAE` — so both 1.11.x projects are `kAE`, and **1.11.221 is the current AE target**.
(01-SETUP.md; CLAUDE.md)

### 2026-07-23 — the landscape and player-controller catalogues

- **Landscape/exterior-cell system** catalogued and applied to OpenCommonwealth's exterior-cell
  compiler: cell size exactly 4096 units, the VHGT chunk exactly 0x448 bytes (33×33 signed-byte
  deltas), the "×8" scale factor verified against the binary, vertex normals confirmed as signed
  bytes — exterior cells use the same world-absolute coordinate space, so the cell service needed
  zero changes.
- **Player controller** fully catalogued: 7 handler classes with no CommonLibF4 header (all vtables
  + layouts confirmed), 13 camera states resolved to real classes (`kAnimated` confirmed to not
  exist; two states never before referenced publicly), `MovementControllerNPC`'s message-map pump
  decompiled, 12 core algorithms read directly.
- **f4-re-mcp ABI bug fixed**: `better-sqlite3` 11 → 13 (N-API prebuilds) fixed the
  `NODE_MODULE_VERSION` mismatch that had broken every lookup tool call. (03-FINDINGS 2026-07-23)

### 2026-07-29 — behaviour-graph variables at runtime

The complete by-name variable API mapped on 1.10.163: `IAnimationGraphManagerHolder::SetGraphVariableHelper<T>` for every type, `hkbBehaviorGraph::accessVariableValues` / `getActiveNodes`, and
`hkVariableTweakingHelper` confirmed present in the retail binary — with the caution that "present"
is not "reachable" and it needs a decompile before anyone builds on it. Variables are addressed by
**name**, not index, so renaming in a graph silently breaks callers. (03-FINDINGS 2026-07-29)

---

## June 2026 — the pipeline rebuilt

The month the workspace admitted its reverse-engineering setup was broken and rebuilt it properly.

### 2026-06-30 — workspace reorg + pipeline rebuild

Reverse-engineering material consolidated into `ReverseEngineering/` from three scattered locations
(`Tools\PDB\`, `Tools\Ghidra\`, `docs\ReverseEngineering\`). The "ready" Ghidra-OG the workspace
claimed to have turned out to be broken twice over: the bundled Ghidra copy was jar-stripped
(ClassNotFoundException at JVM init), and the "named" project had only 12 named functions — the
CommonLib naming pass had silently failed. Rebuilt: real Ghidra 12.1 install, the naming pass run
for real (14,009 `Class::Method` names on the packed project), and every gotcha written down in
`01-SETUP.md` so it never has to be re-discovered.

First verified findings: **`TESQuestTarget::alias` is at 0x10, not 0x00** — a real bug caught in
PrismaHUD's compass before it shipped; compass HUD classes confirmed absent from CommonLibF4; the
vanilla `HUDMenu.swf` element paths (ammo counters). (03-FINDINGS 2026-06-30; 01-SETUP.md)

---

## The product, release by release

Behaviour Graph Studio — a standalone desktop editor for Fallout 4 Havok behaviour files: open
`.hkx` from disk or from `.ba2`, browse tree/graph, edit, validate, compare, preview, save through
a native pipeline that backs up the original and refuses unsafe writes. (Nexus Mods)

| Release | Date | Headlines |
|---|---|---|
| **1.0.0** | Aug 4 | Initial release: graph editing, filtering, selection, validation markers, undo, file comparison; animation controls, variables, events, behaviour authoring |
| **1.0.1** | Aug 6 | Archive browsing (`.ba2`), skeleton/mesh/root-motion views, improved editing, symbol management, save safeguards |
| **1.0.2** | Aug 7 | Native animation saving for supported frame edits, frame browser with bone filtering, playback improvements |
| **1.0.3** | Aug 8 | Graph simulation (event states, transition timing, blend weights), subtree copy/paste, templates, path highlighting, animation trimming/retiming, root-motion + mesh previews |
| **1.0.4** | (undated) | Native save pipeline for behaviour **and** animation edits with `.bak` backups and save refusal; full editing/authoring (states, transitions, generators, variables, events, bindings); project chains, symbol usage, validation results; variable-bounds min/max |
| **1.0.5** | Aug 15 | The Bridge home tab + first-run tour + drag-drop `.hkx` + recent files; safer saving (crash-safe replace, concurrent-change detection, interrupted-save recovery); array-loading fixes; `.ba2` path safety |

Supporting tools that shipped alongside the product work: **f4-re-mcp** (a local MCP server
exposing the verified RE facts — name-confidence audit trail, vtable audits, address-library
mappings, `file_provenance`), **symrm** (symbol/rename utility, gaining the `crash` and `hash`
commands), and the **packfile pointer-layout converter** (8-byte ↔ 4-byte layout transcode from one
source of truth, PR #29) — the 4-byte output validated against the retail 32-bit toolchain and
value-cross-checked so the reference reader reads back exactly the values BGS intended.

## Where the work went

- **Behaviour Graph Studio** — the editor (this workspace's product)
- **The plugin suite** — PrismaHUD / PrismaUI_F4, PrismaCrafting, Permadeath, Rad Mod,
  OpenCommonwealth, Commonwealth Online — each RE pass above ends in a concrete consumer fix or a
  verdict that *no* fix is needed (the 08-16 HUD-vtable stability result being the cleanest
  example of the latter)
- **f4-re-mcp + symrm** — the tooling that makes the knowledge queryable
- **This repository** — the guide ([README.md](./README.md)) and the manifest
  ([MANIFEST.md](./MANIFEST.md)) that preserve it all
