# LEGAL-README — provenance of everything in this repository

This file records **where every piece of content in this repository came from and under what
license**. It is a provenance record built during the archive's pre-push audits, not legal advice.
The working rule, from the repository owner: *never push a PDB or anything that can be
copyrighted.* Everything here was checked against that rule — and the items that did not survive
the check are listed in [§4](#4-what-is-deliberately-not-here) so the decisions are visible.

Companion files: [MANIFEST.md](./MANIFEST.md) (what is tracked vs kept local, mechanically) and
[CONTRIBUTING.md](./CONTRIBUTING.md) (the rules for keeping it that way).

---

## 1. Own work — the repository's own content

Unless a row below says otherwise, everything in the tree is the repository owner's own writing,
analysis, or source code, and no third-party permission is required to distribute it.

| Area | What it is | License status |
|---|---|---|
| Root guides (`README.md`, `MANIFEST.md`, `WORK-LOG.md`, `FINDINGS-INDEX.md`, `ISSUES-MAPPING.md`, `CONTRIBUTING.md`, this file) | the owner's own documentation | no license declared; owner's copyright |
| **Behaviour Graph Studio** (the C#/Avalonia product) | the owner's product — **not tracked in this archive**; it lives in its own public repo `G-A-R-D-E-N/BGS` (GitHub) | **MIT**, Copyright (c) 2026 NomadsReach — see the product's `LICENSE` there |
| `ReverseEngineering/` — the RE pipeline docs, scripts, and re-mcp | the owner's findings prose, Ghidra scripts, and MCP server (`package.json` marks it `UNLICENSED` — private work) | owner's copyright, no public license granted |
| `Tools/C# Project/` + `Tools/C# Project-phase1/` — **BgsInterop** | the owner's C# bridge/interop project | **all rights reserved** (`LICENSE.md`: no open-source license granted at this time) |
| `Tools/FO4AnimForge/` | the owner's Blender-side animation pipeline | README states **GPL-3.0** — **no `LICENSE` file is present** in the archive or the nested repo; see [§5 gap note](#5-provenance-notes-and-gaps) |
| `Tools/Modlist Org/` | the owner's Node mod-list dashboard (`package.json` has no license field) | owner's copyright, no license declared |
| `Tools/godot-mcp-server/` | the owner's Godot MCP bridge (`package.json` marks it `UNLICENSED`) | owner's copyright, no public license granted |
| `bgs-mirror-strip.py`, `merge_animtextdata_manifest.ps1` | small pipeline scripts | owner's copyright, no license declared |

Commit identity throughout is the owner's own GitHub noreply handle
(`144523850+NomadsReach@users.noreply.github.com`); no third-party personal data is present.

## 2. Third-party-derived content that IS in the tree

Only four things in the tree are derived from someone else's work, and each carries its
attribution alongside it:

| File(s) | Derived from | License | Where attributed |
|---|---|---|---|
| *(in the product repo, not this archive)* `src/Hkx/HavokClassTypes.json`, `src/Hkx/BehaviourClasses.json` | **hkxpack**'s published class-description metadata (member types, enum values, which members are written to a file), combined with instance sizes read from Fallout 4 (facts, not code) | **MIT**, Copyright (c) 2016 DexesTTP — the derived JSON carries the same licence | inline provenance note in the JSON + the product's `THIRD_PARTY_NOTICES.md` |
| `ReverseEngineering/Organized/*.csv` | function names, addresses, RVA/vtable slots, and class-inheritance facts read from the game binaries; **names partly originate from CommonLibF4 (MIT) and the community IDC label file**; the verdict/evidence columns are the owner's own analysis | facts + owner analysis; the CSV evidence column holds one-line decompiler *signatures* (metadata), **no function bodies** | this file, §5 |
| `ReverseEngineering/03-FINDINGS.md` and all findings prose | the owner's written analysis of decompiled behaviour — describes *what functions do*, quotes no code bodies | owner's copyright | — |
| `Tools/FO4AnimForge/docs/havok-classes.md` | distilled facts from the 2013 Havok SDK class headers (Project Anarchy lineage) — the raw headers are excluded, only the owner's prose synthesis is tracked | owner's copyright; raw Havok material excluded (see §4) | the doc itself states its provenance |

### Dependencies referenced but not redistributed

The product's `THIRD_PARTY_NOTICES.md` (in its repo, not this archive) also lists **Avalonia**
(MIT, .NET Foundation) and the **.NET runtime** (MIT, .NET Foundation) — build/runtime
dependencies of the product, not vendored anywhere here. The same holds for every package
reference in the tracked `package.json`/`.csproj` files: dependency declarations, not copies.

## 3. Facts vs expression — the legal posture

The bulk of `ReverseEngineering/` is *facts about a compiled binary*: function names, addresses,
offsets, class layouts, vtable slot indices, and the AnimTextData CRC formula. Facts and ideas are
not copyrightable expression, which is the same basis the Address Library project (MIT) and the
modding community's RE documentation rest on. The tree contains **no** decompiled function bodies,
no game data files, and no game assets — only the owner's prose about them and the one-line
signatures in the audit CSVs.

## 4. What is deliberately NOT here — and why

Everything below exists in the workspace but is excluded from this repository. The rationale is
legal, not just policy:

| Excluded | Copyright/rights holder | Why it is excluded |
|---|---|---|
| Game binaries (`.exe`, `.dll`), archives (`.ba2`/`.bsa`), meshes/animations (`.nif`/`.hkx`/`.hko`), textures, `.esm`/`.esp` | Bethesda (the game), mod authors | copyrighted game code and data; redistribution requires rights the owner does not have |
| PDB files and raw symbol/label dumps (`.pdb`, `.idc`, the decoded community IDC CSV) | Microsoft (PDB format/content) + game publishers | proprietary; only facts distilled from them are tracked |
| Decompiled code bodies (`Decompiles/`, `Projects/` Ghidra databases, `DECOMPILE-RECORDS.md`, `Findings/Havok/*.txt`, `Organized/` per-version dumps) | derivative of Bethesda's copyrighted code | decompilation output is the clearest derivative-work risk |
| Havok SDK, Content Tools Compendium, and SDK header copies (`Havok/`, `docs/reference/havok-sdk/`, `Mods/FBXImporter/`) | **Havok / Microsoft — marked confidential**: files carry "Confidential Information of Telekinesys Research Limited (t/a Havok)… trade secrets of Havok" | confidential + copyrighted; read-only reference, never distributed |
| AAF mod and other third-party mod content under `FO4AnimForge/Mods/` | the mod's authors | wholesale copies of others' published work |
| Third-party guides (`guide.md`, `guide2.md`, Blender Rig Animation Guide, HCT Filters) | F4AK, HKX-pack tutorial author, Shiagur, Havok | wholesale copies of others' documentation |
| `GodotCreationKit.zip` (embeds the paid "Godot MCP Pro" addon) | addon author | commercial third-party code |
| Third-party repo checkouts (`maorcc/gimp-mcp`, `libreearth/gimp-mcp`, `Nihilantropy/godot-mcp-docs`, Godot, Ghidra, KnownModTools, hkxpack jar, nifly, Bethesda's Scripting Resources/PapyrusCompiler, …) | their respective owners (MIT/GPL/other) | not the owner's work product; kept local and documented instead of shipped |
| `Blender/`, `.bgs-anim-corpus/` | game-derived asset work | game-derived content |
| Build output, `node_modules`, SQLite indexes, MCP bundles, session logs | — | regeneratable or private working data |

The `.gitignore` enforces these as patterns — a workspace sweep verified that `git add -A`
currently stages nothing (see MANIFEST's integrity checks).

## 5. Provenance notes and gaps

- **`ReverseEngineering/Organized/` CSVs have mixed provenance.** Function names are facts, but
  some originate from CommonLibF4 (MIT) and some from the community IDC label file whose own
  redistribution terms are unknown; the owner's verdict columns are original analysis. If even
  this feels too close to symbol redistribution, the CSVs can be dropped and the findings docs
  still stand alone (this was flagged in MANIFEST's flags section).
- **`Tools/FO4AnimForge/` claims GPL-3.0 in its README but ships no `LICENSE` file.** A GPL
  declaration without the license text is ambiguous — if the project is to be distributed as
  GPL-3.0, the standard GPL-3.0 text should be added at `Tools/FO4AnimForge/LICENSE`. **Action
  recommended before this project is released (not required for this archive).**
- `ReverseEngineering/converter-handoff.md` and the 08-05 findings entry describe the packfile
  *writer* reverse-engineering from the game binary — facts about the binary, no Havok SDK text;
  the engineering record that documented validation against the retail Havok toolchain was
  dropped before push (see MANIFEST flags).

---

*Not legal advice. If this repository is ever made public, re-review §4 against the audience —
"private server" and "public" are different bars.*
