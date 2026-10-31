# Previs read side (Phase 1)

Native-only. No CreationKit dependency. Patch-plugin-first and
skip-and-report live in `include/fo4/previs/detect.hpp` (plan D10/D11).

## Layouts (measured on vanilla)

- CDX: `bcdx` + u32 count + 16-byte rows `(kind, CELL, value, extra)`,
  sorted. `Fallout4.cdx` is 8 + 1460027 * 16.
- CSG: `bcsg` + u32 rows + u32 chunks + chunk table `(offset, size,
  absolute)` + 20-byte row table + payloads. `Fallout4 - Geometry.csg`
  first chunk offset equals 12 + 6841*8 + 32370*20. Only `bcsg` is
  accepted: the clean-mode `.psg` twin is unobserved in measured data,
  so a `.psg`-shaped file fails closed until its layout is measured.
- UVD: u32 magic `0xD6000012`, u32 CRC-32C over bytes [8, end), banner
  C string at +0xBC (Umbra 3.3.17), file size multiple of 16. Read and
  validate only; no tome writer (open D3, deferred to Phase 4). CRC is
  standard CRC-32C (Castagnoli, init/FXOR 0xFFFFFFFF); the real-data
  test pins this against every vanilla .uvd and must fail if the
  assumption is wrong. The parsed view borrows the file image (no
  second body copy).
- XCRI: u32 meshes, u32 reference words, meshes x u32 OC ids, words x
  u32 words as (REFR local id, OC id) pairs. Size is 8 + 4*M + 4*W with
  W even. Contradicts xEdit's 8-byte-pair declaration; all 4599 vanilla
  payloads match the word-pair form. One parser owns it
  (`parse_xcri_bytes`); the CELL layer (`decode_combined_index`) only
  resolves links through the Environment.

## Detection (section 1.3)

`diagnose_cell` flags: VISI without RVIS/.uvd/valid .uvd, XCRI without
CDX/CSG rows, stale PCMB/VISI stamps. A `false` presence flag means
"verified absent", never "not checked": each index answer carries a
`*_checked` flag, and a needed-but-unchecked index becomes a skip
reason with `skip_unsupported` instead of a false broken verdict.
`diagnose_load_order` builds these inputs for every winning CELL from a
loaded Environment plus caller-supplied index answers (CDX rows, the
opaque-row CSG cell set derived from OC NIF names, a .uvd issue lookup
so filesystem I/O stays with the caller) and per-file version dates:
max `version_control` over the winning CELL record and its same-plugin,
non-partial children, compared with `is_precalc_stale` (zero means
unknown on both sides). A CELL that fails to decode is reported broken
with the decode error instead of aborting the scan.
