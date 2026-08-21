# 17 · The INI File Census — every path the CK reads/writes, mapped to its reader

Complete enumeration of the `.ini` files CreationKit.exe touches through the
`GetPrivateProfile*` / `WritePrivateProfile*` Win32 APIs, and the function that
reads each one. Companion to `10-SETTINGS-READER.md` (the `bKey:Section`
reader) and `16-SETTINGS-WRITEBACK.md` (the notify registry).

Decompiled from CreationKit.exe 1.11.137.0. Method: all five IAT slots
(`0x82336b0` WritePrivateProfileStringA, `0x82336d0` GetPrivateProfileIntA,
`0x82336d8` GetPrivateProfileStringA, `0x8233710` GetPrivateProfileStructA,
`0x8233718` WritePrivateProfileStructA) scanned for every direct instruction
reference, each reference attributed to its containing function, and every
containing function decompiled to recover the path string it targets.

## 1. The five touched files

| # | path (as built)                      | reader function(s)                                    | role |
|---|--------------------------------------|-------------------------------------------------------|------|
| 1 | `<base>\CreationKit.ini`             | `FUN_1425c7f90` (ReadSetting) via `FUN_1402cb660` (ReadAll) on the `INISettingCollection` instance; + `FUN_1406946a0` (WinMain) `[General]` reads | primary settings database |
| 2 | `<base>\CreationKitCustom.ini`       | same collection, same machinery (layered override, loaded after #1) | user overrides |
| 3 | `<base>\CreationKitPrefs.ini`        | `FUN_1425c7f90` via `FUN_1402cb660` on the `INIPrefSettingCollection` instance; + ~20 window/dialog-state readers | UI window & dialog state |
| 4 | `%s\ConstructionSetNetwork.ini`      | `FUN_1406da3c0`, `FUN_1406a34f0`, `FUN_1406da920`    | version-control network share |
| 5 | `<CK dir>\Loaders.ini`               | `FUN_1425feb30` (`[Loaders]` section, loader-DLL list) | plugin loader config |

`<base>` = `DAT_14742c2a0` (a global base-dir buffer, e.g. the exe directory or
My Documents\My Games\Fallout4), concatenated with the filename literals at
`.rdata 0x2fd2998` (`CreationKit.ini`), `0x2fd29b0` (`CreationKitCustom.ini`),
`0x2fd29d0` (`CreationKitPrefs.ini`).

**Not touched** (dialog strings only): `ConstructionSet.ini`
(`0x2fd8ef0`/`0x2fd91c0` — version-control help text), `SkyrimEditorCustom.ini`
(`0x3006f20` — duplicate-reference-check dialog, a Skyrim-CK leftover string).

## 2. The two collection classes and their vftables

Both `INISettingCollection::vftable` (`.rdata 0x2f8e088`) and the second
subclass vftable (`0x2f959d0`, same slots, custom dtor at slot 0) expose:

| slot | fn                | role |
|------|-------------------|------|
| 2    | `thunk_FUN_1402cb410`  | **AddSetting** — insert a record into the scatter table |
| 4    | `thunk_FUN_1425c7da0`  | **WriteSetting** — the generic writer (type-letter format + `WritePrivateProfileStringA`) |
| 5    | `thunk_FUN_1425c7f90`  | **ReadSetting** — the generic reader (doc 10) |
| 6    | `thunk_FUN_1425c6660`  | lazy path marker: `coll[0x110] = coll` (self-pointer = "path ready") |
| 7    | `thunk_FUN_1425c6670`  | clear the marker |
| 10   | `thunk_FUN_1402cb660`  | **ReadAll** — walk the record list (`coll+0x118`), call slot 5 on each, AND the results |

Collection instances (three, two classes):

| singleton | object      | ini path wired in WinMain | notes |
|-----------|-------------|---------------------------|-------|
| `FUN_1402ccbf0` | `DAT_14742c170` | **CreationKit.ini** then **CreationKitCustom.ini** (`0x694a6e`, `0x694bd6`) | the path-carrying `INISettingCollection` |
| `FUN_1425c7920` | `DAT_143c575f8` | — (no path) | the scatter-table **registry**; only the register/notify/unregister family calls it |
| `FUN_14033ab80` | `DAT_143f2f9e8` | **CreationKitPrefs.ini** (`0x694c…`) | the `INIPrefSettingCollection` instance |

## 3. The generic reader/writer pair (collection paths #1–#3)

```
ReadSetting  FUN_1425c7f90(coll, rec):   type-letter switch -> GetPrivateProfile*(section, key, default, coll+8)
                                          -> rec+0x8 = value -> notify(rec)         (doc 10, doc 16)
WriteSetting FUN_1425c7da0(coll, rec):   type-letter switch -> format value
                                          -> WritePrivateProfileStringA(section, key, value, coll+8)
ReadAll      FUN_1402cb660(coll):        for rec in coll+0x118 list: ReadSetting(coll, rec)
```

- The ini path is the **inline buffer at `coll+8`**; `coll[0x110]` is only a
  self-pointer marker set by vftable[6].
- `WriteSetting` is dispatched **only** through the collection vftables
  (`0x2f8e0a8`, `0x2f959f0`) — no direct callers, so every save goes through
  `coll->vftable[4]`.

## 4. Read-site census by path

**CreationKit.ini / CreationKitCustom.ini (through the collection):**
- `FUN_1406946a0` (WinMain, `0x694a6e`/`0x694bd6`) — wires the paths, then
  ReadAll; also reads `[General]` keys directly (5× GetPrivateProfileIntA,
  1× GetPrivateProfileStringA).

**CreationKitPrefs.ini — direct readers (~20 functions):**

| function | section(s) | what it reads |
|----------|-----------|---------------|
| `FUN_14051edf0` | `[General]` | dialog/window state |
| `FUN_14051f000` | `[General]` | object-window init state |
| `FUN_1405230f0` | `[General]` | 2× string (window state) |
| `FUN_140534a70` | `[General]`, `Actors` | actors dialog |
| `FUN_140534e10` | `[General]`, `Actors` | actors dialog |
| `FUN_14055a970` | `[General]` | form-properties dialog |
| `FUN_140566530` | `[Hotkeys]` | 2× int — **hotkey bindings** |
| `FUN_14063de00` | `[General]` | window state |
| `FUN_14063e030` | `[General]` | window state |
| `FUN_140636790` | `[General]` | 1× string |
| `FUN_140688590` | `[WhoCanMerge]` | merge dialog permission |
| `FUN_14069f2c0` | `[General]` | window rect (mate of saver `0x69f330`) |
| `FUN_14069f580` | `[General]` | 4× int — window rect loader |
| `FUN_1406a24f0` | `[General]` | window state |
| `FUN_1408c6560` | `[General]` | editor option (mate of writer `0x8c64b0`) |
| `FUN_140b3cd40` | `[Voice]`, `Custom` | voice dialog |
| `FUN_140fee470` | `[General]` | prefs dialog (mate of writer `0xfed890`) |
| `FUN_14112e4b0` | (script-pick) | Papyrus script dialog (doc 10 §5) |
| `FUN_141148030` | `[General]` | 1× int + 2× string |
| `FUN_141154a00` | `[General]` | 3× int — window pos loader (mate of savers `0x1153950`/`0x11547b0`) |

**ConstructionSetNetwork.ini:**
- `FUN_1406da920` — builds the path `"%s\ConstructionSetNetwork.ini"` (`0x2fd9128`)
- `FUN_1406a34f0` — `[Users]` version-control pref + the "Select ConstructionSetNetwork.INI File" dialogs (`0x2fd8e40`–`0x2fd90d0`)
- `FUN_1406da3c0` — 1× Int + 1× String read, 2× String write (the network-ini loader/saver)

**Loaders.ini:**
- `FUN_1425feb30` — `[Loaders]` section, loader-DLL list (doc 10 §5)

## 5. Write-site census by path

**CreationKit.ini / Custom / Prefs — generic writer:**
- `FUN_1425c7da0` (**WriteSetting**, collection vftable slot 4) — the only
  generic path; reachable only via `coll->vftable[4]`.

**CreationKitPrefs.ini — direct savers (23 functions, 43 call sites):**

| function | what it writes |
|----------|----------------|
| `FUN_140522260` | ObjectWindow layout: `[General] Num Object Windows`, per-window `ObjectWindowN`/`… Layout`/`… Filter` |
| `FUN_14069f330` | window X/Y/Width/Height from `rcNormalPosition` |
| `FUN_14112f490` | 5× — prefs dialog state |
| `FUN_141147bb0`, `FUN_141153950`, `FUN_1411547b0` | 3× each — dialog/window state |
| `FUN_1405680e0`, `FUN_1408c64b0`, `FUN_140fed890` | editor options (each has a `[General]` mate reader) |
| `FUN_1404ec5f0`, `FUN_140523f30`, `FUN_14052b5c0`, `FUN_140532f50`, `FUN_14055aaf0`, `FUN_1406366a0`, `FUN_14063df20`, `FUN_14063e210`, `FUN_14069f230` | window/dialog state, `[General]` |
| `FUN_14063ab40` | `GetPrivateProfileStructA` + `WritePrivateProfileStructA` (`[ZipInstall]` script-source stamp — full context in [20-WINE-PAPYRUS-ZIP-CRASH.md](20-WINE-PAPYRUS-ZIP-CRASH.md)) |

**ConstructionSetNetwork.ini:** `FUN_1406da3c0` (2× String).

**Raw counts (IAT census):** WritePrivateProfileStringA 43 sites / 23 fns;
GetPrivateProfileIntA 36 / 22; GetPrivateProfileStringA 22 / 11;
GetPrivateProfileStructA 1; WritePrivateProfileStructA 1.

## 6. Open item

The path-carrying `INISettingCollection` instance (`DAT_14742c170`) and the
1,641-record registry (`DAT_143c575f8`) are statically distinct objects; the
singleton indirection hides which one the descriptor initializers' `AddSetting`
really feeds. Doc 10's caveat stands — but the reader/writer machinery is
proven identical either way (same vftable slots 4/5/10).

## 7. Cross-references

- Reader internals (type letters, descriptor layout): `10-SETTINGS-READER.md`
- Write-back notify + registry API: `16-SETTINGS-WRITEBACK.md`
- WinMain phase-1 settings loads: `11-INIT-CHAIN.md`
- Decompiles: `ghidra-dump/SETWRITE-*.txt`, `READER-FUN_1425c7f90.txt`,
  `READALL-FUN_14092b990-BudgetCaps.txt`
