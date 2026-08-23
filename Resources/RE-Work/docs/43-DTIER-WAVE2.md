# 43 - D-tier wave 2: CONT / DOOR / TXST / FLST / CAMS / LIGH / MSTT / VTYP / IPCT / TERM

Continues doc 42. Loaders via vtable-slot walk; wave-2 decompiles in
`ghidra-dump/decompile-dtier-wave2.txt`.

## 1. LoadRecord locations

| type | class | LoadRecord | size | literal tags |
|---|---|---|---|---|
| CONT | TESObjectCONT | `FUN_140c74be0` | (wave-1 batch) | container family |
| DOOR | TESObjectDOOR | `FUN_140c77160` | (wave-1 batch) | door family |
| TXST | BGSTextureSet | `FUN_140bc93f0` | (wave-1 batch) | texture-set family |
| FLST | BGSListForm | `FUN_140b97e60` | (wave-1 batch) | form-list |
| CAMS | BGSCameraShot | `FUN_140b7ff40` | (wave-1 batch) | camera-shot family |
| LIGH | TESObjectLIGH | **`FUN_140c49db0`** | 1435 B | 35 incl `NAM0 WGDR XGDR` + full DEST/DSTD destruction |
| MSTT | BGSMovableStatic | **`FUN_140c0d1b0`** | 958 B | 25 incl `QRPS PRPS` |
| VTYP | BGSVoiceType | **`FUN_140a56310`** | 444 B | bare: `EDID DNAM` + template family |
| IPCT | BGSImpactData | **`FUN_140b90c90`** | 1169 B | 22 incl `DODT ENAM NAM1/2/3 WMAD` |
| TERM | BGSTerminal | **`FUN_140457aa0`** | 2409 B | 39 - terminal text family (`CTXT ITXT BTXT TNAM UNAM VNAM WNAM ISIZ BSIZ ITID JTID RNAM ANAM BNAM CTDA EESC`) |

Method note: five of these RTTI name-strings were never defined as data
in the Ghidra listing, so the string-search path failed; `RttiWalk.java`
(new script) takes raw typeinfo addresses from the re-mcp string index
instead. Also: Ghidra headless OSGi bundles are per script DIRECTORY -
one broken .java poisons compilation for every script in that folder
(lesson recorded after an hour-long outage).

Secondary-vtable observation: slot 24 of many *secondary* vftables
resolves to a shared default (`FUN_1408e3df0`) - component interfaces,
not forms.

## 2. Writer notes

* LIGH carries the same DEST/DSTD destruction block as C-tier objects
  plus `NAM0` (shadow data) and GDR-family globals.
* TERM is the largest non-inventory loader so far; its screen/item text
  subrecords (CTXT/ITXT pairs with ISIZ/BSIZ size prefixes and ITID/
  JTID links) form a self-contained mini-format worth its own pass if
  Radicle ever emits terminals.
* VTYP is effectively EDID+DNAM - trivial to emit.
* MSTT = STAT minus MNAM, plus QRPS; both share the destruction block.
