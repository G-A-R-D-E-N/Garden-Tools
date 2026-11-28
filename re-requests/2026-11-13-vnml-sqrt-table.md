# RE request: the Creation Kit's sqrt table indexing in the VNML normalise helper

**Status 2026-11-26: unverified.** The previously cited evidence file is absent from this checkout, and section 1.6 of the findings questions the decompile's interpretation. Recheck questions 1-3 against verified evidence; a live CK save is still needed to compare against the editor's unquantised heights.

Requested 2026-11-13 by the Radicle terrain-normals work (branch `nomad/cell-exterior-relocate`, commit `5e42f31a8`, PR https://github.com/G-A-R-D-E-N/GardenTools/pull/1252).

## Why this is needed

`Radicle/src/forms/land_normals.cpp:sqrt_approx` copies the Creation Kit's approximate square root. The pseudo-code in `re-requests/2026-11-13-findings-all.md` section 1.6 was reconstructed from a decompile that the same file says misreads this software-pipelined loop. Two parts are not confirmed against disassembly or a live run:

1. How `idx` is chosen for the upper half (entries 128-255) of the 256-entry table.
2. Whether `edx = ((e<<22) - 0x1FC00000) & 0xff800000` and the final OR are exactly what the compiled code does for every exponent, including very small and very large `L`.

Measured earlier (UNVERIFIED against a live CK save; the figures are the interior rows of the table in `Radicle/docs/code-notes/radicle-forms.md`, the 40.5 % figure is not in findings 1.6): the port matched vanilla bytes at 40.5% (interior) against 38.1% for IEEE `sqrt`. That favours the table, but it cannot distinguish a wrong upper-half index from a right one when only a few bytes move by 1.

## Binaries

| Input | Version | Identity |
|---|---|---|
| `CreationKit.exe` (Steam) | 1.11.246.0 | Ghidra project `CKGhidra-CKRE`, program `CreationKit.exe` |

Functions (RVAs): `0x24d00c0` (normalise 289 float3, stride 12), `0x24d0070` / `FUN_14248c220` (table generator, 0x400 bytes, 256 entries), `0xd0f530` (the caller).

## Questions

1. Disassemble `0x24d00c0` and list the exact instruction sequence for one input vector: how the exponent and top mantissa bits of `L` select the table index, how the shift and mask build the exponent field, and how the table entry is combined. Quote the instructions, not the decompile.
2. Dump the 256 `uint32` entries the generator `FUN_14248c220` produces (or run it and read `DAT_147445730` after start-up). Confirm entries 0-127 are `mantissa(sqrtf(asfloat(0x3f800000 | i<<16)))` and entries 128-255 are `mantissa(sqrtf(asfloat(0x40000000 | i<<16)))`, and state which index range each exponent parity reads.
3. For `L = 1.0, 1.5, 2.0, 3.0, 4.0, 0.25, 1e-6, 1e6` give the output of the helper's reciprocal-sqrt step (one scalar each). Radicle's `sqrt_approx` can then be checked against these eight numbers byte for byte.

## Already known

- The per-face divisor remains unresolved; see findings 1.6.
- The table is not in the PE file; it is built at run time.

## Answer format

A short markdown reply under `re-requests/` with: the quoted disassembly of the index and exponent steps, the 256-entry table as hex, and the eight `L` to result pairs as floats and hex bits.

## What it unblocks

Removes the "unverified" status of `sqrt_approx` in `docs/code-notes/Radicle.md`, and lets a test pin eight reference values so a future change to the index or exponent maths fails immediately.
