# Batch Process Internals — MasterfileOptimizer, MasterfileTagifier, MapMakerProcess, CheckItInChappy, FileSaver

> Companion to [13-CLI-PARSER.md](13-CLI-PARSER.md) (the 31-switch table + dispatcher bit map)
> and [05-BATCH-CLI.md](05-BATCH-CLI.md). Covers the five remaining batch process objects in
> `_anon_F29367BE` and the masterfile pipeline that `-SaveDefaultPlugin` drives.
> Source file: `code\construction set\misc\bgsautomatedprocesses.cpp` (embedded path
> `...\code\construction set\misc\bgsautomatedprocesses.cpp` @ file 0x2fb4c20).

## 1. The shared runner — `FUN_1404906a0`

Every batch process dispatches through this one runner. The dispatcher (doc 13) builds a
small stack object `{ processVftable, arg0, arg1, … }` and calls
`FUN_1404906a0(&obj, argBuffer, showUI, secondFile)`:

```
FUN_1404906a0(obj, argBuf, showUI, secondFile):
  if showUI: SendMessage(mainWnd, WM_COMMAND 0x9cbc); ShowWindow(statusWnd, hide)   // close progress UI
  if argBuf empty:
      ok = obj.vftable[+8](obj)            // Execute, no-arg form
  else:
      DAT_143d33938 = DataHandler->GetFileByFilename(argBuf)     // load masterfile
      if !loaded: log "No masterfile specified" / "Could not get file pointer for %s"; raise 0x6b650001
      DataHandler->GetModFile(file, 1)
      if IsFileNewerThanLoaded(file):
          if !secondFile:                  // reload-all path
              DAT_143b99bc0 = 1; SaveAll(); DataHandler->ReloadAllMasters(0); RebuildRefs(5)
          else:                            // load second file instead
              f2 = SomeSingleton->GetFileByFilename(secondFile)
              if f2 && IsNewer(f2): reload path
      ok = obj.vftable[+8](obj)            // Execute
  if batch mode: RaiseException(0x6b650000 + !ok)     // 0x6b650001 = failure, 0x6b650000 = success
  DataHandler->SaveAll()
```

Key points:

- **Exit codes** are raised as *exceptions* (`RaiseException 0x6b650001 − (ok!=0)`), not
  returned — that is the batch abort mechanism (doc 13's "nonzero return posts WM_CLOSE").
- **Every process runs with the named masterfile loaded** (`DAT_143d33938` is the active
  file the process operates on). The "Could not get file pointer for %s" message is what
  aborts a run when the plugin name in the arg buffer doesn't resolve — e.g. the wine run
  in doc 14 (`Fallout4.esm,clean,all,1,1` passed as one token).
- The process object is just the vftable pointer + extra arg fields set by the dispatcher.

## 2. The process object contract

Each `_anon_F29367BE::<Process>` vftable lives in `.data`; the runner invokes the
**second slot** (`vtable[+8]`) as `Execute`:

| Process | vftable (.data) | vtable[0] (ctor) | Execute (vtable[+8]) | bit / switch |
|---|---|---|---|---|
| MasterfileOptimizer | `0x2fb6380` | `FUN_14048c3c0` | `FUN_14048efb0` | 0x1 `-OptimizeMasterFile` |
| MasterfileTagifier | `0x2fb6520` | `FUN_14048c400` | `thunk_FUN_140916690` → `FUN_140923d00` | 0x10 `-TagifyMasterfile` |
| MapMakerProcess | `0x2fb6d68` | `FUN_14048c340` | `FUN_14048ef80` | 0x200000 `-MapMaker` |
| CheckItInChappy | `0x2fb6720` | `FUN_14048bfb0` | `FUN_14048d4e0` | 0x200 `-CheckInPlugin` |
| FileSaver | `0x2fb66f0` | `FUN_14048c200` | `FUN_14048eb10` | 0x100 `-SaveDefaultPlugin` |

All five ctors install `_anon_F29367BE::IAutomatedProcessRunner::vftable` as the base —
the runner interface. The object fields beyond the vtable are set per-process by the
dispatcher (e.g. `obj+8` = first arg, `obj+0x10`/`obj+0x18` = second arg / file ptr for
CheckItInChappy).

## 3. MasterfileOptimizer — `-OptimizeMasterFile:<ESM>`

Execute `FUN_14048efb0` → three steps:

1. **Duplicate-reference check** `FUN_14085cc40` — MessageBox
   *"Run duplicate reference check for all references in all cells? (This may take some
   time)… bDisableDuplicateReferenceCheck=1 in your SkyrimEditorCustom.ini."* (shared
   Skyrim-era string; batch mode auto-skips via `DAT_143b99a40`). Iterates all cells +
   persistent-reference arrays, removes duplicates: `"%u duplicate references were
   removed.\r\n"`.
2. **COCInfo report** `FUN_140866780` — opens `COCInfo.dat` (writer, 0x2800 buffer) and
   walks the cell scatter-table (`DAT_143d4a1d0`, 0x18-byte entries) for records whose
   type char `+0x26 == '?'` (exterior-cell form), flags `+0x50 & 1 == 0`, passing the
   validation `FUN_140d17890` and resolving the editor file via `FUN_140d268d0`. Per
   selected record it writes tab-separated lines: plugin name (`FUN_140071152`), the
   header *"Select Form Component Export File"*, and per-form `"Exporting %s '%s'
   (%08X)."` with cell coords (`FUN_140045d1d` = x, `FUN_140d196a0` = y).
3. **ESM conversion** `FUN_140690240` — the actual optimization. Message
   *"You have chosen to create a version of the current ESM file optimized for in-game
   loading. This operation cannot be undone. Do you wish to continue?"* / MessageBox
   **"Confirm ESM Conversion."** (skipped in batch: `param_1 == 0`). Then constructs a
   **`BSMasterFileConverter`-family object** (`FUN_1406de7d0(&conv, activeMaster,
   0xf00000)`), starts (`FUN_1406dfa70`), runs (`FUN_1406e71a0`), finishes
   (`FUN_1406e01d0`), and saves the converted master (`FUN_14087ad00`). Result dialog
   "Success"/"Failure".

## 4. MasterfileTagifier — `-TagifyMasterfile:<ESM>`

Execute `thunk_FUN_140916690` → `FUN_140923d00` with the global tagify config
(`&PTR_vftable_143b9c0e0`, filename at `+0x10`):

1. `lVar4 = GetFileByFilename(DataHandler, config+0x10)`; null →
   `"TAGIFICATION: Could not get TESFile pointer for %s, aborting tagification."`
2. File-type predicate `FUN_14087f240(lVar4)`:
   - false → `FUN_1409242d0(config, file)` — the **main tag loop**: walks the file's
     tagged-string table (`param_1+0x148`, 0x18-byte entries) + the ID→form hash
     (`+0x180`/`+0x15c` buckets), tagging each form's string IDs. Logs:
     `"TAGIFICATION: Results: %u strings tagged, 0 check-out fails, 0 SetID fails,
     0 check-in fails."`,
     `"TAGIFICATION: Successfully tagged ID\t0x%08X\tto string on form
     (formID/type):\t0x%08X\t%s"`, `"…FAILED to tag form…"`, `"No Form?"`.
   - true (ESL-family) → scans the filename's extension; `_stricmp(…, ".esl")` →
     **`"TAGIFICATION: FAILED - ESL files are not supported"`** + MessageBox "File
     Error" (ESL masters cannot be tagified); otherwise `FUN_140923fa0` (secondary
     path).
3. Config persisted to globals `DAT_143d4b050` / `DAT_143d4b0b0`.

Additional tagify strings: `"TAGIFICATION: Could not get form for string ID"`,
`"TAGIFICATION: Untagged string found on form (formID/type/chunk):"`,
`"TAGIFICATION: Skipping tagging of empty string…"`,
`"TAGIFICATION: Queuing re-tag of duplicate ID"`.

## 5. MapMakerProcess — `-MapMaker:<ESMFilename> <Worldspace|Interior>`

- Dispatcher grammar: `strtok_s` split; arg0 = ESM (runner's masterfile), arg1 =
  `Worldspace|Interior` (stored at `obj+8`). `param_3=1` → the runner shows the progress
  UI (map generation is long-running).
- Execute `FUN_14048ef80` → `FUN_1406bd040(arg1)` → core `FUN_1406bc9b0`:
  - Validates the cell-coordinate rect `{x0,x1,y0,y1}` (`param_1[1..2]`, `+0xc..+0x14`);
    invalid → MessageBox *"The given cell coordinates are invalid."* / "Invalid Data."
  - `FUN_140837660(activeWorldspace, wsPtr, 1)` — selects the worldspace.
  - Iterates the cell grid; for each cell key `y & 0xffff | (short)x << 0x10` looks up
    the cell's forms in the worldspace cell table (`ws+0x98` / bucket count `+0x74`),
    skips flagged/occluded forms (`form+0x10 bit 3`), and converts
    **cell → world coordinates: `world = (cell << 12) + worldOrigin`** (4096-unit cells,
    origin float `DAT_142f9a420`), collecting map entries.

The map-related cluster in `bgsautomatedprocesses.cpp` also logs
`"GENERATING!! ROOTCELL %s (%i, %i) for cell %s (%i, %i)"` (root-cell generation).

## 6. CheckItInChappy — `-CheckInPlugin:<Plugin> <Master>`

Execute `FUN_14048d4e0` (251-line decompile, `checkitin-fun14048d4e0-full.txt`):
the VCS **auto check-in** ("chappy" = the CK's check-in system; the dispatcher logs
"Failed to start check in process. Check in canceled." if the check-in singleton is
unavailable):

1. **Collect targets** — iterate loaded files (`DAT_143d4a1e0`); for each whose owner
   (`FUN_1408be010(file, −1)`) matches `obj+8`: VCS check-out
   (`FUN_1406da780(vcsSingleton, file)`), store new version at `file+0x20`, push onto
   the checked-out list; collect interior (`'J'`) cells lacking offset arrays.
2. **Converter object** — `FUN_1406de7d0(&conv, 0, 0xf00000)` (same
   `BSMasterFileConverter` engine as §3, no auto-start file).
3. **Check out NavMeshInfoMap** — `FUN_1406eb840(&conv, &files, res)`; fail →
   `"Failed to check out NavMeshInfoMap. Check in canceled."`
4. **Set target files / build form maps** — `FUN_1406e9710` (`TESForm*→TESFile*`
   `NiTMap`), then `FUN_1406e9fc0` (form→uint map); fail → `"Failed to set target
   files. No forms will be affected by check in."`
5. **Resolve required files** — `FUN_1406dfa70`, `FUN_1406ed3e0/1406ed660/1406ed520`
   (three 0x130-byte file objects); fail → `"Failed to acess required files. Check in
   canceled."`
6. **THE CHECK-IN** — `FUN_1406ead80(&conv, filePath+0x78, "AUTO PLUGIN CHECKIN",
   &files, &map, 0)` then `FUN_1406e02e0` (commit).
7. **Per-form loop** — `FUN_1408c23e0(form, &conv, f1, f2, f3)`; failure →
   `"Failed to check in form %s '%s' (%08X)."` (form-type name from the
   record-definition table `PTR_DAT_143b9a548[type*3]` — doc 12).
8. **Post-commit** — worldspace offset arrays `FUN_1406e2930` ("Failed to update offset
   array for worldspace '%s' (%08X)."), owned-ID arrays `FUN_1406e3010` ("Failed to
   update owned ID array for file '%s'."), then teardown of the converter, maps, and
   file objects.

## 7. FileSaver — `-SaveDefaultPlugin:<PluginFilename> <MasterFilename>`

The switch that drives the masterfile save pipeline:

- Dispatcher grammar: `strtok_s` space-split of the arg buffer; missing tokens →
  *"Could not parse command line properly. Should be -SaveDefaultPlugin:<PluginFilename>
  <MasterFilename>"*. `obj = { FileSaver::vftable, PluginFilename }`; the runner loads
  **MasterFilename** as the active masterfile.
- Execute `FUN_14048eb10`:
  1. `file = GetFileByFilename(DataHandler, obj+8)` (the plugin to save).
  2. `FUN_14085ce00(DataHandler, file)` — unlink the file from the handler's loaded-file
     list (`+0xfb0` linked list) so it is saved standalone.
  3. `FUN_140862120(DataHandler, obj+8)` — **DataHandler::SaveDefaultPlugin**, the full
     save pipeline:
     - "Begin Saving…" → "…Prepare files" → "…Create temp file" →
       "…Create backup file" → "…Close temp file" (with `CopyFile( %s, %s) failed:
       error code %08x`)
     - "…Fill file header" → "…Save header" → "…Saving data" →
       "…Post Processing Forms" (with "Pre-processing failed on form '%s'. Save
       aborted.", "CreateTES failed for file '%s'. Save aborted.", "Save restarted
       before previous Save was finished.")
     - "SAVE COMPLETE." / "Saving…Done!"
     - Guard: "File '%s' is a master file or is in use. Please select another file to
       save to."; optional console pushes: "Error pushing plugin to PS4: Failed to copy
       %s to PS4.", "Error pushing plugin to XB1: Could not load function
       XtfCreateFileIOClient from XtfFileIO.dll."

### The masterfile pipeline at a glance (`-SaveDefaultPlugin` and friends)

```
-SaveDefaultPlugin:MyPlugin.esp Fallout4.esm
   → runner loads Fallout4.esm (active master DAT_143d33938)
   → FileSaver.Execute: GetFile(MyPlugin.esp) → RemoveFile → SaveDefaultPlugin()
       temp → backup → header → data → post-process → SAVE COMPLETE
   → DataHandler->SaveAll() (runner tail)

-SaveDefaultPlugin shares the save engine with:
   -SaveDefaultPlugin            (FileSaver, full save above)
   MasterfileOptimizer finish    (FUN_14087ad00 → same DataHandler save)
   CheckItInChappy commit        (converter + FUN_1406e02e0)

Both -OptimizeMasterFile and -CheckInPlugin construct the same converter object
(FUN_1406de7d0, flags 0xf00000) — the "compile for in-game loading / check-in" engine.
```

## 8. The shared converter engine

`FUN_1406de7d0(&obj, file, flags)` zeroes a ~0x60-byte object, stores `flags` at `+0xc`,
and if `file` is set and passes `FUN_14087f240` (file-type predicate) immediately starts
`FUN_1406dfa70`. Usage matrix:

| caller | file arg | flags | purpose |
|---|---|---|---|
| MasterfileOptimizer (`FUN_140690240`) | active master | `0xf00000` | ESM → optimized-for-game-loading conversion |
| CheckItInChappy (`FUN_14048d4e0`) | none (0) | `0xf00000` | check-in target processing |

Lifecycle: `FUN_1406de7d0` (ctor) → `FUN_1406dfa70` (start) → `FUN_1406e71a0` /
`FUN_1406e9fc0` / `FUN_1406ead80` etc. (work) → `FUN_1406e01d0` (finish/teardown).

## 9. Artifacts

| File | Contents |
|---|---|
| `ghidra-dump/batch-runner-fun1404906a0.log` | the shared runner |
| `ghidra-dump/vd-MasterfileOptimizer.log` … `vd-FileSaver.log` | vtable dumps + Execute decompiles for all five processes |
| `ghidra-dump/mfo-exec.log` | MasterfileOptimizer::Execute (FUN_14048efb0) |
| `ghidra-dump/w-866780.log` | COCInfo.dat report writer |
| `ghidra-dump/w3-85cc40.log` | duplicate-reference check |
| `ghidra-dump/w3-690240.log` | ESM conversion ("Confirm ESM Conversion.") |
| `ghidra-dump/w2-923d00.log`, `w4-9242d0.log` | MasterfileTagifier entry + main tag loop |
| `ghidra-dump/w2-6bd040.log`, `w4-6bc9b0.log` | MapMaker dispatch + cell→world map core |
| `ghidra-dump/w3-48d4e0.log`, `checkitin-fun14048d4e0-full.txt` | CheckItInChappy Execute (full) |
| `ghidra-dump/w3-862120.log`, `w3-85ce00.log` | SaveDefaultPlugin pipeline + file-list unlink |
| `ghidra-dump/w4-6de7d0.log` | shared converter ctor |

## Open items

- The exact `0xf00000` converter flags (bit-field breakdown) — set by the ctor only;
  needs the flag consumers (`FUN_1406dfa70` and friends).
- `FUN_14087f240` file-type predicate semantics (drives both tagify path selection and
  converter auto-start).
- Tagifier secondary path `FUN_140923fa0` and MapMaker's root-cell generator
  (`"GENERATING!! ROOTCELL…"` caller).
