# 16 · The Settings Write-Back (`FUN_1425c6b90` — scatter lookup → registered callback)

Companion to `10-SETTINGS-READER.md`. Where doc 10 proved how the CK **reads**
the 1,641 embedded `bKey:Section` settings, this doc decodes the **write side**:
the notify setter `FUN_1425c6b90`, the `BSTScatterTable` callback registry it
walks, and the two distinct paths that actually persist values to `.ini` files.

Decompiled from CreationKit.exe 1.11.137.0.

## 1. The notify setter — `FUN_1425c6b90` (RVA `0x25c6b90`)

```c
void FUN_1425c6b90(longlong key /* a SettingT record address */)
{
  coll = FUN_1425c7920();                    // INISettingCollection singleton (DAT_143c575f8)
  thunk_FUN_141ffe210(&hash, key, 0);        // BSTScatterTable::Hash(key)
  buckets = *(coll + 0x30);                  // bucket array
  if (buckets != 0) {
    node = buckets + ((*(coll + 0xc) - 1) & hash) * 3;   // 3 qwords/node, hash & (count-1)
    if (node != 0 && node[2] != 0) {         // chain head present
      while (true) {
        assert node chain within bucket array;   // "Scatter table has been trashed."
        if (*node == key) break;             // node[0] == key  -> found
        node = node[2];                      // node[2] = next
        if (node == 0) return;               // end of chain
        if (node == *(coll + 0x18)) return;  // sentinel
      }
      if (node != 0) (*(code*)node[1])(key); // node[1] = callback; invoke with the key
    }
  }
}
```

The setter is a pure **keyed dispatch**: hash the record address, walk the
collision chain, and fire the registered callback with the record as its sole
argument. It never touches the ini itself — the ini write lives in the
registered callbacks and in the app-specific savers (sections 5–6).

## 2. The registry API (all in the `FUN_1425c6xxx` family)

| RVA      | name              | signature                      | role |
|----------|-------------------|--------------------------------|------|
| `0x25c6800` | **register**  | `void (key, callback)`         | insert `{key, cb}`; if cb==0 remove instead; if a cb was already present it is overwritten; then **calls `cb(key)` immediately** (notify-on-register) |
| `0x25c69c0` | **unregister**| `void (key, callback)`         | walk chain, unlink the node whose `node[0]==key && node[1]==cb` |
| `0x25c75a0` | **insert**    | `bool (table, key, value)`     | raw `BSTScatterTable` insert (rehash/merge on collision) |
| `0x25c6b90` | **notify**    | `void (key)`                   | lookup + fire `callback(key)` |
| `0x25c7da0` | **write**     | `bool (coll, rec)`             | `WriteSetting` — type-letter format + `WritePrivateProfileStringA` to `coll+8` (vftable slot 4, §6a) |
| `0x1ffe210` | **hash**      | `void (u32*out, u64 key, u64 seed)` | 256-entry table hash, 8 rounds |
| `0x25c7920` | **singleton** | `INISettingCollection* ()`     | Meyers singleton over `DAT_143c575f8` |

### register `FUN_1425c6800` — the two-armed API

```c
void FUN_1425c6800(longlong key, code *callback)
{
  if (key == 0) return;
  coll = FUN_1425c7920();
  if (callback != 0) {
    thunk_FUN_1425c75a0(coll + 8, &key, &callback, out);  // insert {key, cb}
    (*callback)(key);                  // notify-on-register: fire immediately
    return;
  }
  // callback == 0  ->  remove the entry for key
  hash(key); walk chain; if (*node == key) unlink node;   // +0x270 assert
}
```

Registration sites confirmed in the wild: `FUN_141037340` (audio init, 4
callbacks on `&PTR_vftable_143bfa568`-style keys) and `FUN_140eab070`
(renderer, 5 callbacks). Both use **record-address keys** — the same shape the
settings statics use (`&PTR_vftable_143b9d8c8` in doc 10 §3).

### unregister `FUN_1425c69c0`

`void (key, callback)` — only removes the node if **both** key and callback
match (`local_res10 = param_2 == node[1]`), so a given key can have several
listeners and removing one does not clobber the others.

### insert `FUN_1425c75a0` — the BSTScatterTable core

`code\bscore\bstscattertable.h` (embedded path at the `0x1d4`/`0x270` asserts).
Chained scatter table, 3 qwords per node: `{key, value, next}`. Handles the
classic cases: empty slot → new head; key present → value update; table full →
`FUN_1425c79b0` (grow) then re-link, merging the displaced chain into the
vacated bucket.

### hash `FUN_141ffe210` — 8-round table hash

```c
void FUN_141ffe210(uint *out, ulonglong key, ulonglong seed)
{
  h = TBL[ (byte)seed ^ (byte)key ] ^ (seed >> 8);
  h = TBL[ (byte)h ^ (byte)(key>>8)  ] ^ (h >> 8);
  ...                                             // 8 rounds, key>>8..key>>56
  *out = TBL[ ((key>>56) ^ h) & 0xff ] ^ (uint)(h >> 8);
}
```

`TBL` = `DAT_1432d6bd0`, 256 × u32. Note the **low byte of the key is the only
input in round one** — record addresses share the top 32 bits (all in
`0x143b9xxxx`-style .data), so the table’s low-order byte diversity is what
spreads the buckets.

## 3. Where the read path ends — the reader fires the notify

`FUN_1425c7f90` (doc 10 §1) ends with:

```c
  ... switch (type) { /* b/c/h/i/u/f/s/r/a arms, each writes rec+0x8 */ }
  thunk_FUN_1425c6b90(param_2);       // <-- notify(rec) after EVERY successful read
  return bVar8;
```

So a successful read is immediately pushed back through the scatter table:
read value → store into the record’s default-value slot → notify any listener
registered on that record. The notify-on-register in §2 plus the
notify-after-read here make the registry self-consistent: late registrants get
the current value for free, and re-reads (e.g. the BudgetCaps bulk loop,
`FUN_14092b990`) re-broadcast.

## 4. The vftable wiring

The `SettingT<INISettingCollection>` per-type vtables live in the
`.rdata` table cluster at `0x3a2a440–0x3a2a660` (32-bit RVA slots, from
`DumpPtrs.java 3a2a560 288`):

| table address | slot 0 (register) | notify slot(s) | other decoded slots |
|---------------|-------------------|----------------|---------------------|
| `0x3a2a4a0` | `FUN_1425c6800` (register) | `FUN_1425c6b90` @ `0x3a2a580` | `FUN_1425c66f0` GetKeyName, `FUN_1425c69c0` unregister |
| `0x3a2a5e0` | — | `FUN_1425c6b90` @ `0x3a2a5b8` | `FUN_1425c6d00` SetString, `FUN_1425c6dc2`/`FUN_1425c6e09` string ops |
| (type family) | `FUN_1425c6800` @ `0x3a2a4c0`/`0x3a2a500` | — | `FUN_1425c69c0` @ `0x3a2a50c` |

Two of the notify slots alias into `FUN_1425c6b90`’s body at interior offsets
(`+0x3d` = `0x25c6bcd`, `+0x62` = `0x25c6bf2`): MSVC emitted multiple entry
points into the shared lookup body, one per per-type notify variant (each
decompiles to the identical scatter-lookup + callback code). The
`INISettingCollection` vftable itself (`.rdata+0x2e088`, second subclass at
`0x2f959d0`) carries `AddSetting` at slot 2 and the reader thunk at slot 5 —
the notify fires **directly**, not through the collection vftable.

## 5. Who calls the notify — the write-side trigger sites

The notify thunk (`thunk_FUN_1425c6b90` @ RVA `0x92a23`) is called from the
whole editor. Notable families (census from `CallersOf.java 0x92a23`):

- **WinMain orchestration** — `FUN_1406946a0` (3 calls): phase-1 settings
  loads push defaults through the registry.
- **CLI dispatcher** — `FUN_1404870f0` (6 calls): `-GeneratePreVisData` and
  friends write their mode settings, notifying dependent subsystems.
- **Batch runner** — `FUN_1404906a0`: the shared process runner (doc 15)
  notifies after applying process settings.
- **GUI dialogs** — `FUN_1406f3cb0`/`FUN_1406f4100`, `FUN_142825590`/
  `FUN_142825510`, `FUN_1401a9f00`, `FUN_1404c56b0`, `FUN_140c1e4c0` …: the
  settings/preferences dialogs call notify after each control change, which
  is how a slider edit reaches the renderer/audio instantly.
- **World/precombined generation** — `FUN_140d20d90` (doc 09), plus
  `FUN_14048e930`/`FUN_14048f920` (PreVis family).

## 6. The ini write path — the generic writer + app savers

**Correction (census re-run, uncapped):** an earlier `CheckRefs` probe capped at
6 refs per IAT slot and missed the big caller sets. The full census is
**43 WritePrivateProfileStringA sites across 23 functions**, and there **is** a
generic writer — `FUN_1425c7da0`, the collection's own WriteSetting:

### 6a. The generic writer — `FUN_1425c7da0` (RVA `0x25c7da0`)

The exact mirror of the reader (`INISettingCollection::WriteSetting`, collection
vftable **slot 4**; reader is slot 5):

```
bool FUN_1425c7da0(longlong *coll, longlong rec)
{
  if (*(rec + 0x10) == 0) return false;          // no key string
  if (coll[0x22] == 0) coll->vftable[6](coll, 1);// lazy path marker
  type = TypeTag(rec[0x10]);                     // FUN_1425c7210
  GetSection(rec, section); GetKeyName(rec, key);
  switch (type) {
    case 1,3: sprintf(L"%d",  *(int*)(rec+8));  break;  // c / i
    case 5:   sprintf(L"%f",  (double)*(float*)(rec+8)); break; // f
    case 6:   value = *(char**)(rec+8);          break;  // s: raw
    case 7:   sprintf(L"%u,%u,%u",   r,g,b);    break;  // r: from packed BGRA
    case 8:   sprintf(L"%u,%u,%u,%u",r,g,b,a);  break;  // a
    default:  sprintf(L"%d",  *(uint*)(rec+8)); break;  // b / h / u
  }
  WritePrivateProfileStringA(section, key, value, (LPCSTR)(coll+1)); // coll+8 = ini path
  ok = (ret != 0);
  if (lazy marker was set) coll->vftable[7](coll);  // clear marker
  return ok;
}
```

The ini path is the inline buffer at `coll+8`; `coll[0x110]` is just a
self-pointer "path ready" marker (vftable[6] = `FUN_1425c6660` sets it,
vftable[7] = `FUN_1425c6670` clears it). WriteSetting is dispatched **only**
through the collection vftable (slots at `0x2f8e0a8` / `0x2f959f0`) — no direct
callers. Full path→reader/writer map: `17-INI-PATHS.md`.

### 6b. The app savers (CreationKitPrefs.ini + ConstructionSetNetwork.ini)

Representative direct savers — the collection machinery is not involved, paths
built as `<base dir>\CreationKitPrefs.ini` (`&DAT_14742c2a0` +
`PTR_s_CreationKitPrefs_ini_143b91e60`):

| RVA      | function               | writes                                     | target ini |
|----------|------------------------|--------------------------------------------|------------|
| `0x522260` | `FUN_140522260`      | `[General] Num Object Windows`, then per window `ObjectWindowN`, `ObjectWindowN Layout`, `ObjectWindowN Filter` | `CreationKitPrefs.ini` |
| `0x69f330` | `FUN_14069f330`      | `[General] <Window>X/Y/Width/Height` from `rcNormalPosition` | `CreationKitPrefs.ini` |
| `0x6da3c0` | `FUN_1406da3c0`      | ConstructionSetNetwork ini (2 calls)       | `ConstructionSetNetwork.ini` |
| `0x63ab40` | `FUN_14063ab40`      | `Get/WritePrivateProfileStructA` (1 call each) | CreationKitPrefs.ini (struct blob) |
| `0x112f490` | `FUN_14112f490`      | prefs dialog state (5 calls)              | `CreationKitPrefs.ini` |
| `0x1147bb0`/`0x1153950`/`0x11547b0` | window/dialog state (3 calls each) | `CreationKitPrefs.ini` |

So the `bKey:Section` settings are **not** read-only: WriteSetting (6a) persists
any record through the collection, while the window/dialog savers bypass the
collection for pure UI state. Mutations also propagate through the in-memory
notify registry (§1–§5).

## 7. Full read/write round-trip contract

```
static init (CRT):  for each of 1,641 records:
   record.vftable = SettingT<INISettingCollection>::vftable   // +0x00
   AddSetting(coll, &record)          // coll vftable[2] -> BSTScatterTable insert {&record, record}
   Setting::ctor(&record)             // FUN_1425c6c90: 'S'-prefixed keys register with the global string table

read path:   FUN_1425c7f90(coll, rec)                 // doc 10
             type-letter switch -> GetPrivateProfile*  -> rec+0x8 = value
             thunk_FUN_1425c6b90(rec)                  // notify(rec) fires listeners
             ^-- every successful read re-broadcasts the value

write path:  UI / CLI / batch / worldgen code:
             rec+0x8 = newValue;                      // mutate record
             FUN_1425c6b90(rec)                       // hash -> chain -> callback(rec)
             ^-- registered listeners apply the change (audio, renderer, ...)

persistence: generic writer + app savers:
             FUN_1425c7da0 (WriteSetting, coll vftable[4])  -> coll+8 ini path
             FUN_140522260 / FUN_14069f330  -> CreationKitPrefs.ini [General] window state
             FUN_1406da3c0                 -> ConstructionSetNetwork.ini
             FUN_14063ab40                 -> WritePrivateProfileStructA
             (full census: 17-INI-PATHS.md)
```

## 8. Cross-references

- Read side, descriptor layout, registration: `10-SETTINGS-READER.md`
- Full ini path→reader/writer census: `17-INI-PATHS.md`
- Key inventory: `06-SETTINGS-KEYS.md`, `ghidra-dump/ck-settings-keys.txt`
- CLI dispatcher (a notify caller): `13-CLI-PARSER.md`; batch runner: `15-BATCH-PROCESSES.md`
- Duplicate descriptor write collisions and the two collection getter targets: `ghidra-dump/duplicate-ini-settings.md`
- Decompiles: `ghidra-dump/SETWRITE-NOTIFY-FUN_1425c6b90.txt`,
  `SETWRITE-REGISTER-FUN_1425c6800.txt`, `SETWRITE-UNREGISTER-FUN_1425c69c0.txt`,
  `SETWRITE-INSERT-FUN_1425c75a0.txt`, `SETWRITE-HASH-FUN_141ffe210.txt`,
  `SETWRITE-OBJECTWINDOW-SAVER-FUN_140522260.txt`,
  `SETWRITE-WINDOWGEOM-SAVER-FUN_14069f330.txt`
