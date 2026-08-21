# 19 — Batch Exit-Code Contract (`RaiseException` 0x6b650000 / 0x6b650001)

**Scope:** every CLI batch-process branch, the shared process runner, and the
process-level termination path. Companion to doc 13 (CLI parser + dispatcher)
and doc 15 (the five documented batch processes).

---

## 1. The contract in one paragraph

The CreationKit batch pipeline signals process completion to external
automation by **raising an exception with a sentinel code**:

| code | meaning | signed exit code |
|------|---------|------------------|
| `0x6b650000` | process completed **successfully** | `1800404992` |
| `0x6b650001` | process **failed** | `1800404993` |

The raise is gated on the bool setting **`bThrowProcessCompleteExceptions:General`**
(default `0` = off). When the setting is on and the process is not caught by any
in-process handler, the exception propagates to the installed unhandled-exception
filter (writes a `.dmp`, returns `EXCEPTION_CONTINUE_SEARCH`), the OS terminates
the process, and `GetExitCodeProcess` returns the sentinel code.

The setting name is literal: the CK "throws" a "process complete" exception.

---

## 2. The gate setting — `bThrowProcessCompleteExceptions:General`

The gate byte `DAT_143b8c888` is **not** a hand-written global; it is the
**value storage of a `SettingT<INISettingCollection>` descriptor** at `.data 0x3b8c880`:

```
0x3b8c880  +0x00  vftable ptr  = SettingT<INISettingCollection>::vftable (0x142f8dce8)
0x3b8c888  +0x08  bool VALUE   = DAT_143b8c888   <-- the gate (default 0)
0x3b8c890  +0x10  key:section  = "bThrowProcessCompleteExceptions:General" (0x142fb6080)
```

- **Reader wrapper** `FUN_1401575a0` (`bsmain\setting.h` line 0x3fc): assert +
  `INISettingCollection::ReadSetting` (vtable[+8]) on the descriptor, `atexit`
  registration of the add wrapper.
- **Add wrapper** `FUN_142e89460`: vftable init + `INISettingCollection::AddSetting`
  (vtable[+0x10]) on the singleton (`FUN_1402ccbf0`, `bsmain\setting.h`).
- **Value source:** the standard collection load (`INISettingCollection::ReadAll`
  → `ReadSetting`, type letter `b`) reads it from `CreationKit.ini` +
  `CreationKitCustom.ini` under `[General]` (doc 17 path map). Key confirmed
  present in the CK's own descriptor inventory (`ghidra-dump/ck-settings-keys.txt`
  line 440).
- Because it is a setting, **no code ever writes the byte directly** — every
  reference is a READ (`CheckRefs` census: 4 sites in the dispatcher, 3 in the
  runner; a raw-byte RIP-relative scan over the whole image confirms zero writes).

Effect: `bThrowProcessCompleteExceptions=1` under `[General]` in the ini turns on
the raise contract. With the setting off, all raise sites are skipped and batch
processes return silently (exit code 0 via the normal message-loop exit).

---

## 3. Raise-site census

All sites follow the identical idiom, always gated on the setting:

```c
if (DAT_143b8c888 != 0) {
    RaiseException(code, 0, 0, NULL);   // continuable, no args
}
```

| site (RVA) | owner | branch | code |
|---|---|---|---|
| `0x4874d7` | dispatcher `FUN_1404870f0` | `0x20` MasterfileTextExporter (post-runner) | `0x6b650001 - (fileFound != 0)` |
| `0x4875d9` | dispatcher | `0x40` CompileTextExport (direct call) | `(result ^ 1) + 0x6b650000` |
| `0x48782a` | dispatcher | `0x4000000` MaterialConverter (direct call) | `0x6b650001 - (result != 0)` |
| `0x488a44` | dispatcher | `0x10000000` CompressPSG (direct call) | `0x6b650001 - (result != 0)` |
| `0x4907f9` | runner `FUN_1404906a0` | post-Execute, success path | `0x6b650001 - (Execute() != 0)` |
| `0x490880` | runner | masterfile-open failure / empty-arg path | `0x6b650001` (hard fail) |
| `0x490899` | runner | shared `LAB_1404908ac` raise | (as set above) |

The `flags` argument is `0` → **continuable**; the exception-args pointer is
NULL (no parameters are carried to the consumer).

---

## 4. The shared runner `FUN_1404906a0`

Signature: `FUN_1404906a0(proc, masterfile, splash, secondFile)` — `proc` is the
process object whose **vtable[+8] is `Execute()`**.

Three paths:

1. **No masterfile arg** (`param_2 == NULL || *param_2 == 0`): calls `Execute()`
   directly; if gate on, raises `0x6b650001 - (Execute() != 0)`.
2. **Masterfile open failure** (`DAT_143d33938 == 0`, the global TESFile*):
   logs `"No masterfile specified, aborting automated process."` or
   `"Could not get file pointer for %s, aborting automated process."`
   (severity 0xe); if gate on, raises **hard `0x6b650001`**.
3. **Normal path:** load + `ReloadIfNewer` (`FUN_14087f310/0x350`) the masterfile
   into `DAT_143d33938`; optional second file load (e.g. the `-SaveDefaultPlugin`
   output); set `DAT_143b99bc0 = 1` and notify the settings collection
   (`thunk_FUN_1425c6b90(&PTR_vftable_143b99bb8)`); call `Execute()`; then
   `SaveAll()` (`FUN_14085ef80`), and if gate on raise
   `0x6b650001 - (Execute() != 0)`.

Ordering note: for runner-dispatched branches the **runner's** raise fires first
(e.g. after the TextExporter runner call the dispatcher's own `0x4874d7` raise is
dead code in gate mode — same code either way).

---

## 5. Full exit-code matrix (31-bit switch table)

The parser `FUN_140487010` walks the 31-entry switch table at
`PTR_s__OptimizeMasterFile_143b8c740` (`.data 0x3b8c740`), OR-accumulating bit
`1 << i` into `DAT_143d2f34c` (doc 13). The dispatcher `FUN_1404870f0` then walks
the bitmask. Exit-code behavior per bit:

| bit | switch | process | exit-code behavior |
|-----|--------|---------|--------------------|
| `0x1` | `-OptimizeMasterFile` | MasterfileOptimizer | via runner → `0x6b650000/0x6b650001` |
| `0x2` | `-ExportDialogue` | DialogueExporter | via runner |
| `0x4` | `-GenerateLips` | LipGenerator | via runner |
| `0x8` | `-ExportFaceGenData` | FaceGenDataExporter | via runner (2nd token validated) |
| `0x10` | `-TagifyMasterfile` | MasterfileTagifier | via runner |
| `0x20` | `-ExportText` | MasterfileTextExporter | via runner; dispatcher re-raises on file check |
| `0x40` | `-CompileTextExport` | *(direct)* | **dispatcher raises** `(result ^ 1) + 0x6b650000` |
| `0x80` | `-DelocalizeMasterfile` | MasterfileDelocalizer | via runner |
| `0x100` | `-SaveDefaultPlugin` | FileSaver | via runner |
| `0x200` | `-CheckInPlugin` | CheckItInChappy | via runner |
| `0x400` | `-Install` | *(no process)* | AdminTasks elevation runner; **no raise** |
| `0x800` | `-DumpNeededFiles` | NeededFileDumper | via runner |
| `0x1000` | `-OptimizeDLCMasterFile` | DLCMasterfileOptimizer | via runner |
| `0x2000` | `-ExportSEQ` | SEQExporter | via runner |
| `0x4000` | `-GenerateWarnings` | WarningGenerator | via runner |
| `0x8000` | `-ExportAnimationBuildProcessData` | AnimationBuildProcessDataExport | via runner |
| `0x10000` | `-GenerateSingleLip` | *(direct)* | logs `"Unable to create LIP file."`; **no raise** |
| `0x20000` | `-GenerateStaticCollections` | StaticCollectionProcess | via runner |
| `0x40000` | `-GeneratePreCombined` | PreVisPreCombineDataProcess | via runner |
| `0x80000` | `-GeneratePreVisData` | PreVisPreCombineDataProcess | via runner |
| `0x100000` | `-DepersistRefs` | DepersistRefsProcess | via runner |
| `0x200000` | `-MapMaker` | MapMakerProcess | via runner |
| `0x400000` | `-MapInfo` | MapInfoProcess | via runner |
| `0x800000` | `-OutputAreaArt` | AreaArtOutput | via runner |
| `0x1000000` | `-UpdateModelData` | ModelDataUpdater | via runner |
| `0x2000000` | `-ExportDismemberData` | DismemberDataExporter | via runner |
| `0x4000000` | `-ConvertMaterials` | MaterialConverter | **dispatcher raises** `0x6b650001 - (result != 0)` |
| `0x8000000` | `-ImportScalingData` | ImportScalingDataProcess | via runner |
| `0x10000000` | `-CompressPSG` | CompressPSGProcess | **dispatcher raises** `0x6b650001 - (result != 0)` |
| `0x20000000` | `-BuildCDX` | BuildCDX | via runner |
| `0x40000000` | `-ConvertToESL` | ESLConverter | via runner |

**Matrix summary:**
- 26 of 31 bits route through the shared runner → single raise point, code
  decided by `Execute()`.
- 3 bits raise **directly in the dispatcher** without the runner: `0x40`
  (`-CompileTextExport`), `0x4000000` (`-ConvertMaterials`),
  `0x10000000` (`-CompressPSG`) — these still use the same two-code idiom.
- 2 bits never raise: `0x400` (`-Install`, elevation only) and `0x10000`
  (`-GenerateSingleLip`, logs and continues).
- Every failure mode collapses to `0x6b650001`; every success to `0x6b650000`.

---

## 6. The termination path (no in-process handler)

Verified exhaustively:

- **SEH census:** the whole binary contains only 10 SEH-scoped functions
  (`__C_specific_handler` / `__GSHandlerCheck_SEH` scope tables, parsed from
  `.pdata`); the three filter functions check `0xC00000FD` (stack overflow),
  `0x406D1388` (setthreadname), or continue-everything. **None covers any raise
  site** — the sentinel exceptions are never caught in-process.
- **Unhandled-exception filter** `FUN_1406a3e60`, installed at WinMain
  (`FUN_1406946a0`) via `SetUnhandledExceptionFilter`:
  1. `GetModuleFileNameA` → `CreateFileA` → writes
     `CreationKit_<yyyy>-<mm>-<dd>_<hh>-<mm>.dmp` next to the exe
     (`MiniDumpWriteDump`, type `0x40` = MiniDumpNormal).
  2. returns `0` = **`EXCEPTION_CONTINUE_SEARCH`** → the OS default handler runs
     and terminates the process.
- **Result:** `GetExitCodeProcess` yields the sentinel DWORD —
  `0x6b650000` (success) or `0x6b650001` (failure).

Consequence for automation: with `bThrowProcessCompleteExceptions:General=1`,
every batch run (success *or* failure) leaves a timestamped minidump next to the
exe, and the exit code is the sentinel. Shells that report signed 32-bit codes
show `0x6b650000`/`0x6b650001` as `-2494562304`/`-2494562303`.

---

## 7. Dispatch plumbing (batch still runs inside a window)

- WinMain `FUN_1406946a0` parses the command line, installs the filter, and
  creates the main window; the message loop (`GetMessageA`/`DispatchMessageA`)
  is the standard MFC-style loop.
- The wndproc `FUN_140696e00` on `WM_CREATE` calls the **dispatcher**
  `FUN_1404870f0`; a nonzero return triggers `SendMessageA(hwnd, WM_CLOSE)`.
  So even fully-automated batch runs create a (hidden) window and execute the
  processes during WM_CREATE, before the loop spins.

---

## 8. Validator oracle

`tools/validate_ckini.py --exit-codes <CreationKit.ini>` replays this matrix without
running the editor. It reads `[General] bThrowProcessCompleteExceptions` using the
same integer-setting semantics as the INI reader, verifies that the exception gate
is enabled, and prints all 31 switches with their bit, process path, and reachable
codes:

```bash
python3 tools/validate_ckini.py CreationKit.ini --exit-codes
```

For runner/direct-raise branches the oracle reports both
`0x6b650000` (runtime success) and `0x6b650001` (runtime failure), because a static
INI cannot determine whether the CK can load the requested masterfile or whether
`Execute()` succeeds. `-Install` and `-GenerateSingleLip` are reported as normal exit `0` paths with
no sentinel raise. A missing, malformed, or zero gate produces `VERDICT: FAIL` and
explains that the sentinel raises are skipped.

## 9. Open items

- `FUN_1404905a0` (the `XB1|X64|PS4|W32` token validator used by three branches)
  is now byte-decompiled and mapped to `DAT_147322d04`; see doc 13 §Arg helpers.
- Whether the minidump write can be suppressed in gate mode (it cannot, per the
  filter body — every unhandled exception dumps) — noted here as a behavioral
  gotcha for pipeline tooling.

## Staged decompiles

- `ghidra-dump/decompile-batch-support-1575a0-2e89460-6a3e60-487010.log`
  (setting wrapper, add wrapper, unhandled filter, CLI parser)
- `ghidra-dump/decompile-ProcessRunner-0x4906a0.log`
- `ghidra-dump/CLI-DISPATCHER-FUN_1404870f0.txt`
