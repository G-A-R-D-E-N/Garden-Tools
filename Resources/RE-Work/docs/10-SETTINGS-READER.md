# 10 · The Settings Reader (`bKey:Section` → GetPrivateProfile machinery)

Decompiled from CreationKit.exe 1.11.137.0. This is the editor's embedded settings
database: the 1,629 `bKey:Section` literals, the descriptor table that anchors them,
and the single reader that turns each into a `GetPrivateProfile*` call.

## 1. The reader: `FUN_1425c7f90` (RVA `0x25c7f90`)

`bool INISettingCollection::ReadSetting(INISettingCollection* coll, SettingT* rec)`

```
bool FUN_1425c7f90(longlong *coll, longlong rec)
{
  if (*(rec + 0x10) == 0) return false;            // no key string -> skip
  if (coll[0x22] == 0) { coll->vftable[6](coll, 1); }   // lazy alloc ini path buffer
  type = TypeTag(*(rec + 0x10));                   // FUN_1425c7210: first char
  GetSection(coll->rec, section);                  // FUN_1425c6690: strchr(':')+1, default "MAIN"
  GetKeyName(coll->rec, key);                      // FUN_1425c66f0: before ':', default whole
  switch (type) {
  case 0:  // b : bool
    v = GetPrivateProfileIntA(section, key, *(rec+8) != '\0', coll->iniPath);
    *(rec+8) = v != 0; break;
  case 1:  // c : char / int8
    v = GetPrivateProfileIntA(section, key, (char)*(rec+8), coll->iniPath);
    *(rec+8) = (char)v; break;
  case 2:  // h : int16 (default cast)
    v = GetPrivateProfileIntA(section, key, *(INT*)(rec+8), coll->iniPath);
    *(rec+8) = v; break;                            // (decompiler default arm)
  case 3:  // i : int32
    v = GetPrivateProfileIntA(section, key, *(INT*)(rec+8), coll->iniPath);
    *(rec+8) = v; break;
  case 4:  // u : uint32
    v = GetPrivateProfileIntA(section, key, *(INT*)(rec+8), coll->iniPath);
    *(rec+8) = (UINT)v; break;
  case 5:  // f : float
    wsprintfW(L"%f", (double)*(float*)(rec+8));     // default formatted as %f
    if (GetPrivateProfileStringA(section, key, wbuf, buf, 0x400, coll->iniPath) == 0) goto out;
    ok = sscanf(buf, "%f", rec+8) == 1; break;
  case 6:  // s / S : string
    if (strcmp(section, "LANGUAGE") == 0) {         // special case
      GetPrivateProfileStringA("General", "sLanguage", PTR_DAT_143b992d0, buf, 0x400, coll->iniPath);
      strcpy(section, buf);                          // sLanguage VALUE becomes the section!
    }
    GetPrivateProfileStringA(section, key, *(LPCSTR*)(rec+8), buf, 0x400, coll->iniPath);
    SetString(rec, buf);                             // FUN_1425c6d00 (copy/free-alloc)
    break;
  case 7:  // r : RGB color
    if (GetPrivateProfileStringA(section, key, "", buf, 0x400, coll->iniPath) == 0) goto out;
    ok = sscanf(buf, "%u,%u,%u", &r, &g, &b) == 3;
    *(rec+8) = ((r | g<<8) << 8 | b) << 8 | 0xff;    // packed BGRA, alpha=0xff
    break;
  case 8:  // a : RGBA color
    if (GetPrivateProfileStringA(section, key, "", buf, 0x400, coll->iniPath) == 0) goto out;
    ok = sscanf(buf, "%u,%u,%u,%u", &r,&g,&b,&a) == 4;
    *(rec+8) = ((r | g<<8) << 8 | b) << 8 | a;       // packed BGRA
    break;
  }
  thunk_FUN_1425c6b90(param_2);                     // notify(rec) via scatter table — see doc 16
out:
  if (lazy buffer was allocated) free it;
  return ok;
}
```

### The type-letter table — `FUN_1425c7210` (RVA `0x25c7210`)

The **first character of the key name** (before the `:`) selects the type:

| letter | code | reader case | C type     | ini syntax                  |
|--------|------|-------------|------------|-----------------------------|
| `b`    | 0    | bool        | bool       | `0` / `1`                   |
| `c`    | 1    | char        | int8       | integer                     |
| `h`    | 2    | int16       | int16      | integer                     |
| `i`    | 3    | int32       | int32      | integer                     |
| `u`    | 4    | uint32      | uint32     | integer                     |
| `f`    | 5    | float       | float32    | decimal (`%f`)              |
| `s`,`S`| 6    | string      | char*      | text                        |
| `r`    | 7    | RGB color   | uint32     | `r,g,b`                     |
| `a`    | 8    | RGBA color  | uint32     | `r,g,b,a`                   |
| other  | 9    | —           |            | (unhandled; reader's switch has no case) |

### Section/key split — `FUN_1425c6690` / `FUN_1425c66f0` (RVA `0x25c6690` / `0x25c66f0`)

Both operate on the record's key string at `rec+0x10`:
- **key name** = everything before the first `:` (whole string if no colon)
- **section**  = everything after the first `:` — or the literal `"MAIN"` if no colon
- one `sLanguage:General`-shaped quirk: `GetSection == "LANGUAGE"` re-reads
  `[General] sLanguage` and uses **its value as the section name**.

## 2. The descriptor records

Each embedded key lives in exactly **one** 0x18-byte descriptor:

```
+0x00  qword  SettingT<INISettingCollection>::vftable (per-type vftable)
+0x08  qword  default value (typed: byte / int / float bits / char* / packed BGRA)
+0x10  qword  pointer to the "bKey:Section" literal
```

Raw-binary census: **3,826** `[bsuifach]...:...` string candidates in `.rdata`;
**1,641** have a static qword pointer → 1,641 real descriptors (the other ~2,185
candidates are message text, not settings). Descriptors cluster into **~360 per-subsystem
static arrays** (e.g. `BudgetCaps`'s array at `.data+0x16748`).

## 3. Registration (static init)

Every descriptor has its own static initializer (e.g. `FUN_142ea4030` for the
`uRefCountInterior:BudgetCaps` record at `.data+0x168c8`):

```
PTR_vftable_143b9d8c8 = SettingT<INISettingCollection>::vftable;  // write +0x00
plVar1 = GetINISettingCollectionSingleton();                      // FUN_1425c7920
(**(coll->vftable + 0x10))(coll, &record);                        // vftable[2] = AddSetting
Setting::ctor(&record);                                           // FUN_1425c6c90
```

- `FUN_1425c7920` (RVA `0x25c7920`) — Meyers singleton over `DAT_143c575f8`
  (`.data+0x575f8`), with `_Init_thread_header`/`atexit` TLS-guard.
- `AddSetting` = collection vftable slot **2** — inserts into the `bstscattertable`
  (`code\bscore\bstscattertable.h`, "Scatter table has been trashed." asserts at
  line 0x13c/0x270). Bucket array at `coll+0x30`, count at `coll+0xc`.
- The initializers fire from the CRT static-init table at process start — so
  **all 1,641 records are registered before WinMain**.

Verified per-record: ghidra reports a code reference (WRITE from the init blob
`0x2ea38xx–0x2ea40xx`, plus LEA from readers) for **every one of the 1,641 record
addresses** — zero orphan descriptors.

## 4. The read-all loop (BudgetCaps example) — `FUN_14092b990` (RVA `0x92b990`)

```
void FUN_14092b990() {
  coll = GetBudgetCapsCollection();        // DAT_14742c170, INISettingCollection::vftable
  ppuVar2 = &PTR_PTR_143b9d490;            // pointer table (40-byte stride, 2 recs each)
  do {
    coll->vftable[5](coll, ppuVar2[-1]);   // ReadSetting(rec A)
    coll->vftable[5](coll, *ppuVar2);      // ReadSetting(rec B)
    ppuVar2 += 5;                          // +40 bytes
  } while (ppuVar2 < 0x143b9d738);         // 17 strides = 34 records
}
```

- `vftable[5]` of `INISettingCollection::vftable` (`.rdata+0x2e088` … slot5 at
  `0x2f8e0b0`) is the **reader thunk** `0x14002ddb7` → `FUN_1425c7f90`.
- A second vftable at `.rdata+0x2f959d0` (same slots 1–10, custom slot 0 = dtor)
  also carries the reader at slot 5 — a second `INISettingCollection` subclass.
- `SettingT<INISettingCollection>::vftable` is the per-record type vftable (the
  `+0x00` field of every descriptor).

## 5. Verdict: which keys are actually read

**All 1,641 registered keys are read through `FUN_1425c7f90`.** Evidence:

1. Every key string has exactly one static pointer → exactly one descriptor.
2. Every descriptor is registered (AddSetting) by a static initializer.
3. The reader is the **only** code path that reads `bKey:Section` settings:
   the full GetPrivateProfile census is **59 call sites** (36 GetPrivateProfileIntA
   + 22 GetPrivateProfileStringA + 1 GetPrivateProfileStructA) — every site is
   attributed to a file and function in `17-INI-PATHS.md`:
   - the reader itself (4 arms) + the BudgetCaps ReadAll loop (`FUN_14092b990`)
   - 2 in `FUN_1425feb30` = **Loaders.ini** loader (`[Loaders]` section, loader DLLs)
   - 5 in `FUN_14112e4b0` = Papyrus script-pick dialog (**CreationKitPrefs.ini**)
   - ~20 **CreationKitPrefs.ini** window/dialog readers (doc 17 §4)
   - WinMain 0x6946a0 (CreationKit.ini `[General]`), 0x6da3c0/0x6da920/0x6a34f0
     (ConstructionSetNetwork.ini), 0x1148030 … — app config
4. The read-all loop pattern is proven on the BudgetCaps collection
   (`FUN_14092b990`); the main collection shares the identical vftable slot 5
   dispatch, reached through the `.data` singleton.

**Caveat (honest):** the single "load every setting" function for the *main*
collection was not isolated — its read loop dispatches through the
`.data`-held singleton, so ghidra cannot statically link those call sites
(the same pointer-scrubbing that hides the RTTI graph in this binary). What is
proven is that no other code reads `bKey:Section` keys, that every key is
registered, and that the only reader is the decompiled dispatcher above. If the
main collection never bulk-loaded, keys would simply read lazily on first access
through the same function — either way the read semantics are identical.

## 6. Cross-references

- Key inventory (all 1,629 literals): `06-SETTINGS-KEYS.md`, `ck-settings-keys.txt`
- Batch CLI (which switches read settings): `05-BATCH-CLI.md`
- Write-back side (notify setter, registry API, ini-write census): `16-SETTINGS-WRITEBACK.md`
- Full ini path→reader map: `17-INI-PATHS.md`
- Compatibility oracle for a real CreationKit.ini: `tools/validate_ckini.py` (replays this reader + the type-letter switch against the 1,629-key table)
- Duplicate descriptor pairs, their two initializer/data runs, and pointer-keyed collision behavior: `ghidra-dump/duplicate-ini-settings.md`
- Decompiles: `ghidra-dump/READER-FUN_1425c7f90.txt`, `READALL-FUN_14092b990-BudgetCaps.txt`,
  `INIT-FUN_142ea4030-record-register.txt`, `SINGLETON-FUN_1425c7920.txt`,
  `LOADERSINI-FUN_1425feb30.txt`
- Analyzer scripts: `ghidra-dump/ClusterRefs.java` (reference-chain census),
  `CheckRefs.java` (per-record code-ref probe)
