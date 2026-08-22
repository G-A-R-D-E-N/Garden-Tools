# 12 — Record-Definition Dispatch Table

CreationKit.exe 1.11.137.0. This doc answers: *what records can the CK load and save, and how does the binary 4CC dispatch to a class?*

There are **three tables** that together form the record-definition dispatch, plus the **RTTI class map** that ties indices to classes.

---

## 1. `formEnumString[]` — the definitive record enum (`.data` RVA `0x3b9a540`)

The authoritative 159-entry index→4CC table. It is **validated at runtime** by the `TESForm` constructor (`FUN_1408bc610`, source `tesform.cpp`), which scans it once:

```
formEnumString[ %u ].cFormID in TESForm.cpp is out of order.
formEnumString[ %u ] and formEnumString[ %u ] have the same iFormString %s
You must fix the problems in TESForm.cpp to run this game.
```

**Layout** — 24-byte records, 159 entries, contiguous in `.data`:

| offset | size | meaning |
|-------:|-----:|---------|
| +0x00  |  1   | enum index (`iFormString` ordinal, must equal array position) |
| +0x01  |  7   | padding |
| +0x08  |  8   | pointer to the ASCII 4CC literal (`.rdata`) |
| +0x10  |  4   | packed 4CC (little-endian uint32; written at runtime) |

The packed code is built in the validator: `((c[3]<<8|c[2])<<8|c[1])<<8|c[0]` — i.e. `'TES4'` → `0x34534554` little-endian.

**Index 0 is `NONE`** (no record). The 158 real codes occupy indices 1–158. Full table in `record-definition-table.txt` (generated dump); the 4CC list:

```
NONE TES4 GRUP GMST KYWD LCRT AACT TRNS CMPO TXST MICN GLOB DMGT CLAS FACT
HDPT EYES RACE SOUN ASPC SKIL MGEF SCPT LTEX ENCH SPEL SCRL ACTI TACT ARMO
BOOK CONT DOOR INGR LIGH MISC STAT SCOL MSTT GRAS TREE FLOR FURN WEAP AMMO
NPC_ LVLN KEYM ALCH IDLM NOTE PROJ HAZD BNDS SLGM TERM LVLI WTHR CLMT SPGD
RFCT REGN NAVI CELL REFR ACHR PMIS PARW PGRE PBEA PFLA PCON PBAR PHZD WRLD
LAND NAVM TLOD DIAL INFO QUST IDLE PACK CSTY LSCR LVSP ANIO WATR EFSH TOFT
EXPL DEBR IMGS IMAD FLST PERK BPTD ADDN AVIF CAMS CPTH VTYP MATT IPCT IPDS
ARMA ECZN LCTN MESG RGDL DOBJ DFOB LGTM MUSC FSTP FSTS SMBN SMQN SMEN DLBR
MUST DLVW WOOP SHOU EQUP RELA SCEN ASTP OTFT ARTO MATO MOVT SNDR DUAL SNCT
SOPM COLL CLFM REVB PKIN RFGP AMDL LAYR COBJ OMOD MSWP ZOOM INNR KSSM AECH
SCCO AORU SCSN STAG NOCM LENS LSPR GDRY OVIS
```

Note the index values: `RACE`=17, `REFR`=64, `CELL`=63, `WRLD`=74, `LAND`=75, `NAVM`=76, `TES4`=1.

## 2. The file-load table (`.rdata` RVA `0x300d478`)

157 entries × 8 bytes, each `{u32 0, u32 4CC}` (4CC in the high dword, little-endian packed). Source `tesfilecontainer.cpp`. **This is the load order for plugin files** — the set of records the file reader accepts.

Relationship to `formEnumString`: **identical sequence minus index 0 (`NONE`) and minus `RACE`**. `RACE` (17) is present in the enum but **not file-loadable** — it's not in this table (its literal still exists at `.rdata` RVA `0x2fb9aec`, referenced by the enum). Everything else lines up 1:1:

```
enum idx 1..16  = TES4..EYES   (table idx 0..15)
enum idx 18..158 = SOUN..OVIS   (table idx 16..156)
```

A byte-scan found no code references into the bulk of this table — the loader walks it by pointer (`FUN_1408bc480`-family), same pointer-scrubbing pattern as the RTTI graph.

## 3. The runtime factory array (`.data` RVA `0x3d49cd0`)

159 slots × 8 bytes (0x4f8 total) — **the dispatch table proper**: `slot[enumIndex] = factory vftable` for every registered record class. Lazily zeroed on first use (guard byte `DAT_143d49cc8`).

Accessor family (all thunked through `.text` RVA 0x13ef3 / 0x1694b):

| function | role |
|----------|------|
| `FUN_1408bc350` | **setter** — `factory[idx] = vftable` (bounds-checked `< 0x9f`) |
| `FUN_1408bc2f0` | **getter** — `return factory[idx]` |
| `FUN_1408bc3b0` | clear slot |
| `FUN_1408bc410` / `FUN_1408bc480` | **visitors** — iterate all 159 slots, call `vftable[1]` per non-null entry (`ObjectFactoryVisitor`) |

## 4. Registration — static initializers

Each record class registers itself in a static initializer. Confirmed examples (decompiles in `ghidra-dump/`):

| initializer | call | class | idx | 4CC |
|-------------|------|-------|----:|-----|
| `FUN_140153760` | `thunk_FUN_1408bc350(0x97, ...)` | `ConcreteObjectFormFactory<BGSAttractionRule,151,120,4>` | 151 | `AORU` |
| `FUN_140154780` | `thunk_FUN_1408bc350(0x96, ...)` | — | 150 | `SCCO` |
| `FUN_140154840` | `thunk_FUN_1408bc350(0x95, ...)` | `ConcreteObjectFormFactory<BGSAudioEffectChain,149,117,8>` | 149 | `AECH` |
| `FUN_140154cd0` | `thunk_FUN_1408bc350(8, ...)` | `ConcreteObjectFormFactory<BGSComponent,8,109,4>` | 8 | `CMPO` |
| `FUN_140155160` | `thunk_FUN_1408bc350(0xc, ...)` | `ConcreteObjectFormFactory<BGSDamageType,12,118,4>` | 12 | `DMGT` |
| `FUN_140155260` | `thunk_FUN_1408bc350(0x6f, ...)` | — | 111 | `DFOB` |

Each also registers an `atexit` teardown. Template arguments decode as `<class, enumIndex, size?, flags?>`.

## 5. RTTI class map — index → class (cross-validated)

The mangled RTTI names embed the enum index: `ConcreteObjectFormFactory@V<Class>@@$0<hex>@...` where the hex uses per-nibble letters by position — **A=0 … P=15** (so `$0JH` = 0x97 = 151 = AORU, `$0HI` = 0x78 = 120, `$0DP` = 0x3F = 63 = CELL, `$03` = 3). An earlier draft read this as "A=0..J=9" and produced a systematic off-by-one for indices ≥ 0xA0's nibbles — the factory *registration call* (`thunk_FUN_1408bc350(idx, ...)`) is the ground truth, and doc 23 re-validated the CELL/WRLD/LAND rows against it.

Decoding all 119 `ConcreteObjectFormFactory` / `ConcreteFormFactory` RTTI names and joining on enum index yields **83 class bindings with zero mismatches** against `formEnumString`. Highlights:

| idx | 4CC | class | idx | 4CC | class |
|----:|-----|-------|----:|-----|-------|
| 3 | GMST | BGSKeyword | 74 | WRLD | TESWorldSpace (see doc 23) |
| 4 | KYWD | BGSLocationRefType | 75 | LAND | TESObjectLAND (see doc 23) |
| 16 | EYES | TESEyes | 80 | QUST | TESQuest |
| 17 | RACE | TESRace | 88 | EFSH | TESEffectShader |
| 18 | SOUN | TESSound | 96 | BPTD | BGSBodyPartData |
| 21 | MGEF | EffectSetting | 97 | ADDN | BGSAddonNode |
| 23 | LTEX | TESLandTexture | 98 | AVIF | ActorValueInfo |
| 36 | STAT | TESObjectSTAT | 105 | ARMA | TESObjectARMA |
| 48 | ALCH | AlchemyItem | 128 | OTFT | BGSOutfit |
| 50 | NOTE | BGSNote | 134 | SNCT | BGSSoundCategory |
| 55 | TERM | BGSTerminal | 137 | CLFM | BGSColorForm |
| 57 | WTHR | TESWeather | 149 | AECH | BGSAudioEffectChain |
| 63 | CELL | TESObjectCELL (see doc 23) | 151 | AORU | BGSAttractionRule |

Full join in `record-definition-table.txt`.

## 6. Load vs save surface

- **Load** (plugin → form): file reader (`tesfilecontainer.cpp`) accepts the **157-code table**; `RACE` is enum-known but not file-loaded. On a 4CC hit, the loader creates via the factory array slot for the enum index.
- **Save** (form → plugin): the factory array's 159 slots are the full in-memory type surface (incl. `NONE`); the editor-side `TES*_editor` classes (source list in `07-SOURCE-TREE.md`, `code\construction set\tesforms\*_editor.cpp`) provide the per-record UI. `RACE` is handled by the editor without a file representation.
- The 6 confirmed static registrations + the 83 RTTI bindings cover the classes with dedicated factories; the remaining enum slots have no `ConcreteFormFactory` RTTI class (e.g. `GRUP`, `REFR` handled by generic `TESForm` paths).

## Artifacts

- `record-definition-table.txt` — full 159-row index→4CC→class dump
- `ghidra-dump/decompile-TESFormCtor-FUN_1408bc610.txt` — the enum validator
- `ghidra-dump/decompile-FactoryVisitor-FUN_1408bc480.txt` — the 159-slot walker
- `ghidra-dump/decompile-Register-BGSAttractionRule-0x153760.txt` — a registration initializer
