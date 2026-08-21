# Radicle

Native Fallout 4 plugin SDK. The first root a plugin puts down, and a radiation pun.

C++ namespace is still `fo4`. The library is meant to replace Mutagen inside FO4IDE / PluginEditTool. It is not a line-for-line Mutagen port. It is an independent reader/writer grounded in:

1. Fallout 4 runtime behavior
2. Creation Kit behavior
3. local reverse-engineering in `CKRE` and `ReverseEngineering`
4. xEdit where useful
5. Mutagen as a compatibility oracle, not a specification

## Status

Generic lossless plugin container, FormKey identity, preservation writer, master remap, and FormLink rewrite (`NAME`/`KWDA`/`CTDA`/`VMAD` plus structured slots such as `CNTO`/`LVLO`/`XTEL`). Light plugins are TES4 SmallFile when known, otherwise `.esl` filename. Inspect CLI and GoogleTests.

Not yet: typed WEAP/ARMO classes, FO4IDE C adapter, BA2, conflict graph.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Requires C++23, CMake 3.25+, zlib.

## Inspect

```bash
fo4inspect testdata/FW_HeatherSurvivalPatch.esp
fo4inspect header testdata/FW_HeatherSurvivalPatch.esp
fo4inspect masters testdata/FW_HeatherSurvivalPatch.esp
fo4inspect groups testdata/FW_HeatherSurvivalPatch.esp
fo4inspect records testdata/FW_HeatherSurvivalPatch.esp
fo4inspect record testdata/FW_HeatherSurvivalPatch.esp 01000800
fo4inspect raw testdata/FW_HeatherSurvivalPatch.esp 01000800
fo4inspect write testdata/FW_HeatherSurvivalPatch.esp /tmp/heather-copy.esp
fo4inspect formlinks testdata/FW_HeatherSurvivalPatch.esp
```

JSON is the default output.

## Canonical identity

Raw load-order FormIDs such as `0x5600009A` are never the library's identity for a reference.

They resolve to a symbolic `FormKey`:

```text
00009A:RadiationOverhaul.esp
```

On write, the destination plugin's master table reconstructs the raw on-disk FormID.

See `docs/FORMID_MODEL.md`.

## Docs

- `docs/ARCHITECTURE.md`
- `docs/RE_INVENTORY.md`
- `docs/FORMAT_NOTES.md`
- `docs/FORMID_MODEL.md`
- `docs/TEST_STRATEGY.md`
- `docs/OPEN_QUESTIONS.md`
