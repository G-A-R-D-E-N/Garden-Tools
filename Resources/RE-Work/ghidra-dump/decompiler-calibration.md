# Ghidra Decompiler Calibration — Side-by-Side

Function: `TESObjectCELL::AllocateCellData()` at `tesobjectcell.cpp:0xaee`
Binary RVA: `0xd18510`, called as `thunk_FUN_140d18510(cell)`.

This is the cell-data allocator. It frees any existing cell-data block at
`cell+0x60`, then allocates a new one — 0x20 bytes for exterior cells,
0x88 bytes for interiors — and stores the pointer back at `cell+0x60`.

---

## Ghidra decompile (raw)

```c
void FUN_140d18510(longlong param_1)
{
  undefined8 *puVar1;
  longlong lVar2;
  undefined8 *puVar3;
  undefined1 local_18 [16];
  
  thunk_FUN_141ffb470(local_18,0x67,1,
                      "e:\\buildagent\\work\\4a2aa37ca4b2ffa3\\scripts\\_workspace\\code\\shared\\tesforms\\world\\tesobjectcell.cpp"
                      ,0xaee);
  lVar2 = *(longlong *)(param_1 + 0x60);
  puVar3 = (undefined8 *)0x0;
  if (lVar2 != 0) {
    if ((*(byte *)(param_1 + 0x50) & 1) == 0) {
      if ((*(uint *)(param_1 + 0x10) >> 0xe & 1) != 0) {
        *(undefined8 *)(lVar2 + 8) = 0;
        *(undefined8 *)(*(longlong *)(param_1 + 0x60) + 0x10) = 0;
        lVar2 = *(longlong *)(param_1 + 0x60);
      }
      if (lVar2 != 0) {
        thunk_FUN_140d3fec0(lVar2,1);
      }
    }
    else {
      FUN_140001c4e(lVar2,0x88);
    }
    *(undefined8 *)(param_1 + 0x60) = 0;
  }
  if ((*(byte *)(param_1 + 0x50) & 1) == 0) {
    if (DAT_143d1293c != 2) {
      thunk_FUN_141ff7ac0(&DAT_143d11e80,&DAT_143d1293c);
    }
    puVar1 = (undefined8 *)thunk_FUN_141ff6d30(&DAT_143d11e80,0x20,0);
    if (puVar1 != (undefined8 *)0x0) {
      *puVar1 = 0;
      puVar1[1] = 0;
      puVar1[2] = 0;
      *(undefined4 *)(puVar1 + 3) = 0;
      *(undefined2 *)((longlong)puVar1 + 0x1c) = 0;
      *(undefined1 *)((longlong)puVar1 + 0x1e) = 0;
      puVar3 = puVar1;
    }
  }
  else {
    if (DAT_143d1293c != 2) {
      thunk_FUN_141ff7ac0(&DAT_143d11e80,&DAT_143d1293c);
    }
    puVar1 = (undefined8 *)thunk_FUN_141ff6d30(&DAT_143d11e80,0x88,0,0);
    if (puVar1 != (undefined8 *)0x0) {
      thunk_FUN_140911970(puVar1 + 5);
      *puVar1 = 0;
      puVar1[1] = 0;
      puVar1[2] = 0;
      *(undefined4 *)(puVar1 + 3) = 0;
      *(undefined8 *)((longlong)puVar1 + 0x1c) = 0x3f800000;
      *(undefined4 *)((longlong)puVar1 + 0x24) = 0x3f800000;
      *(undefined4 *)(puVar1 + 9) = 0;
      *(undefined8 *)((longlong)puVar1 + 0x4c) = 0x3f800000;
      *(undefined4 *)((longlong)puVar1 + 0x54) = 0;
      puVar1[0xb] = 0x79f;
      puVar1[0xc] = 0x461c4000;
      puVar1[0x10] = 0;
      *(undefined4 *)(puVar1 + 0xd) = 0;
      *(undefined4 *)((longlong)puVar1 + 0x6c) = 0x3f800000;
      *(undefined4 *)(puVar1 + 0xe) = 0x3f800000;
      *(undefined4 *)((longlong)puVar1 + 0x74) = 0x3f800000;
      *(undefined4 *)(puVar1 + 0xf) = 0x3f800000;
      *(undefined4 *)((longlong)puVar1 + 0x7c) = 0x3f800000;
      puVar3 = puVar1;
    }
  }
  *(undefined8 **)(param_1 + 0x60) = puVar3;
  thunk_FUN_141ffb550(local_18);
  return;
}
```

---

## Reconstructed original C++

```cpp
// tesobjectcell.cpp, line 0xaee

void TESObjectCELL::AllocateCellData()
{
    ASSERT_LINE(0x67, "tesobjectcell.cpp", 0xaee);

    ExteriorCellData* oldData = this->cellData;          // +0x60
    ExteriorCellData* newData = nullptr;

    // Free the old block
    if (oldData) {
        if (this->IsExterior()) {                        // +0x50 & 1 == 0
            // Clear linked sub-object pointers if the form
            // flag 0x4000 ("has local data") is set
            if (this->flags & 0x4000) {                  // +0x10 >> 14 & 1
                oldData->linkedData1 = nullptr;           // +0x08
                this->cellData->linkedData2 = nullptr;    // +0x10
                oldData = this->cellData;                  // reload ptr
            }
            if (oldData) {
                DecRefLinkedData(oldData, 1);             // FUN_140d3fec0
            }
        } else {
            // Interior: free the 0x88-byte XCLL block
            HeapFree(oldData);                            // FUN_140001c4e(oldData, 0x88)
        }
        this->cellData = nullptr;
    }

    // Allocate the new block
    if (this->IsExterior()) {
        // Exterior: 0x20-byte block with grid coords + flags
        ScrapHeapLock lock;                              // thunk_FUN_141ff7ac0
        newData = (ExteriorCellData*)ScrapAlloc(0x20);   // FUN_141ff6d30
        if (newData) {
            memset(newData, 0, 0x20);
            // +0x00: gridX = 0
            // +0x04: gridY = 0
            // +0x08: linkedData1 = nullptr
            // +0x10: linkedData2 = nullptr
            // +0x18: field_18 = 0
            // +0x1C: field_1C = 0
            // +0x1E: field_1E = 0
        }
    } else {
        // Interior: 0x88-byte block with XCLL lighting data
        ScrapHeapLock lock;
        newData = (ExteriorCellData*)ScrapAlloc(0x88);
        if (newData) {
            // Initialize the XCLL lighting sub-block at +0x28
            InitLightingBlock(&newData->lighting);       // FUN_140911970(newData+0x28)
            newData->gridX = 0;                          // +0x00: int
            newData->gridY = 0;                          // +0x04: int
            // ... (interior block has 0 fields between +0x08 and +0x1C)
            newData->field_1C = 1.0f;                    // +0x1C: scale seed
            newData->field_24 = 1.0f;                    // +0x24: float
            newData->field_48 = 0;                       // +0x48: u32
            newData->field_4C = 1.0f;                    // +0x4C: scale seed
            newData->field_54 = 0;                       // +0x54: u32
            newData->validityMarker = 0x79F;             // +0x58: u64 marker
            newData->waterHeight = 10000.0f;             // +0x60: default water
            newData->pointer_80 = nullptr;               // +0x80: ptr
            newData->field_68 = 0;                       // +0x68: u32
            newData->field_6C = 1.0f;                    // +0x6C: float
            newData->field_70 = 1.0f;                    // +0x70: float
            newData->field_74 = 1.0f;                    // +0x74: float
            newData->field_78 = 1.0f;                    // +0x78: float
            newData->field_7C = 1.0f;                    // +0x7C: float
        }
    }

    this->cellData = newData;
}
```

---

## What Ghidra got right

1. **Control flow is exact.** Every `if`/`else` branch, the nesting of the
   flag-check inside the exterior path — all preserved 1:1.

2. **Offset arithmetic is correct.** Every `this+0x60`, `+0x50 & 1`,
   `+0x10 >> 14 & 1` is the genuine field access pattern. Ghidra's
   type-propagation correctly resolved these as struct offsets on `param_1`.

3. **Constants are literal.** `0x3f800000` (1.0f), `0x79f` (marker),
   `0x461c4000` (10000.0f), `0x20` and `0x88` block sizes — all preserved
   exactly as they appear in the binary.

4. **The `__FILE__` anchor is recoverable.** The `ASSERT` macro expands to
   a function call with file path and line hex — Ghidra shows the full
   build-agent path, which maps to `tesobjectcell.cpp:0xaee`.

5. **Call targets are named.** Even without symbols, Ghidra assigns
   `thunk_FUN_XXXXXXXX` names consistently — the call graph is traceable.

---

## What Ghidra loses

### 1. Type information (the biggest loss)

Ghidra sees the cell-data block as `undefined8*` — a typeless pointer.
Accesses use integer arithmetic: `puVar1[0xb]` means `*(u64*)(puVar1 + 0x58)`
because `puVar1` is `undefined8*` (8 bytes per element) and `0xb * 8 = 0x58`.

In the original code this would be `newData->validityMarker` — a named
field on a typed struct `ExteriorCellData`. Ghidra flattens every struct
access into raw offset arithmetic.

### 2. Member-access syntax

```c
// Ghidra:
*(undefined8 *)(*(longlong *)(param_1 + 0x60) + 0x10) = 0;

// Original:
this->cellData->linkedData2 = nullptr;
```

The triple dereference (`*(*(this+0x60)+0x10)`) becomes a nightmare to
read against the original `this->cellData->linkedData2`.

### 3. Variable names

Ghidra auto-generates generic names (`puVar1`, `lVar2`, `puVar3`). The
original has `oldData`, `newData`, and a named method `AllocateCellData`.

### 4. Bitfield and flag semantics

```c
// Ghidra:
if ((*(byte *)(param_1 + 0x50) & 1) == 0) {  // "is exterior?"

// Original:
if (this->IsExterior()) {  // likely a helper inline or macro
```

The flag `cell+0x50 & 1` tests the interior/exterior bit. Ghidra shows
the raw bit test; the original almost certainly had a named accessor.

### 5. Pointer reload artifact

```c
// Ghidra (note: reloads cellData AGAIN instead of using lVar2):
*(undefined8 *)(*(longlong *)(param_1 + 0x60) + 0x10) = 0;
lVar2 = *(longlong *)(param_1 + 0x60);

// Original:
this->cellData->linkedData2 = nullptr;
oldData = this->cellData;  // reload ptr after modification
```

Ghidra re-reads `param_1 + 0x60` through the global pointer rather than
using the local `lVar2`, then immediately re-loads `lVar2` from the same
address. This is because the compiler reloaded the pointer after the
store (volatile or aliasing concern) — Ghidra faithfully reproduces the
compiler's redundancy but the original code almost certainly wrote it
more cleanly.

---

## Severity scale for RE work

| issue | impact on analysis | workaround |
|---|---|---|
| **No struct types** | high — must manually annotate every offset | build a struct layout table from ctor (as §8.5) |
| **Raw flag tests** | medium — need to decode bit semantics | test against known values (0x50 bit 0 = interior flag) |
| **Generic names** | low — purely cosmetic | rename after understanding function role |
| **Pointer reloads** | low — just noise in the decompile | ignore redundant loads |
| **Control flow** | zero — 100% accurate | — |
| **Constants** | zero — exact | — |
| **Call graph** | zero — thunk names are stable | — |

---

## The practical rule

Treat the Ghidra decompile as the **compiler's output**, not the
programmer's input. Every `this+0x60` is a real field offset; every
`+0x50 & 1` is a real flag test; every `0x3f800000` is a real constant.
The decompile is *structural* ground truth — you can build a struct
layout from it and reconstruct member names by cross-referencing.

What you can't get from the decompile: the original variable names,
the class hierarchy (vtables tell you that), the member *types*
(beyond what the access pattern suggests), or the original formatting
and comments.