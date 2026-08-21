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
  (`editor+0x428`, populated from `editor+0x410` by `FUN_141126280`). The source
  array is parsed from semicolon-separated `sAdditionalImports:Papyrus`;
  `$(source)` expands to the configured script-source folder. The installed
  default is `$(source);.\Data\Scripts\Source\Base`, so the active default
  groups are `User` and `Base`, generating `PapyrusSource_User` and
  `PapyrusSource_Base` in that order.
- Each basename is derived with `PathFindFileNameA`, then the loop builds
  `<import-path>\\<basename>.zip` and `PapyrusSource_<basename>`. `CreationClub`
  and `DLC01`–`DLC06` become active only when their paths are added to
  `sAdditionalImports`; directory presence alone does not activate them.
- `NameSpace()` of a nonexistent path yields the same S_OK+NULL under Wine, so
  the fault is independent of whether an active configured ZIP exists. Full
  construction evidence is in
  [`ghidra-dump/decompile-papyrus-zip-array-population.md`](ghidra-dump/decompile-papyrus-zip-array-population.md).

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

## 7. The prompt lambda is the clean extraction gate

The callback is `FUN_14112ae80` (RVA `0x112ae80`), RTTI-labeled
`lambda_71ce1f4350638474a0f15e4607c39b12`. Its closure has the captured zip path
at `this+0x08`. The complete body is:

```c
bool operator()() const {
    char message[0x104];
    sprintf_s(message, 0x104,
        "Script source zip \\\"%s\\\" is new or has been updated - unpack?",
        this->capturedZipPath);
    return MessageBoxA(NULL, message, "Script Source Extraction", 0x34) == 6;
}
```

`0x34` is `MB_YESNO | MB_ICONQUESTION`; `6` is `IDYES`. At
`FUN_14063ab40+0x128` (`0x14063ac68`), a false callback result branches around
`CoCreateInstance`, `NameSpace`, `Items`, and `CopyHere`; the normal cleanup and
stamp path then continues. Thus clicking **No** is a clean per-zip skip, not a
COM failure. The exact disassembly and helper/RTTI anchors are in
[`ghidra-dump/decompile-papyrus-zip-prompt-lambda-FUN_14112ae80.md`](ghidra-dump/decompile-papyrus-zip-prompt-lambda-FUN_14112ae80.md).

The lambda and its Papyrus caller read no `CreationKit.ini` descriptor, no
`CreationKitPrefs.ini` setting, and no batch bit. In particular,
`bMustConfirmScriptSync:Papyrus`, `bCopyScriptToSource:Papyrus`, and
`sScriptSourceFolder:Papyrus` do not gate this block in this build; neither does
any `-Switch`. The path is startup Papyrus initialization, not the CLI batch
pipeline. A noninteractive always-skip mode therefore needs an external No
answer or a binary patch; a pre-seeded `[ZipInstall]` stamp remains unreliable
because the fresh-path expected tuple is read from uninitialized stack.

## 8. Relationship to the docs

- `FUN_14063ab40` is doc 17's `WritePrivateProfileStructA` saver — the
  `[ZipInstall]` census entry now has its full context.
- The `PapyrusSource_%s` key naming and the `[ZipInstall]` section are new
  settings-surface facts (keys live in `CreationKitPrefs.ini`, not the
  `1,641-key` descriptor table).

## Staged decompiles

- `ghidra-dump/decompile-papyrus-zip-prompt-lambda-FUN_14112ae80.md`
- `ghidra-dump/decompile-papyrus-zip-array-population.md`

- `ghidra-dump/decompile-zipinstall-0x63ab40.log`
- `ghidra-dump/decompile-papyrusinit-0x1123e20.log`
- `ghidra-dump/decompile-ziparraycopy-0x1126280.log`
