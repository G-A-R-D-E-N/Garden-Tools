# CreationKit.exe — Recovered Source Tree

The binary embeds **1,116 `__FILE__` source paths** (from `assert`/`BS_ASSERT`/logging macros),
all rooted at the CI checkout:

```
E:\BuildAgent\work\4a2aa37ca4b2ffa3\scripts\_workspace\code\
```

Full list: [`ghidra-dump/srcpaths.txt`](ghidra-dump/srcpaths.txt). The CK-specific module is
`code\construction set\` (86 files, listed below).

## Top-level modules (file counts)

| Module | Files | Role |
|---|---|---|
| `shared` | 359 | engine core (AI, forms, refs, cells, scripts) |
| `gamebryogen9` | 201 | Gamebryo graphics gen9 — `corelibs\ni*` scene graph, `nimain\bslodtrishape.cpp` |
| `bsshader` | 99 | shader library (`bsmeshlodtrishape.cpp`) |
| `bshavokmigration` | 86 | Havok version-migration glue |
| `bspathfinding` | 61 | runtime pathing (navmesh, Detour-ish) |
| `bsmain` | 61 | engine main |
| `bscore` | 44 | core (strings, containers, memory) |
| `bshavok` | 38 | Havok integration |
| `bsscript` | 33 | Papyrus VM |
| `bssystem` | 30 | system services |
| `bsanimation` | 23 | animation |
| `bsresource` | 17 | resource/file layer |
| `bsgraphics` | 11 | graphics |
| `bsaudio` | 11 | audio |
| `bsdiag` | 10 | diagnostics |
| `bsplatform` | 8 | platform |
| `bsdevices` | 8 | devices |
| `sdkgen9` | 5 | graphics SDK |
| `facegen` | 4 | FaceGen |
| `bssystemmonitor` / `bssystemutilities` | 5 | system utils |
| `havokgen9` | 1 | Havok (a stray `d:\radio\havokgen9\...` path — built on a dev machine) |
| `construction set` | 86 | **the CK itself** |

## `code\construction set\` — the CK module (86 files)

### `misc\` — application, windows, LOD, tools
```
tesmain.cpp                     ← CK entry (WinMain / editor init)
tesdialog.cpp  tesuser.cpp  tesundo.cpp  tespreferences.cpp
niwindow.cpp  bgswindowlayout.cpp  bgsdialogbase.cpp  bgsrendergrid.cpp
bgsrenderwindow.cpp  bgsrenderwindowgizmo.cpp
bgsrenderwindownavmesheditmodule.cpp  bgsrenderwindowreferenceeditmodule.cpp
bgsobjectwindow.cpp  bgsreferencepropertiesdialog.cpp  bgsreferencecollection.h
bgsanimsoundtagdialog.cpp  bgsmeshdebuggerdialog.cpp  bgsarchivebrowser.cpp
bgsdistantlodmeshdialog.cpp  bgsworldlodsettingsdialog.cpp   ← LOD UI
tesmapmaker.cpp  tespreviewcontrol.cpp  tesclassdf.cpp  bgsmodutils.cpp
bgsautomatedprocesses.cpp  bgsadmintasks.h                  ← batch CLI
tesversioncontrol.cpp  lipsynchmanager.cpp  appresourceloaders.cpp
export\fbxexportutils.cpp       ← FBX export
tescreation\tesdetailspecifications.cpp
```

### `pathfinding\` — editor navmesh generation
```
editornavmesh.cpp  navmeshgeneration.cpp  navmeshgeneration_util.cpp
navgentriangle.cpp  navgenvertex.cpp
```

### `script\` — Papyrus fragments
```
bgscsmodifiableboundscript.cpp  bgscsscriptfragmentsystem.cpp
bgscsscriptobjectbindpolicy.cpp  bgscsscriptutils.cpp
```

### `tesforms\` — editor-side form forks (`*_editor.cpp`)
```
tesland_editor.cpp  teslandtexture_editor.cpp  tesnpc_editor.cpp
tesobjectcell_editor.cpp  tesobjectrefr_editor.cpp  tesquest_editor.cpp
testopic_editor.cpp  testopicinfo_editor.cpp  tesworldspace_editor.cpp
tesfurniture_editor.cpp  tesimagespacemodifier_editor.cpp
bgsassociationtype_editor.cpp  bgsaudioeffectchain_editor.cpp
bgsautoweaponsounddef_editor.cpp  bgsbodypart_editor.cpp
bgscompoundsounddef_editor.cpp  bgsequipslot_editor.cpp  bgsimpactdata_editor.cpp
bgsimpactdataset_editor.cpp  bgskeyword_editor.cpp  bgslensflare_editor.cpp
bgslensflaresprite_editor.cpp  bgslistform_editor.cpp  bgsmaterialobject_editor.cpp
bgsmaterialtype_editor.cpp  bgsmovementtype_editor.cpp  bgsmusic*_editor.cpp (6)
bgsnote_editor.cpp  bgsoutfit_editor.cpp  bgsrelationship_editor.cpp
bgsscene_editor.cpp  bgssound*_editor.cpp (5)  bgsstandardsounddef_editor.cpp
bgsstaticcollection_editor.cpp  bgstextureset_editor.cpp  bgszoomdata_editor.cpp
```

### `visibility\`
```
bsprecomputedvisibility_editor.cpp   ← precombined/previs editor side
```

## Notable engine files (from `srcpaths.txt`)

| File | Interest |
|---|---|
| `gamebryogen9\corelibs\nimain\bslodtrishape.cpp` | LOD trishape runtime |
| `bsshader\bsmeshlodtrishape.cpp` | mesh-LOD shader-side |
| `shared\ai\*` | AI/package data (35 files) |
| `shared\tesforms\*` | runtime form classes (the `_editor.cpp` forks pair with these) |

## How to use

- `__FILE__` strings land in ghidra near their `assert`/log sites → xrefs from a path string jump
  into that file's first inlined frame.
- File names give function-naming hints: functions near the `bgsrenderwindow.cpp` path string are
  render-window code; near `tesmain.cpp` are CK init.
- The `*_editor.cpp` list is the **editor-only fork surface** — the exact classes to diff against
  the runtime forms for "what the editor does differently."
