# Duplicate `INISettingCollection` descriptors

This note resolves the nine `bKey:Section` names that occur twice in the
Creation Kit 1.11.137.0 descriptor census. The two records are **not** one
record overwritten by the other: each is a separate 0x18-byte `SettingT`
object with its own address, default/value slot, registration initializer, and
notify key.

## 1. What “two arrays” means in the binary

The static records are laid out in several linker-concatenated `.data` runs;
the source-level C++ array names are not present. The reliable array identity
is therefore the record address plus its CRT initializer. Every initializer has
the same shape:

```asm
lea   rax, [SettingT<...>::vftable]
mov   [record], rax
call  collection_getter
lea   rdx, [record]
mov   rcx, rax
mov   r8,  [rax]
call  qword ptr [r8+0x10]       ; collection AddSetting(record)
lea   rcx, [record]
tailcall Setting::ctor
```

There are two registration collections in this build:

| label | getter/thunk | storage | collection-object vftable | record vftable |
|---|---|---|---|---|
| **main INI** | `0x14002c539` → `FUN_1402ccbf0` | `DAT_14742c170` | `0x142f8e088` | `0x142f8dce8` |
| **preferences INI** | `0x140069560` → `FUN_14033ab80` | `DAT_143f2f9e8` | `0x142f959d0` | `0x142f959b0` |

The collection getter lazily allocates a collection object and installs its
own collection vftable. The descriptor’s vftable is separate; it is written to
`record+0x00` by the initializer. Thus the two repeated descriptors can be in
the same collection (the usual case) without sharing an object identity.

## 2. Complete duplicate map

`init` is the beginning of the per-record static initializer. The data offset
is relative to `.data` RVA base `0x143b87000`.

| key | first record / init | second record / init | target | data runs / array evidence |
|---|---|---|---|---|
| `fMouseScale:General` | `0x143b901e0` / `0x142e8c9c0` | `0x143b901f8` / `0x142e8ca10` | main INI | `+0x91e0`, `+0x91f8`; contiguous General-settings run |
| `fMinMouseScale:General` | `0x143b90210` / `0x142e8c920` | `0x143b90228` / `0x142e8c970` | main INI | `+0x9210`, `+0x9228`; same run as `fMouseScale` |
| `fMaxDistanceMoved:Pathfinding` | `0x143c590f8` / `0x142f332e0` | `0x143c59110` / `0x142f33160` | main INI | `+0xd20f8`, `+0xd2110`; Pathfinding run `+0xd20e0..+0xd21d0` |
| `sResourcePrefixList:Archive` | `0x143b92098` / `0x142e935c0` | `0x143b999f0` / `0x142e9fc80` | main INI | `+0xb098` and `+0x129f0`; two separate linker data runs |
| `strPluginsFileHeader:General` | `0x143b87cd0` / `0x142e82c10` | `0x143b99c78` / `0x142e9fd50` | main INI | `+0xcd0` and `+0x12c78`; two separate linker data runs |
| `uMaterialCount:BudgetCaps` | `0x143b9d8b0` / `0x142ea3ea0` | `0x143b9da48` / `0x142ea3ef0` | main INI | `+0x168b0`, `+0x16a48`; explicit pair in `PTR_PTR_143b9d490` |
| `bSkipInitializationFlows:MESSAGES` | `0x143b87ca0` / `0x142e82b70` | `0x143b92608` / `0x142e92540` | main INI | `+0xca0` and `+0xb608`; two separate message/settings runs |
| `bSkipProgramFlows:MESSAGES` | `0x143b87c88` / `0x142e82bc0` | `0x143b925f0` / `0x142e92540` | main INI | `+0xc88` and `+0xb5f0`; two separate message/settings runs |
| `fTessFactorMaxDistanceScale:Display` | `0x143c5ee38` / `0x142f41510` | `0x143c5ee50` / `0x142f414b0` | preferences INI | `+0xd7e38`, `+0xd7e50`; Display preferences run |

The two `bSkip*` rows share the second initializer block at
`0x142e92400..0x142e92573`; the two records are separate functions within that
block. The same is true of the General, Pathfinding, BudgetCaps, and Display
runs: adjacent records do not imply a single C++ object or a single key.

### BudgetCaps pair is explicitly enumerated

The BudgetCaps read-all pointer table begins at `0x143b9d490`. Its 40-byte
entries contain the exterior and interior record pointers. The entry at
`0x143b9d700` is:

```text
+0x00  0x143b9d8b0   ; uMaterialCount (first descriptor)
+0x08  0x143b9da48   ; uMaterialCount (second descriptor)
+0x10  0x00000000
+0x18  0x00000000
+0x20  0x00000101     ; following table metadata
```

`FUN_14092b990` walks that pointer table and calls the collection read virtual
on both pointers. This is direct proof that both records are intentionally
read, rather than one being dead padding.

## 3. Why the duplicate names do not collide during registration

`FUN_1425c75a0` (the embedded `BSTScatterTable` insert) hashes and compares the
**key argument pointer** (`*param_2`). The static initializer passes the
record address to collection vftable slot `+0x10`; it does not pass the
`record+0x10` string pointer as the table key. `FUN_1425c6800` and
`FUN_1425c6b90` use the same record-address identity for callback registration
and notification.

Consequently:

- `0x143b901e0` and `0x143b901f8` are two scatter-table entries;
- the table never compares `"fMouseScale:General"` while inserting them;
- no duplicate-key replacement occurs at `AddSetting` time;
- a callback registered for one record does not automatically receive updates
  for the other record.

This is a pointer-identity collision model, not an INI-name uniqueness model.

## 4. Read behavior

`FUN_1425c7f90(coll, rec)` derives the type letter, section, and key from
`rec+0x10` and calls the Win32 profile API. It is invoked once for each record
in the collection's read-all traversal. Therefore two records with the same
`key:section` produce two reads of the same INI entry:

```text
INI contains the key:       both records receive the same parsed value.
INI omits the key:          each record keeps its own compiled default.
malformed typed value:      each invocation follows the normal type parser;
                            the records remain separate notification keys.
```

The last case matters for the pairs with different defaults:

| pair | first default | second default |
|---|---:|---:|
| `fMinMouseScale` | `0.01` | `0.05` |
| `fMaxDistanceMoved` | `5.0` | `50.0` |
| `fTessFactorMaxDistanceScale` | `100.0` | `500.0` |

The other duplicate defaults are equal in this build (`fMouseScale=1.0`,
`uMaterialCount=5000`, both `bSkip*` values true). The two
`sResourcePrefixList` descriptors are the notable string case: one default is
the empty-string pointer and the other points at the full resource-prefix
list. If the INI entry is absent, that difference survives in memory.

The explicit BudgetCaps traversal calls the reader on both
`uMaterialCount` records. The main collection's bulk traversal is pointer-held
rather than statically named in the decompiler, but it uses the same reader
contract and record-address identity.

## 5. Write behavior

`FUN_1425c7da0(coll, rec)` performs the inverse operation and calls
`WritePrivateProfileStringA(section, key, value, coll+8)`. It also uses the
record's `+0x10` string, so both duplicate records write the same physical INI
key. There is no duplicate merge or arbitration layer:

1. writing either record updates the same `[section] key` on disk;
2. if both records are written with different in-memory values, the later
   `WriteSetting` call wins in the file;
3. writing one record does not copy its value into the other record; only a
   subsequent read (or explicit application code) resynchronizes them;
4. notify callbacks remain address-keyed, so changing/notifying one duplicate
   does not imply notifying the other.

If a save routine writes a whole collection, its traversal order is the order
of that collection's registered/read array. For the explicit BudgetCaps pair,
that means both records are visited in the pointer-table order shown above;
the second write would win if their values diverged. The binary proves the two
records and read order, but does not show a special duplicate policy.

## 6. Evidence and cross-links

- Record reader: `READER-FUN_1425c7f90.txt` and `10-SETTINGS-READER.md`
- Scatter insertion: `SETWRITE-INSERT-FUN_1425c75a0.txt`
- Register/unregister/notify: `SETWRITE-REGISTER-FUN_1425c6800.txt`,
  `SETWRITE-NOTIFY-FUN_1425c6b90.txt`
- Generic writer: `SETWRITE-WRITESETTING-FUN_1425c7da0.txt` and
  `16-SETTINGS-WRITEBACK.md`
- Main collection getter: `0x1402ccbf0`, recovered in the BudgetCaps read-all
  decompile; preferences getter: `SETWRITE-PREFSCOLL-FUN_14033ab80.txt`
- Explicit BudgetCaps traversal: `READALL-FUN_14092b990-BudgetCaps.txt`
- Full descriptor/type census: `ghidra-dump/ck-settings-keys.txt`
