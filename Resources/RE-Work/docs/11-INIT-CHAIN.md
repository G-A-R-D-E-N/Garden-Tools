# 11 · The Subsystem Initialization Chain (WinMain and its init sequence)

Decompiled from CreationKit.exe 1.11.137.0. This maps the editor's full startup
sequence — from `WinMainCRTStartup` through `WinMain` and the ~60 subsystem
initializers it calls — and labels the three functions requested
(`FUN_140e84b60`, `FUN_140e84cf0`, `FUN_140943620`). Full decompile:
`ghidra-dump/WINMAIN-FUN_1406946a0-full.txt`.

## 0. Before WinMain

- `WinMainCRTStartup` @ RVA `0x2d65aa0` → `WinMain` `FUN_1406946a0` @ RVA `0x6946a0`.
- Between them, the CRT runs the guarded static-initializer table: hundreds of
  per-object initializers, each wrapped in `_Init_thread_header`/`_Init_thread_footer`
  (`thunk_FUN_141ffb470`/`141ffb550`) with `__FILE__`/line baked in. These register
  the 1,641 settings descriptors (see `10-SETTINGS-READER.md`), global singletons,
  and vftables.

## 1. WinMain — Phase 0: preflight

| # | call | what |
|---|------|------|
| 1 | `SetUnhandledExceptionFilter(0x1e1eb)`; `FUN_1404fc100(0x1f2ad)` | crash-handler install |
| 2 | `GetCommandLineA` → `DAT_143d34140` | exe dir; `SetCurrentDirectoryA`; `.NIF` arg → `DAT_143d33b60` (NIF preview) |
| 3 | `FUN_14032c470` → `DataFiles.txt` load (`FUN_142436190`) | data-file list |
| 4 | `FUN_142047360`, `142006480`, `14246ef50(0)`, `CoInitialize` | base init, COM |
| 5 | log files: `EditorWarnings.txt`, `EditorProgramFlow.txt`, `EditorProgrammerOutput.txt`, `EditorStackTrace.txt` (`0x32c5c0/650/6b0/710`) + `FUN_14032ca10`, `140631a40` | editor logging |
| 6 | `FUN_1402ccbf0()` = **INISettingCollection singleton** — sets `CreationKit.ini` path, reads it (`vftable[6]/[10]/[7]`); then `CreationKitCustom.ini`; then `FUN_14033ab80` prefs collection → `CreationKitPrefs.ini` | settings load (doc 10) |
| 7 | `FUN_141ffa540(0xc,1)` if NIF arg | NIF preview flag |
| 8 | `IsDebuggerPresent`; crash-reporter `FUN_1420484f0`; exception hooks `14204c3e0/3c0/400` | debug/reporting |
| 9 | `FindWindowA("Creation Kit")` → single-instance bail (`142047440`, `14244ff90`, `CoUninitialize`) | instance guard |
| 10 | `DAT_143d3410a` → window title = `"Preview"` | preview mode |

## 2. Phase 1: engine core (`if` gate = not networked / editor build)

| # | call | subsystem |
|---|------|-----------|
| 1 | `FUN_140640e80`; `FUN_14093d410("Creation Kit")` | title |
| 2 | **32 MB formID remap table** (`0x2000000` alloc, `DAT_143d12a80`); `GetPrivateProfileStringA("ReMapIDs", …)` with "Remapping userName from %s…" warning | master/plugin ID remap |
| 3 | `FUN_1425c6b90(&PTR_vftable_143bd1328)`, `(143b91f30)` | settings write-back (doc 10) |
| 4 | 13 × `DAT_143b926xx → DAT_143d340xx` | default table copy |
| 5 | `FUN_14246f7c0`; `14243a1b0`; `141037340`; `141c953b0`; `1425d08a0`; `14243aee0(1)`; `1408fb030` | misc engine singletons |
| 6 | `DAT_143d314d8=140ed9d20`; `DAT_143d1e870=1409082d0`; `1409162e0`; `140911f10`; `DAT_143d324e0=1406481f0` | stream/thread singletons |
| 7 | `142030800`; `1420312f0(1)` | Papyrus VM boot |
| 8 | **`FUN_14241fe20`** — `NiStaticDataManager` ("NiStaticDataManager Initialized") | NetImmerse static data |
| 9 | `FUN_1420d6830`/`1420d6820(+1)` | worker-thread count bump |
| 10 | **`FUN_140e84b60`** — Havok stream (below) | Havok |
| 11 | `14242e0d0`; `14242dde0(0,0,…)`; `1425e9a60` (**BSCullingProcess**); `142c840c0` (**DrawWorld**); `1425e9a60` | render pipeline |
| 12 | **`FUN_140e84cf0`** — comma-list hook (below) | (inert hook) |
| 13 | `140884e50` (**TESModelDB**, "Meshes\Marker_Error.NIF"); `142602140` (**BSResourceEntryCache**); **`FUN_140943620`** — lipsync (below); `140eaec10` (entry-cache); `14083ebe0` (**TES**); `140b3a7d0` (TESTopic); `1425fee00` (**BSStream** NIF nodes: BSValueNode/BSNodeReferences/BSMultiBoundAABB); `1407684c0` (**BSTFactory** "KeepAnEyeOn"); `14094d750`; `1409558c0`; `2bd0730` (BSEffectShader/BSPackedCombined node registration) | content/model systems |
| 14 | `_DAT_147364220 = 0x2620b`; `1407684c0`; `DAT_143d30840=141086940`; `DAT_143d1e890=140f4b0f0` | more singletons |
| 15 | `14039e7a0`; `142000820(0x1800000)` | arena alloc |
| 16 | 4 × thread singletons (`1405d7740/58ba70/5a6af0/5bfaf0` → `140584650`) | worker pools |
| 17 | `DAT_143d33d08 = 14056ad80`; `14056b290(hwnd, hinst)` | main-window class object |
| 18 | `142bd0730`; `DAT_143d1aeb8=140f15170`; `DAT_143d34f58=14271a9d0`; `DAT_143d31c80=14271a670`; `142041df0`; `1427dc4f0`; `1427dc870`; `140345130`; `140bd22d0`; `140be2510` | manager singletons |
| 19 | **`1406a3800` × 8** — Explorer file associations: `BGS.Master`, `BGS.Plugin`, `BGS.ArtFile` (+`DefaultIcon`, `shell\Edit\command`, `shell\View\command`) | shell integration |
| 20 | **14 window classes** (`RegisterClassA`): `Creation Kit` (wndproc `0x7505e`), `ActivatorClass`, `AlchemyClass`, `ArmorClass`, `CreatureClass`, `LockPickClass`, `NPCClass`, `WeaponClass`, `FaceClass`, `PlaneClass`, `MonitorClass`, `ViewerClass`, `SpeakerClass`, `LandClass` | editor UI classes |

## 3. Phase 2: main + render windows

- `riched32.dll`/`riched20.dll` loaded; `14065b370`, `140649760`, `14073c3f0`.
- Settings write-back `1425c6b90(143b914a0)`; `142c69f00(1)`; icon list
  `ImageList_LoadImageA(0xb2, 16)`.
- 4 × `GetPrivateProfileIntA("General", "%s", …)` on **CreationKitPrefs.ini** — saved window rect.
- **`CreateWindowExA("Creation Kit", style 0x2cf0000, …)`** → `DAT_143d33d00` (main HWND).
- `local_c38 = "RenderWindow"` → render-window descriptor; if not batch-mode:
  `142b3fb20(&DAT_147b87440)` (BGSRenderWindow global), `142b3e060(&DAT_147b87440, …)` =
  **BSGraphicsRenderer** init (win32/bsgraphicsrenderer.cpp), `142b7c430`, `14263b270`,
  `14262d910`, `142ca37c0`, `142624500`. Batch mode instead: `1425dcc60`.
- `CreateDialogParamA(0xeb)` → `ShowWindow(5)`; menu setup (DeleteMenu/CheckMenuItem/
  SendMessage on settings-driven items).

## 4. Phase 3: content systems + splash

- `FUN_140073d94(1, "Initializing Archive...")` — status bar.
- Archive init `142446440(…, "Fallout4 - Voices1.ba2", 0x20000)`; then
  `142446550`/`142446560`; `142446580(1, "ArchiveInvalidation.txt")` — **archive invalidation**.
- Archive registrations (`1424707e0/700/7b0`): `Fallout4 - Textures1.ba2`,
  `Fallout4 - TexturesPatch.ba2`, `Fallout4 - Voices.ba2`, `Fallout4 - Animations.ba2`,
  + `1426023e0("Fallout4 - Geometry.csg")`.
- `FUN_140073d94(1, "Initializing FaceGen...")`; `DAT_143d2fbd8 = 140ebd8f0` —
  **BSFaceGenManager** (bsfacegenmanager.cpp).
- `_time64` → `142002d40` — RNG seed; `14047cc40(0)`; singletons `1411787b0`, `141123e20`.
- `FUN_140073d94(1, "Initializing Windows...")`; `GetPrivateProfileIntA("General",
  "Num Object Windows", 0, prefs)` → loop `140522220` (**CreateDialogParamA** — object windows).
- `SetTimer(hwnd, 7, 100)`; then **message loop** (`GetMessageA`/`Translate`/`Dispatch`),
  per-frame: `141039db0(0)`, `142031300`, guarded `140630110(&DAT_143d321d0)`,
  `140630210`, `1420422f0` (BSHTMLCallbackHash), `1420d5dc0`, tab-key tracking
  (`GetAsyncKeyState(9)` → `DAT_143d33a40`).

## 5. Phase 4: teardown (mirror order)

`1425e9b70`, `14242e060`, render-window teardown (`142c84990`, `142b3fb20`,
`142b3e910`, `14008ef27`), `14210b220` (BSHavok), `14241ff40` … then the full
reverse of phase-1/3: culling, streams, FaceGen, archives, entry caches, thread
pools, image list, shell, `FreeLibrary(riched*)`, `CoUninitialize`, `CComModule`
teardown.

## 6. The three requested functions

### 6.1 `FUN_140e84b60` — Havok stream init (havokstream.cpp:0x205)

```
void FUN_140e84b60() {
  guard(0x32, 1, "havokstream.cpp", 0x205);
  thunk_FUN_14210b130();            // BSHavok (bshavok.cpp)
  thunk_FUN_141a23100();            // BSHavokMigration (bshavokmigration.cpp)
  alloc = TLS->vftable[1](TLS+0x58, 0x30);        // thread-local allocator
  new = FUN_140e856e0(alloc);                     // HavokStreambufFactory ctor
  refcount-swap(DAT_143e4fd10, new);              // replace global stream factory
  buf  = TLS->vftable[1](TLS+0x58, 0x10);
  *buf = HavokStreambufFactory::vftable;          // streambuf vtable
  refcount-swap(DAT_143e4fd20, buf);
  thunk_FUN_1420cbc80(&LAB_14001b027);            // register handler
  thunk_FUN_1420a4ae0();                          // ???
  guard_footer();
}
```

Installs the Havok stream factory pair (stream + streambuf) from thread-local
allocator memory, replacing the static `DAT_143e4fd10`/`143e4fd20` globals.

### 6.2 `FUN_140e84cf0` — comma-list override hook (no source path)

Parses `PTR_DAT_143bd8878` (`.data+0x51878`) as a **comma/space-separated token
list** into a `BSTArray<{hash, flags}>`:

```
if (*PTR_DAT_143bd8878 != '\0') {
  copy string; strtok_s(list, ", ");
  while (token) { hash = FUN_14030d5c0(token);     // string hash
                  array.append({hash, flags}); }
  process pairs (FUN_1400523d3);
}
```

In the shipped binary the pointer resolves to a vftable-adjacent data region
whose first byte is `0x00`, so the guard fails and the parse never runs — an
**inert override hook** (likely a debug-only plugin/list injection point). No
`__FILE__` was recovered for it.

### 6.3 `FUN_140943620` — Lipsync init (lipsynchanim.cpp:0x77d)

```
void FUN_140943620() {
  guard(0x56, 1, "lipsynchanim.cpp", 0x77d);
  if (!DAT_143d17138) DAT_143d17138 = FUN_140945aa0(&DAT_143d539a0);  // BSResourceEntryCache-style singleton
  FUN_14246f490(&buf, 0);            // alloc
  assert(DAT_143d17138);             // bstsingleton.h:0x87
  *(DAT_143d17138 + 0x100) = buf;
  FUN_14246f970(DAT_143d52514, DAT_143d17138);   // register in lipsync table 1
  FUN_14246f970(DAT_143d52534, DAT_143d17138);   // register in lipsync table 2
  guard_footer();
}
```

Boots the shared Lipsync manager singleton (`lipsynchanim.cpp`) and registers it
into two static handler tables (`DAT_143d52514`, `DAT_143d52534`).

## 7. Cross-references

- `ghidra-dump/WINMAIN-FUN_1406946a0-full.txt` — the full WinMain decompile.
- `08-CK-LOD-SUBSYSTEM.md` — LOD pipeline (independent of startup, but reuses
  BSResource + TES systems initialized here).
- `10-SETTINGS-READER.md` — the INISettingCollection singleton reads fired in
  Phase 0 step 6.
- `05-BATCH-CLI.md` — `-GeneratePreCombined` etc. run *after* Phase 1 via the CLI
  dispatcher (0x4870f0), not in WinMain.
