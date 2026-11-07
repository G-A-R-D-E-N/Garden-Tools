# Fallout 4 schema

FormLink locations, not full record layouts.

- 179 four-byte form ID subrecords: xEdit `wbFormIDCk` signatures plus hand-added `EITM`, `CS2K`, `CS2D`, `HEAD`, `MDOB` and `PLYR` (remap only if payload is exactly 4 bytes). The list is hand-maintained (no generator in the repo) and is not complete: form IDs from `wbDefinitionsCommon.pas` helpers and computed signatures are not all listed. Tables: `include/fo4/schema/formid_subrecords.inc`
- Arrays: `KWDA`, `SPLO`, TES4 `ONAM`, `XLKR`, `APPR`
- Record-qualified packed arrays (`form_link_mixed.cpp:packed_formid_array_offsets`): ARMO and ARMA `MODL` (a path string in other records), NPC_ `TPTA` (13 form IDs) and OVIS `INDX` (one STAT form ID per entry)
- Structured mixed payloads (`structured_formids.inc`): `CNTO` item at 0, `LVLO` reference at 4, `XTEL` door at 0, `XLOC` key at 4, `XESP` parent at 0, `COED` owner at 0, `DSTD` explosion at 8 and debris at 12, `XNDP` navmesh at 0, `ATKD` attack spell at 8
- QUST `ALLA` is one packed array of 8-byte `{Keyword FormID, Alias int32}` rows (xEdit `wbArray`, not a repeatable subrecord). Remap visits offsets `0, 8, 16, ...` and fails closed when the payload is not divisible by 8. CNTO Item uses the generated `sigBaseObjects` union (xEdit FO4, minus `NULL` which is not a record signature).
- `CTDA`: function table `ctda_functions.inc`; Use Global (type bit 2) remaps the comparison value; Run On = Reference remaps offset 24
- Light masters: TES4 SmallFile (bit 9) when known, else `.esl` filename. On-disk encode still uses the MAST ordinal and a 12-bit local. Not `0xFE`.
- LVLI/LVLN: LVLG (use global) and LVSG (epic loot chance, LVLI only) are FormLink fields; LVLD, LVLM and LVLF are scalars. LVLO entries are remapped by `structured_formids.inc`. LLKC has rows: `FilterKeyword` (KYWD link, offset 0) and `FilterChance` (u32 scalar, offset 4), both stride 8. LLCT is not a scalar because it is derived from the entry count.
- `VMAD`: object properties (format 1 and 2), object arrays, nested structs, QUST alias objects. Int/float/bool/string properties are not FormIDs.

Do not add handwritten WEAP/ARMO classes here. Grow these tables instead.
