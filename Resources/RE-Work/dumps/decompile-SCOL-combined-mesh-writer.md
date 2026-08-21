# SCOL combined-mesh writer and `_OC` NIF path chain

This note labels the Creation Kit 1.11.137.0 code reached by the two SCOL
format-string xrefs supplied for this investigation:

- `0x140c1d1c2` — inside `FUN_140c1d140`
- `0x140c1f02f` — inside `FUN_140c1efe0`

The important correction is that these two instructions are **path-formatting
sites**, not the byte serializer itself. They both select the same rdata format
string and therefore identify the static-collection (`SCOL`) output family.
The `_OC` geometry path is a related, separately visible writer path in
`FUN_140d4f160` and the static-collection routine beginning at `FUN_14048a890`.

## 1. SCOL path helper (`FUN_140c1efe0`)

The second xref is the compact helper used through the hot-patch thunk at
`0x140082d30`:

```text
FUN_140c1efe0(this, output, outputCapacity)
```

The register/data flow is:

1. `FUN_140059912(this, 0)` obtains the owning/associated record.
2. If that record exists, `FUN_1400369df(record)` selects the `0x0fff` ID
   mask for the special owner case; otherwise the mask is `0x00ffffff`.
3. The low 24 (or special low 12) bits of `this + 0x14` are formatted.
4. `0x14000415b` writes the result into the caller's buffer with the rdata
   format at VA `0x143075570`:

```text
SCOL\\%s\\CM%08X.NIF
```

The exact byte string is at **VA `0x143075570`**. The `%s` argument is the
associated collection/group name at `record + 0x78`, or the static fallback
pointer at `0x142f97bfc` when no associated record is available. The helper
returns the generated path through its output buffer; it does not construct a
NiStream and does not call the NiStream slot-6 disk writer.

`FUN_140c1d140` contains the same `FUN_140059912`/ID-mask/path sequence in its
larger collection-processing routine. Its xref at `0x140c1d1c2` passes:

```text
rcx = local output buffer
rdx = 0x104
r8  = "SCOL\\%s\\CM%08X.NIF"
r9  = collection-name pointer
[rsp+0x20] = masked form ID
```

After formatting, it invokes the owning object's virtual save/dispatch slot at
`this + 0xa8`, vtable offset `0xb8`, then walks the collection state at
`this + 0xf8`. The latter is an array of 0x18-byte entries and is used to
resolve/register the component objects; it is not itself the NIF wire layout.

## 2. `_OC` and `_Physics` names (`FUN_140d4f160`)

The per-cell combined-output routine beginning at `0x140d4f160` exposes the
actual `_OC` pair. Its setup is byte-level and independent of the older generic
summary in doc 09:

```text
base = "Meshes\\PreCombined"
if owner has a collection group:
    base += "\\" + owner->groupName      // groupName is owner + 0x78

for each 32-bit entry in owner[this + 0x58]->array:
    path = sprintf_s(
        "%s\\%08X_%08X_OC.NIF",
        base,
        this->formID & 0x00ffffff,
        entry
    )

physicsPath = sprintf_s(
    "%s\\%08X_Physics.NIF",
    base,
    this->formID & 0x00ffffff
)
```

The relevant fields and instructions are:

| evidence | meaning |
|---|---|
| `0x140d4f17e`: `rdi = [this+0x58]` | combined-output state/entry owner |
| `0x140d4f1b2`: `FUN_140059912(this, 0)` | associated owner lookup |
| `0x140d4f1d0`–`0x140d4f1ed` | initialize `Meshes\\PreCombined`, obtain `this+0x14 & 0xffffff` |
| `0x140d4f218`–`0x140d4f229` | append the optional `\\` and owner group string |
| `0x140d4f22f`: `eax = [rdi+0x60]` | number of 4-byte combined entries |
| `0x140d4f23a`: `rbx = [rdi+0x50]` | entry-array start |
| `0x140d4f264`: `eax = [rbx]` | second `%08X` component ID |
| `0x140d4f272` | rdata format `%s\\%08X_%08X_OC.NIF` |
| `0x140d4f2bf` | rdata format `%s\\%08X_Physics.NIF` |
| `0x140d4f2a4` / `0x140d4f2f9` | pass each generated path to the output/registration object |

The literal strings are adjacent in `.rdata` at `0x142fb6ed0` and
`0x142fb6ee8`:

```text
0x142fb6eb8  Meshes\\PreCombined
0x142fb6ed0  %s\\%08X_%08X_OC.NIF
0x142fb6ee8  %s\\%08X_Physics.NIF
```

The same pair is used by the static-collection routine at `0x14048b70b` and
`0x14048b896`. That routine formats one `_OC` path per 4-byte collection
entry, creates the temporary smart/RAII path object (`0x140005bc8` through
its thunk/body), dispatches it to the collection manager, and repeats for the
physics companion. It also performs the expected reference-count cleanup.

### Naming consequence

For a cell/form ID `01012345`, entry `0000002A`, and no optional group, the
confirmed names are:

```text
Data\\Meshes\\PreCombined\\01012345_0000002A_OC.NIF
Data\\Meshes\\PreCombined\\01012345_Physics.NIF
```

When a static collection group is active, the group directory is inserted
between `PreCombined` and the file name. The `SCOL\\<group>\\CM%08X.NIF`
family is the static-collection mesh naming convention; it should not be
collapsed into the cell `_OC` naming convention even though both paths are
built by the same combined-mesh subsystem.

## 3. What is and is not the NIF geometry writer here

The path routines hand the generated path to a collection/output object. In the
observed windows, the calls at `0x140d4f2a4`/`0x140d4f2f9` and the corresponding
`0x14048b7xx` sequence are dispatch/registration calls; there is no direct
`NiStream` vftable-slot-6 call at those exact instructions. Therefore the
following claims are confirmed:

- the output category and exact `_OC`/`_Physics` names;
- the per-entry ID ordering in `_OC` names;
- the 24-bit form-ID truncation used for the cell/owner component;
- the optional collection-group directory;
- one geometry path per entry, followed by one physics companion path.

The exact `NiNode`/`NiTriShape`/`BSGeometry` root composition for an individual
`_OC` file is **not** encoded by the format-string helper. It is selected by
the delegated collection/output object after the combined geometry has been
assembled. This avoids attributing the path-builder's 0x18-byte bookkeeping
entries to NIF blocks.

## 4. Relationship to the confirmed NiStream save chain

Once the delegated output object has a root scene graph, its disk write follows
the CK NiStream chain (cross-build contract in the local Elric symbol
cross-reference, `ghidra-dump/elric-pdb-symbols.txt` §4b–§4e):

1. Stack/heap NiStream is initialized with vftable `0x142fa4780`.
2. Child objects are registered through the CK's `AddObject`-equivalent
   vtable slot `0x1e0`.
3. Save preparation registers streamables and discovers RTTI block names.
4. The slot-6 wrapper `0x140097e92` → `0x1425fe750` opens the target path,
   writes the stream, closes the file, and returns success.
5. The SaveStream core performs the three per-object passes:
   `RegisterStreamables`, `SaveBinary`, and link/post-link bookkeeping.

The byte layout applicable to the `_OC` NIF is consequently the normal FO4 NIF
layout, not an `_OC`-specific header:

```text
Gamebryo File Format, Version 20.2.0.7\\n
u32 0x14020007
u8  endian = 1
u32 userVersion = 12
u32 numRoots
u32 BS uiCurrVersion (0x82)
BS header strings
u16 numBlockTypes
(u32 length + bytes) block-type names
u16 typeIndex[numRoots]
u32 objectSize[numRoots]
fixed-string table
object groups
root/object SaveBinary payloads
u32 linkCount
u32 link IDs
```

The block-type names and indices are dynamic: `SaveRTTI` collects the actual
runtime RTTI classes in the combined mesh, so the path alone cannot prove the
class sequence. The likely geometry-side classes are the normal FO4 Ni/BS
scene-graph classes; `bhkNiTriStripsShape` is the PDB-confirmed physics-side
shape family for `_Physics`, but no claim is made here that every `_OC` file
contains Havok blocks.

This distinction is important for byte parsers: use the `_OC` suffix only to
select the geometry output family, then parse the NIF header/object tables and
RTTI names. Do not expect `_OC` to change the header magic, string encoding,
link-ID encoding, or object-size-table rules.

## 5. Evidence and open target

Primary evidence:

- `CreationKit.exe` `.rdata` `0x143075570`: `SCOL\\%s\\CM%08X.NIF`;
- `.rdata` `0x142fb6eb8`–`0x142fb6efb`: `Meshes\\PreCombined`, `_OC`, and
  `_Physics` formats;
- `FUN_140c1d140`, xref `0x140c1d1c2`;
- `FUN_140c1efe0`, xref `0x140c1f02f`;
- `FUN_140d4f160`, xrefs to both `_OC` and `_Physics` formats;
- static-collection routine `FUN_14048a890`, xrefs `0x14048b70b` and
  `0x14048b896`;
- `ghidra-dump/nifwriter-facegen-0x140ab8a60.asm` and the NiStream save-chain
  section of the local Elric symbol cross-reference.

The next payload-level target is the delegated output object's call site that
constructs the combined `NiNode`/geometry root before invoking the common
NiStream save wrapper. A real `_OC.NIF` sample would let the dynamic RTTI table
be correlated with that call chain and turn the generic block sequence above
into a class-by-class geometry spec.
