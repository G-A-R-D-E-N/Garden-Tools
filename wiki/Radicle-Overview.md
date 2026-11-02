# Radicle: what it is and where it stands

This page is for developers new to the repo. It says what Radicle is, where things live, and how far it has got. For the wider design see the wiki pages Architecture, Repo-Map and Roadmap. This page does not repeat them.

## 1. What it is and why

Radicle is the native C++ Fallout 4 plugin SDK in `Radicle/`. The C++ namespace is still `fo4` (`Radicle/README.md`). It is an xEdit-style backend. It parses and exposes records, supports proven field and record edits, and writes changes back while keeping untouched data (`Radicle/README.md`).

It replaces Mutagen for PluginTool / PluginEditTool, and it supplies structured game data to GardenEngine (`Radicle/README.md`). Mutagen stays only as a compatibility oracle (`Radicle/README.md`). The core library has no FO4IDE, Mutagen or CLR dependency (`Radicle/docs/ARCHITECTURE.md`).

Three rules to know first:

- **Identity is a `FormKey`**, such as `00009A:RadiationOverhaul.esp`. Raw load-order FormIDs like `0x5600009A` are never the identity. On write, the destination plugin's master table rebuilds the on-disk FormID (`Radicle/README.md`, `Radicle/docs/FORMID_MODEL.md`).
- **Unknown bytes are kept.** Unknown layouts stay opaque or refuse mutation/remapping (`Radicle/README.md`, `Radicle/docs/ARCHITECTURE.md`).
- **A reader alone is not editing support.** Coverage per operation (READ / WRITE / ADD / REMOVE / CREATE / REMAP / VALIDATE / OPAQUE) is tracked in `Radicle/docs/FO4_PARITY.md` and issue #287 (`Radicle/README.md`).

## 2. Folder map (`Radicle/`)

| Folder / file | What lives there |
|---|---|
| `include/fo4/` | Public C++ headers, one sub-folder per subsystem, plus the C ABI headers `c_api*.h`. |
| `src/` | Implementation. Mirrors `include/fo4/`. The `c_api_*.cpp` files at the top are the C ABI. |
| `schema/fallout4/` | Machine-readable schema inputs: `fields.json`, `condition_functions.json`, `xedit_signatures.txt`, and a `README.md`. |
| `bindings/csharp/` | `Radicle.Interop` (.NET 8 wrapper), its tests, and `tools/generate_interop.py`. |
| `tests/` | The GoogleTest sources and fixtures. Built into one binary, `fo4_tests`. |
| `tools/` | `inspect/` (the `fo4inspect` CLI), `parity/`, `env_bench/`, `corpus/` (Python measuring scripts), and Python code generators and their tests. |
| `docs/` | Format notes, evidence docs, `FO4_PARITY.md`, `TEST_STRATEGY.md`, `OPEN_QUESTIONS.md`, and `code-notes/Radicle.md`. |
| `testdata/` | Small checked-in samples: two plugins (`FW_HeatherSurvivalPatch.esp`, `SlowTime.esp`) and `graph/`, `papyrus/`, `swf/` folders. |
| `probes/` | `gardenengine/`. Not described in the docs reviewed (unverified purpose). |
| `cmake/` | `radicleConfig.cmake.in` (install/export config). |
| `CMakeLists.txt` | Defines the targets below. |
| `README.md`, `ROADMAP.md`, `THIRD_PARTY_NOTICES.md` | Entry docs and license notices. |

Targets in `Radicle/CMakeLists.txt`:

| Target | Kind | Notes |
|---|---|---|
| `fo4` | library | The core C++ library. |
| `radicle_c` | shared library | The C ABI. Output name is `radicle`. |
| `fo4inspect` | executable | JSON inspection CLI (`tools/inspect/main.cpp`). |
| `radicle_parity_dump`, `radicle_parity_writeback` | executables | From `tools/parity/`. |
| `radicle_env_bench` | executable | From `tools/env_bench/`. |
| `fo4_tests` | executable | The gtest binary. |
| `radicle_abi_layout_probe`, `radicle_abi_stub`, `radicle_abi_stub_v1` | test helpers | Used by the C# ABI tests. |

## 3. Main subsystems

Sources are under `Radicle/src/<folder>` with headers in `Radicle/include/fo4/<folder>`. Only the primary folder is listed. This shows what is in the tree, not how complete it is (see section 6).

| Subsystem | One line | Primary folder |
|---|---|---|
| Plugins and records | Lossless TES4 / GRUP / record / subrecord reader, preservation writer, master remap, field and record edits. | `src/plugin`, `src/forms`, `src/environment` |
| Binary helpers | Bounds-checked little-endian cursor. Truncated input throws `fo4::parse_error`. | `src/binary` |
| Schema and codegen | FormLink locations, scalar field descriptors, condition tables. Python generators in `tools/*_codegen.py` emit C++. | `src/schema`, `schema/fallout4`, `tools/` |
| FormIDs | `FormKey` resolve/encode, light (ESL) master handling. | `src/formid.cpp`, `src/forms`, `src/plugin_key.cpp` |
| Load order and conflicts | `plugins.txt` / `loadorder.txt` / `Fallout4.ccc` parsing, load-order assembly, MO2 profile code, conflict graph, plugin diff. | `src/loadorder`, `src/graph/conflict_graph.cpp`, `src/graph/plugin_diff.cpp` |
| Strings | `.STRINGS` / `.DLSTRINGS` / `.ILSTRINGS` tables, text encoding. | `src/localization`, `src/text` |
| BA2 archives | `BTDX/GNRL` and `BTDX/DX10` (v1, v7, v8): list, read, write, texture extraction. | `src/archive` |
| NIF | NIF read/write, geometry, shader, skin, OBJ import. | `src/nif` |
| Havok HKX | Packfile, layout and skeleton code. Not on your list, but present. | `src/hkx` |
| Materials | `.bgsm` / `.bgem` byte-identical read/write. | `src/material` |
| Textures | DDS header, mip sizing, decode of one mip to RGBA8. No encoding, no BC6H. | `src/texture` |
| Audio / sound | SNDR / SOUN record read and authoring; AVM2 sound decode. | `src/environment/sound.cpp`, `src/avm2/sound_decode.cpp` |
| Papyrus | Source lexer/parser, PEX read/write, decompiler, type checker, code generator, interpreter and scheduler, save state, F4SE emitters. | `src/papyrus`, `src/papyrus_source`, `src/pex`, `src/pex_analysis`, `src/f4se` |
| SWF / AVM1 / AVM2 | SWF tags, timeline, text, bitmaps and edits; ABC assembler; AVM1 and AVM2 interpreters. | `src/swf`, `src/abc`, `src/avm1`, `src/avm2` |
| Graph | Papyrus graph tooling: document, lifter, linearizer, validator, compiler, node palette. | `src/graph` |
| Other | Quests (`src/quest`), SEQ files (`src/seq`), delta patch and list merge (`src/patch`), atomic file write (`src/io`). | as listed |

## 4. How C# and PluginEditTool call it

Layers, from the C++ side up:

1. The C++ library `fo4` holds the logic.
2. The `radicle_c` shared library exports a stable C ABI. Headers are `include/fo4/c_api.h` plus one `c_api_<area>.h` per area (`Radicle/include/fo4/`). `c_api.h` defines `RADICLE_ABI_VERSION` (87 on this checkout).
3. `Radicle/bindings/csharp/Radicle.Interop` is a .NET 8 wrapper (`Radicle/bindings/csharp/README.md`):
   - `RadicleNative.g.cs` is generated `DllImport` code. Do not edit it by hand.
   - The other `*.cs` files are safe wrappers with managed records, `IDisposable` handles and `RadicleException`.
4. `tools/generate_interop.py` regenerates `RadicleNative.g.cs` and `tests/abi_layout_probe.cpp` from the headers. Run it after any public C header change. The line to run is in `Radicle/bindings/csharp/README.md`.

Guards:

- The first call into the wrappers checks the native library's `radicle_abi_version` against the compiled number. A mismatch throws `RadicleAbiMismatchException` (`Radicle/bindings/csharp/README.md`).
- The CTest `radicle_csharp_interop_up_to_date` fails if the committed generated files no longer match the headers (same README).
- `AbiLayoutTests` compares C `sizeof`/`offsetof` with the managed layout (same README).
- Load the library with `RadicleLibrary.SetPath(fullPath)` or `RADICLE_NATIVE_PATH` (same README).

Callers:

- **PluginEditTool managed side:** `PluginEditTool/FO4RecordEditor.Core/FO4RecordEditor.Core.csproj` has a project reference to `Radicle.Interop`.
- **PluginEditTool native side:** `PluginEditTool/native/CMakeLists.txt` builds against the sibling `Radicle/` directory (`RADICLE_DIR`).
- **GardenEngine and other C# hosts:** the C# README names GardenEngine as a user of the wrapper.

## 5. Build and test

As documented in `Radicle/README.md`:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Run these from inside `Radicle/`. It needs C++23, CMake 3.25+ and zlib. Do not take this as permission to build locally. Check the repo's own rules and `Radicle/docs/TEST_STRATEGY.md` before you run anything.

CMake options (`Radicle/CMakeLists.txt`): `FO4_ENABLE_ASAN`, `FO4_ENABLE_UBSAN`, `RADICLE_BUILD_TOOLS`, `RADICLE_BUILD_TESTS`, `RADICLE_INSTALL`, `RADICLE_WARNINGS_AS_ERRORS`, and `RADICLE_CSHARP_TESTS` (off by default; needs the .NET 8 SDK).

Where tests go (`AGENTS.md`):

| Area | Suite |
|---|---|
| Radicle | `Radicle/tests` (one gtest binary, `fo4_tests`) |
| Radicle real-data checks | `*_real.cpp` (runs only on the Fallout 4 data runner) |
| PluginEditTool native | `PluginEditTool/native/tests` |
| PluginEditTool managed | `PluginEditTool/FO4RecordEditor.Tests` and `PluginEditTool/FO4RecordEditor.Core.Tests` |

Details:

- Python generator tests are registered as CTest entries in `Radicle/CMakeLists.txt` (for example `radicle_field_address_codegen`, `radicle_parity_matrix_unit`). Their sources are `Radicle/tools/test_*.py`.
- Real-data tests read installed game data and skip when the variable is unset. `FO4_DATA_DIR` is the game `Data` folder. Others are `FO4_PEX_CORPUS`, `FO4_PEX_ARCHIVES` and `FO4_GAME_BUILD` (`Radicle/docs/TEST_STRATEGY.md`).
- Real-data tests record the game build (OG 1.10.163, NG 1.10.980/984, AE 1.11.x). Exact-count assertions are pinned to AE 1.11.240.0 (`Radicle/docs/TEST_STRATEGY.md`).
- Acceptance for an editable surface is: parse, expose, edit, serialize, reparse. A byte-identical unmodified round trip proves preservation only (`Radicle/docs/TEST_STRATEGY.md`).
- Testing rules in `AGENTS.md`: prefer end-to-end tests; write the test before the fix; extend an existing suite instead of adding a file per bug; every new test says which regression it detects.
- Sanitizer builds use the ASAN/UBSAN options above.

Note: `Radicle/docs/TEST_STRATEGY.md` has "Planned (not this pass)" and "Corpus / oracle" sections that read as older plans. Treat them as history.

## 6. Current status

Only things confirmed in docs or code are listed. Docs can be older than the code. Check the parity matrix (`Radicle/docs/FO4_PARITY.md`) before you rely on a row.

**Documented as implemented** (`Radicle/README.md`, `Radicle/ROADMAP.md`):

- Generic lossless plugin container, `FormKey` identity, preservation writer, master remap.
- Schema-aware FormLink rewrite (`NAME`, `KWDA`, `CTDA`, `VMAD`, structured slots like `CNTO`, `LVLO`, `XTEL`).
- Load-order and conflict graph, stable C ABI, native PluginEditTool backend.
- BA2 GNRL and DX10 at v1, v7, v8, measured on installed archives (`Radicle/docs/BA2.md`).
- Load-order files (`plugins.txt`, `loadorder.txt`, `Fallout4.ccc`). The OG, AE and NG official tables are verified statically (`Radicle/README.md`).
- Localized string tables: strict validation, lookup, deterministic writing.
- `.bgsm` / `.bgem` byte-identical read/write.
- ROADMAP phases 1, 2, 3 and 6 are marked "shipped".

**Documented as partial or still open** (`Radicle/README.md`, `Radicle/ROADMAP.md`):

- Broad typed, schema-driven record authoring. The first authoring schema is `schema/fallout4/fields.json`; `REFR` `HealthPercent` / `XHLT` is the first scalar field on that path.
- Safe ESL compaction.
- NAVM / NAVI: stored read and indexed geometry edits exist. Cover, waypoint and NG layout are open (`Radicle/docs/OPEN_QUESTIONS.md` item 20b).
- Automatic resolution of 4-byte localized plugin fields. Deliberately separate until TES4 bit 7 semantics are settled.
- BA2 metadata that is not measured. See `Radicle/docs/BA2.md` and `OPEN_QUESTIONS.md` item 22.
- Texture decode: no encoding, no BC6H (`Radicle/docs/TEXTURES.md`).
- ROADMAP phase 4 (FO4IDE surface) is marked "now", phase 5 (schema completeness) "next", phase 7 (load order and conflicts) "later". The ROADMAP says its phases are a historical outline, not the current ledger. So these labels are stale in part: load order and conflict code already exists (see above).

**Not started:** There is no subsystem in the docs reviewed that is marked "not started". The only "not this pass" items are in `Radicle/docs/ARCHITECTURE.md` ("Non-goals for this pass": handwritten WEAP/ARMO/NPC_ decoders, a full writer, Skyrim/Starfield/Oblivion). That list is also dated, because BA2, Papyrus and a writer now exist in the tree.

**Unverified:**

- Completeness of NIF, Papyrus, SWF/AVM1/AVM2, graph and audio. The source folders exist and have tests (for example `Radicle/tests/test_sound.cpp`, `test_abc_real.cpp`), but I did not read a status doc for them or open the code to judge depth. Use `Radicle/docs/FO4_PARITY.md` and the per-topic docs (`NIF.md`, `SOUND.md`, `MATERIALS.md`) for that.
- Which operations in `FO4_PARITY.md` are READ only and which are fully editable. This page does not cover that file.
- Anything about CI results. This page makes no claim.

## 7. Where to read next

| You want | Read |
|---|---|
| Product framing, status prose | `Radicle/README.md`, `Radicle/ROADMAP.md` |
| Data model, identity, error policy | `Radicle/docs/ARCHITECTURE.md`, `Radicle/docs/FORMID_MODEL.md` |
| What can be read or edited per record type | `Radicle/docs/FO4_PARITY.md` |
| Test rules and real-data gates | `Radicle/docs/TEST_STRATEGY.md`, `AGENTS.md` |
| Unresolved questions | `Radicle/docs/OPEN_QUESTIONS.md` |
| Binary formats | `Radicle/docs/FORMAT_NOTES.md`, `BA2.md`, `NIF.md`, `MATERIALS.md`, `TEXTURES.md`, `SOUND.md`, `LOCALIZED_STRINGS.md`, `LOAD_ORDER_FILES.md` |
| Why a decision was made | `Radicle/docs/code-notes/Radicle.md` (long file; each entry is headed by the code it explains, such as `src/environment/sound.cpp:decode_sound_descriptor`) |
| Schema inputs | `Radicle/schema/fallout4/README.md` |
| C# wrapper and ABI checks | `Radicle/bindings/csharp/README.md` |
| Trying it on a plugin | `fo4inspect` examples in `Radicle/README.md`; sample plugins in `Radicle/testdata/` |
