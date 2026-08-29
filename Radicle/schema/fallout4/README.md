# Fallout 4 schema

FormLink locations, not full record layouts.

- 173 four-byte FormID subrecords from xEdit `wbFormIDCk` (remap only if payload is exactly 4 bytes). Tables: `include/fo4/schema/formid_subrecords.inc`
- Arrays: `KWDA`, `SPLO`, TES4 `ONAM`, `XLKR`
- Structured mixed payloads (`structured_formids.inc`): `CNTO` item at 0, `LVLO` reference at 4, `XTEL` door at 0, `XLOC` key at 4, `XESP` parent at 0, `COED` owner at 0, `DSTD` explosion at 8 and debris at 12, `XNDP` navmesh at 0
- QUST `ALLA` is one packed array of 8-byte `{Keyword FormID, Alias int32}` rows (xEdit `wbArray`, not a repeatable subrecord). Remap visits offsets `0, 8, 16, ...` and fails closed when the payload is not divisible by 8. CNTO Item uses the generated `sigBaseObjects` union (xEdit FO4, minus `NULL` which is not a record signature).
- `CTDA`: function table `ctda_functions.inc`; Use Global (type bit 2) remaps the comparison value; Run On = Reference remaps offset 24
- Light masters: TES4 SmallFile (bit 9) when known, else `.esl` filename. On-disk encode still uses the MAST ordinal and a 12-bit local. Not `0xFE`.
- `VMAD`: object properties (format 1 and 2), object arrays, nested structs, QUST alias objects. Int/float/bool/string properties are not FormIDs.

Do not add handwritten WEAP/ARMO classes here. Grow these tables instead.
