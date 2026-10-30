# Plugin Edit Tool — Master Index

Reference hub for editing Fallout 4 plugins (`.esp`/`.esm`/`.esl`). Two distinct things are
documented here:

- **FO4RecordEditor** — a Fallout 4 plugin IDE (desktop editor + MCP server) written in
  native C++23 on Radicle, the C++ plugin library in this repository. Reads and writes real binaries;
  needs no .NET, Mutagen or xEdit. **This is the primary tool.** Its tool list is in
  [../MCP_SETUP.md](../MCP_SETUP.md).
- **The Spriggit text pipeline** — an older, separate workflow from the outer workspace: convert a
  plugin losslessly to a folder of per-record JSON, edit or generate it with scripts, compile it back.
  Best for diffing, git tracking, and bulk generation. It is built on Mutagen; FO4RecordEditor no
  longer is.

```
   binary .esp  <-->  Spriggit (CLI/UI)  <-->  folder of per-record JSON/YAML
                          built on
                         Mutagen (C# API, validation, generation)
```

---

## Start here

| If you are… | Read |
|---|---|
| Using the MCP tools to edit records | [../MCP_SETUP.md](../MCP_SETUP.md) — the native tools, their arguments and the write-safety rules. [KNOWLEDGE.md](KNOWLEDGE.md) is older background from the C# server |
| Working on the editor's own code | [ARCHITECTURE.md](ARCHITECTURE.md) — the native host, write-safety invariants, how to add an MCP tool |
| Generating/compiling plugins as text | [PIPELINE.md](PIPELINE.md) → [SPRIGGIT.md](SPRIGGIT.md) → [JSON_RECORD_FORMAT.md](JSON_RECORD_FORMAT.md) |
| Authoring meshes | [NIF_TOOLCHAIN.md](NIF_TOOLCHAIN.md) |

## All docs in this folder

| File | What it covers |
|---|---|
| [KNOWLEDGE.md](KNOWLEDGE.md) | Background from the C#/Mutagen server: tool patterns, type-name rules, form ID encoding, `run_script` Rules 0–10, master-order traps, `create_cell`/`create_placed_object`, SW-keyword removal, binary TES4 surgery. The tool names and `run_script` rules do not apply to the native server (see [../MCP_SETUP.md](../MCP_SETUP.md)); the rest has not been re-checked against it |
| [ARCHITECTURE.md](ARCHITECTURE.md) | **Current.** The native C++ host: RPC targets, the React IPC contract, Radicle as the plugin engine, write-safety invariants, adding an MCP tool, release paths |
| [HISTORY.md](HISTORY.md) | How the editor got here; the abandoned WPF shell; the React pivot; what was never started |
| [UI_REDESIGN_TASKS.md](UI_REDESIGN_TASKS.md) | **Live backlog** — approved 6-phase xEdit-style UI redesign, still unbuilt (`bd f4se_og-4wp`) |
| [CONFLICT_ENGINE.md](CONFLICT_ENGINE.md) | xEdit conflict-detection research (DisplaySortKey, conflict priority) + the plan to beat it |
| [findings.md](findings.md) | Per-session fix log for the `feat/schematic-dedupe` branch (plugins repaired, counts, commits) |
| [PIPELINE.md](PIPELINE.md) | Full round-trip: parse → build → merge → compile → deploy |
| [SPRIGGIT.md](SPRIGGIT.md) | serialize/deserialize CLI, packages, the compile gotcha + bypass, ESL corruption warning |
| [MUTAGEN.md](MUTAGEN.md) | Historical: the C# generator API (load Fallout4.esm, validate FormKeys, create records). FO4RecordEditor no longer builds against Mutagen |
| [JSON_RECORD_FORMAT.md](JSON_RECORD_FORMAT.md) | Spriggit's per-record JSON layout, naming convention, header/RecordData |
| [NIF_TOOLCHAIN.md](NIF_TOOLCHAIN.md) | Radicle's NIF reader and writer behind the `nif_*` MCP tools and the GUI panel with a textured 3D viewport (author/repair/verify/view FO4 NIFs, no NifSkope) |
| [SCRIPTS.md](SCRIPTS.md) | Helper scripts in the tool folder |
| [MO2_SETUP.md](MO2_SETUP.md) | Loading an MO2 modlist. ⚠ Contains an unresolved contradiction with the shipped README about running *through* MO2 — read the banner |

**Not here:** end-user docs for the distributable ship from the repo itself —
`PluginEditTool/README.md` and `PluginEditTool/docs/MCP_SETUP.md`. Keep it that way; a
recursive copy of that `docs/` folder is what shipped internal planning notes to end users once
already. `package.ps1` now allowlists exactly one file.

Related, outside this folder (outer workspace, not part of this repository):
`Patched Data/` (crafting/schematic pipeline, ESP build pipeline) · `docs/Code Notes/` ·
`Tools/PluginEditTool/CLAUDE.md` (the agent pointer that loads in that folder).

---

## Tool inventory

### Plugin record editing
| Tool | Path | Role |
|---|---|---|
| **FO4RecordEditor** | `PluginEditTool/native/` (CMake target `fo4ide`) | The IDE + MCP server: one native C++ executable, `FO4RecordEditor.exe`. `publish.bat` builds it; `package.ps1` makes the release zip |
| Mutagen (patched) | `Tools\PluginEditTool\tools\Mutagen\` | Historical, outer workspace: C# record library for the Spriggit pipeline. **Our fork carries patches** (`Perk.cs` EPF3 choice flags, enum serialization) — a clean upstream clone cannot write perk EPF3 data |
| Spriggit (CLI) | `Tools\PluginEditTool\tools\Spriggit\Spriggit.CLI\bin\Release\net9.0\Spriggit.CLI.exe` | serialize/deserialize plugins |
| Spriggit (UI) | `Tools\PluginEditTool\tools\Spriggit\Spriggit.UI\bin\Release\net9.0\Spriggit.exe` | visual workflow, link manager |
| Translation exe cache | `%LOCALAPPDATA%\Temp\Spriggit\Translations\<pkg>\<ver>\<pkg>.exe` | per-game serializer (the compile bypass) |
| mastersort | `Tools\PluginEditTool\tools\mastersort\` | master-order utility |

Papyrus compile/decompile is **built into FO4RecordEditor** (`compile_papyrus` / `decompile_papyrus`)
-- Champollion is no longer needed, and as of issue #78 neither is the Creation Kit's
`PapyrusCompiler.exe`: `compile_papyrus` has its own compiler and an `engine` parameter that picks
between it and the CK (`auto` prefers an installed CK). The built-in engine still needs the vanilla
base script *sources* on the import path, which are not the compiler and are just `.psc` text. See
[PAPYRUS.md](PAPYRUS.md) for the whole subsystem.

---

## Quick start — Spriggit round-trip

```powershell
cd "<workspace>\Tools\PluginEditTool\tools\Spriggit"

# Plugin -> JSON folder ("serialize" = parse/extract)
.\spriggit-cli.bat serialize --InputPath "Some.esp" --OutputPath "D:\SomeRepo" `
  --GameRelease Fallout4 --PackageName Spriggit.Json.Fallout4

# JSON folder -> Plugin ("deserialize" = compile)
.\spriggit-cli.bat deserialize --InputPath "D:\SomeRepo" --OutputPath "Some.esp"
```

The words are backwards from intuition — **serialize = plugin→text**, **deserialize = text→plugin**.

## Conventions

- Spriggit pinned to **0.40.1**, package **Spriggit.Json.Fallout4** (JSON, not YAML, for FO4 here).
- Plain ESP (header `Flags: []`) loads last and wins conflicts; ESL/Light loads early and loses.
  Choose deliberately — see [JSON_RECORD_FORMAT.md](JSON_RECORD_FORMAT.md).
- Spriggit `create-plugin`/`deserialize` can corrupt **ESL master FormID refs** on FO4. For ESL-heavy
  patches prefer the xEdit Pascal route — see the gotcha in [SPRIGGIT.md](SPRIGGIT.md).
- **Verify the master order after every save.** A plugin with out-of-order masters makes the game hang
  on load with no crash log. See [[feedback_check_master_order_every_save]].

## Worked reference example

`Projects\Rad Mod\` is a complete working instance of the Spriggit pipeline (Python generates a JSON
record tree → Spriggit compiles `RadiationOverhaul.esp`). Mine it for patterns:
`Tools\PluginEditTool\Patched Data\Source\gen_esl.py`, `merge_overrides.py` (corrected 2026-07-18 —
`Projects\Rad Mod\tooling\` doesn't exist anymore, the scripts moved here), and
`docs\Rad Mod\ESP_BUILD_PIPELINE.md`.
