# 20 — Wine Startup Crash: Papyrus Script-Source Zip Extraction (NULL `Folder*`)

**Binary:** CreationKit.exe 1.11.137.0 · **Crash:** page fault on read at
`0x0`, `movq (%rcx), %rax`, RVA `0x63ad44` (inside `FUN_14063ab40` +0x204).

This is the crash that makes the CK appear to "not load under Wine": it faults
during **startup**, inside the Papyrus script-source zip auto-extraction, before
the editor window is created.

---

## 1. The crash site

```
0x0063ad3f  MOV qword ptr [RSP + 0x58],R13        ; out param = 0
0x0063ad44  MOV RAX,qword ptr [RCX]               ; <-- FAULT: RCX == 0
0x0063ad47  CALL qword ptr [RAX + 0x58]           ; virtual call, slot 11
0x0063ad4a  TEST EAX,EAX                          ; HRESULT check follows
```

`RCX` is a COM **`Folder*`** that is NULL. The code is calling
`Folder->vtable[0x58]` (slot 11, the `Items()`/folder-item family) on it.

## 2. Call chain (from the Wine backtrace)

| frame | RVA | function | role |
|---|---|---|---|
| 0 | `0x63ad44` | `FUN_14063ab40` | zip-install stamp saver + extractor |
| 1 | `0x1129d4b` | `FUN_141129aa0` | script-source extraction loop ("Failed to extract script source zip…") |
| 2 | `0x112434b` | `FUN_141123e20` | Papyrus editor subsystem init |
| 3 | `0x696348` | `FUN_1406946a0` | **WinMain** (+0x1ca8) |
| 4 | `0x2d65bbe` | `FUN_142d65ab8` | startup orchestrator |

So the crash runs unconditionally during CK startup, before the UI is up.

## 3. The extraction function `FUN_14063ab40`

Signature `FUN_14063ab40(zipPath, destDir, key, std::function cb)`. Two phases:

**Phase A — the `[ZipInstall]` stamp (doc 17's `WritePrivateProfileStructA` saver):**
- Builds the `CreationKitPrefs.ini` path, then
  `GetPrivateProfileStructA("ZipInstall", key, &stamp, 8, prefsIni)`.
- `key` = `"PapyrusSource_<group>"` (e.g. `PapyrusSource_DLC01`).
- Re-extract if the stored 8-byte stamp is missing or "older" than the expected
  tuple `{uStack_2b0, uStack_2ac}`.
- **Gotcha:** the expected tuple is read from **uninitialized stack** — it is
  only written *after* a successful extraction (from the shell COM result, see
  below). On a fresh Wine prefix `GetPrivateProfileStructA` fails anyway, so
  `bVar1 = true` → extraction always runs.

**Phase B — extraction via Shell COM (this is the interesting part):**
```c
CoCreateInstance(CLSID_Shell, NULL, CLSCTX_INPROC_SERVER, IID_IShellDispatch, &shell);
//   CLSID_Shell        = 13709620-c279-11ce-a49e-444553540000  (Shell.Application)
//   IID_IShellDispatch = d8f015c0-c278-11ce-a49e-444553540000
shell->NameSpace(zipPath, &zipFolder);     // slot 9  — zip as a folder
shell->NameSpace(destDir, &destFolder);    // slot 9  — destination folder
zipFolder->slot11(&zipItem);               // <-- CRASH (zipFolder == NULL)
destFolder->CopyHere(zipItem, opts);       // slot 15 — the actual extraction
WritePrivateProfileStructA("ZipInstall", key, &stamp, 8, prefsIni);  // stamp it
```

The CK extracts the Papyrus source zips with the **Windows Shell's built-in zip
folder support** (`explorer`-style `CopyHere`).

## 4. Why it faults under Wine

1. `CoCreateInstance(CLSID_Shell, IID_IShellDispatch)` **succeeds** (Wine ships a
   Shell.Application implementation).
2. `IShellDispatch::NameSpace(zipPath)` returns **S_OK but leaves `*out = NULL`**
   — Wine has no zip-folder namespace handler (`zipfldr`), so a `.zip` path can't
   be opened as a `Folder`.
3. The CK checks only the **HRESULT sign** (`if (-1 < iVar6)`), never the
   out-pointer, then dereferences the NULL `Folder*` →
   `movq (%rcx), %rax` → page fault.

This is a genuine CK latent bug (S_OK-with-NULL out-param), exposed by Wine's
stub behavior; it would also crash on Windows if the zip were unreadable in a way
that made `NameSpace` return S_OK with NULL.

## 5. Why removing the zips does NOT help

- The zip list is a **fixed member array** of the Papyrus editor object
  (`editor+0x428`, populated by the copy helper `FUN_141126280` from
  `editor+0x410`), not a directory scan — the loop iterates the group list
  (Base / CreationClub / DLC01–DLC06 / User, matching the folders already
  extracted on this install) and builds `…\<group>.zip` paths regardless of
  whether the files exist.
- `NameSpace()` of a nonexistent path yields the same S_OK+NULL under Wine, so
  the fault is independent of the zips' presence on disk.

## 6. Workarounds (in increasing invasiveness)

1. **Pre-seed `[ZipInstall]`** — unreliable: the expected stamp is compared
   against uninitialized stack values, so a seeded stamp only skips extraction
   by luck.
2. **Binary patch at `0x63ad44`** — replace
   `48 8B 01 FF 50 58` (`MOV RAX,[RCX]; CALL [RAX+0x58]`) with
   `31 C0 90 90 90 90` (`XOR EAX,EAX; NOP×4`). This fakes a success HRESULT for
   the `zipItem` fetch; the following `CopyHere` runs on the (valid) dest folder
   with an empty item variant and the final `WritePrivateProfileStructA` stamps
   `[ZipInstall]`, so **subsequent launches skip extraction entirely**.
   *(Documented from the decompile; not yet run-tested under Wine.)*
3. **Wine override for `shell32`** that makes `NameSpace` of a `.zip` return a
   real `Folder` — requires zip folder support Wine doesn't have; not practical.

## 7. Relationship to the docs

- `FUN_14063ab40` is doc 17's `WritePrivateProfileStructA` saver — the
  `[ZipInstall]` census entry now has its full context.
- The `PapyrusSource_%s` key naming and the `[ZipInstall]` section are new
  settings-surface facts (keys live in `CreationKitPrefs.ini`, not the
  `1,641-key` descriptor table).

## Staged decompiles

- `ghidra-dump/decompile-zipinstall-0x63ab40.log`
- `ghidra-dump/decompile-papyrusinit-0x1123e20.log`
- `ghidra-dump/decompile-ziparraycopy-0x1126280.log`
