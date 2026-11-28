# RE request: ToggleRefID value for an enable-parent in a DLC or light plugin (.bto NiIntegerExtraData)

Date: 2026-11-13
Requested by: Radicle LODGen object writer (`Radicle/src/lod/object_candidates.cpp:runtime_form_id`, branch `nomad/lodgen-parity`, PR 1270)
Executable version and hash: no executable was opened. Measurements are from game data: all 802 vanilla `.bto` (Fallout4, DLCCoast, DLCNukaWorld archives) and the seven shipped ESMs.

## What is answered from data

- 43 `NiIntegerExtraData` blocks named `ToggleRefID` exist in the shipped `.bto`, all Commonwealth, 7 distinct values (0x187965, 0x2496c7, 0x184fd9, 0x23a406, 0x187964, 0x2469bc, 0x0b1810), every parent in Fallout4.esm, high byte 0.
- The tagging rule (child's enable-parent has record flag 0x100) reproduces all 27 vanilla toggled shapes exactly (27/27, no false positive).
- xEdit's public export (`wbLOD.pas:3309-3312`, written verbatim by xLODGen `LODApp.cs:1946-1951`) writes `XESP.FormID`, the in-file (raw) id of the parent as seen from the child's file. Radicle writes the runtime FormID (load-order index in the top byte; light plugins `0xFE` plus light index). Both give the same value for a Fallout4.esm parent, the only case shipped.
- No shipped DLC or ESL parent carries flag 0x100 with a LOD-bearing child (26 DLC parent children, none with a LOD mesh), so there is no vanilla sample of a non-zero high byte.

## Open (needs engine evidence)

1. When the engine loads a `.bto` shape with a `ToggleRefID` whose high byte is non-zero, does it resolve the value as a runtime FormID (load-order index, 0xFE light space), a file-local (raw) id, or something else?
2. If it is a runtime FormID, is it resolved against the active load order at load time (so the value must be written for the load order the player runs)?

## Answer format needed

One of "runtime FormID in the active load order", "file-local id of the child's plugin", or "other", with function or address evidence and the engine version (OG 1.10.163, NG 1.10.984, AE 1.11.240).

## What the answer unblocks

Which value Radicle writes for a flagged enable-parent defined in a DLC or light plugin. Until answered Radicle writes the runtime FormID (UNVERIFIED); every vanilla case is unaffected.
