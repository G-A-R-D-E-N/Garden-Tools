# NIF toolchain: author / repair / verify / **view** FO4 NIFs (kill NifSkope)

Self-contained Fallout 4 NIF authoring built into the FO4RecordEditor toolchain. Take a
Blender-exported OBJ → a game-ready FO4 NIF (shader, textures, tangents, BSX), **see it in a live 3D
viewport with its DDS textures**, verify it, edit it, and wire it onto a record's `Model`, with no
NifSkope, no Creation Kit and no external NIF program.

The engine is Radicle, the repository's own C++ library (`Radicle/src/nif`, ABI in
`Radicle/include/fo4/c_api_nif.h`). The native host (`PluginEditTool/native`) calls it in process and
formats its results as JSON. There is no external NIF executable to install or bundle.

---

## Architecture

```
Blender ──OBJ──▶ fo4::nif::import_obj (Radicle) ──▶ FO4 .nif ──▶ record Model.File ──▶ game
                        ▲
                        │  read_nif / read_shapes / read_lighting_shader / read_alpha /
                        │  read_collision_blobs / describe_nif / verify_nif / fix_nif /
                        │  apply_edits (and apply_edits_json for the editor's payload)
                        │
   FO4RecordEditor (C#/WPF + React WebView2)
     • native host  NifService (PluginEditTool/native) → Radicle, JSON out
     • MCP tools    nif_inspect / nif_verify / nif_tree / nif_geometry  → NifService
     • GUI panel    View / Edit / Import / Inspect / Verify / Fix       → NifInterop → NifService
     • 3D viewport  (three.js) with DDS textures via Texconv
```

Radicle reads files, never game memory, and holds every NIF fact the project relies on; see
`Radicle/docs/NIF.md` for the sources each layout fact is pinned to. `PluginEditTool/native` keeps
only argument parsing, the service surface and JSON.

## Verbs

| Verb | Radicle entry point | Notes |
|---|---|---|
| `inspect` | `inspect_nif` | FO4 flag, per-shape name/verts/tris/skinned/shader/textures/tangents, BSX and collision presence |
| `geometry` | `geometry_nif` | flat vert/tri/normal/uv arrays, texture slots, billboard mode; feeds the viewport |
| `tree` | `describe_nif` | every block, grouped, with shader flags, per-material tail fields, alpha, BSX flags and the decoded vertex descriptor |
| `verify` | `verify_nif` | the eight check names plus the vertex-descriptor/stride check |
| `fix` | `fix_nif` | recomputes tangents and bitangents, adds a missing BSXFlags, trims texture and material whitespace; idempotent |
| `import` | `import_obj` | OBJ → `BSTriShape` + shader + texture set + BSX, with normals and tangents; `--from-blender` restores Z-up |
| `set` | `apply_edits` / `apply_edits_json` | `{id,key,value}` edits; text, numbers, number lists and shader-flag name lists |

Edits write each field where the matching reader reports it, copy a shared shader, texture set or
alpha property before changing it, and pass a `bhkPhysicsSystem` packfile through untouched. An
unknown or read-only key is a parse error and the file is left byte-identical.

## What is implemented

- **Read:** the four tri-shape types, vertex attributes, skin weights and bones, `BSLightingShaderProperty`
  with its 64 flags and every per-material tail, `BSEffectShaderProperty`, `NiAlphaProperty`,
  `BSXFlags`, `bhkNPCollisionObject` and its system payloads.
- **Write:** AV name and transform, shape name/material/textures, shader floats, every per-material
  tail and the flags by name, effect-shader fields and texture paths, alpha flags and threshold, BSX
  flags and the editor marker, and the collision object's flags and Body ID. Booleans and the editor's
  `{key,label,on}` flag toggles are accepted, so the flag grid saves.
- **Refused:** `rimlightPower` on either side of the `FLT_MAX` placeholder that stores Backlight
  Power, because writing it moves every field after it; the effect shader's environment, normal and
  env-mask paths below BS 130; and a shader flag bit above 31 below stream version 0x45.
- **Verify:** header parses; each block is listed under one parent only; shapes parse; shapes have
  vertices and triangles; normals match the vertex count; UVs match the vertex count; shader and skin
  reads are clean; texture paths are relative; the vertex descriptor matches the stored data.
- **Edit mode:** `NifEditor.tsx` renders the block tree with typed inputs, a shader-flag grid, alpha
  fields and per-field revert; a save reloads the viewport.

## Limits

- **Box collision import is not implemented.** `import` refuses it with `box collision is not
  supported yet (needs the HKX writer, #814)`. Reading and editing existing collision works.
- **No structural edits:** blocks cannot be added or removed yet (new shapes, deleting collision,
  adding a `NiAlphaProperty` to a shape that has none).
- **Alpha blend and test functions are read-only;** the whole `flags` word can be set.
- Fragment and water shader properties are stored as references and not decoded.
- JSON stays out of Radicle: it returns structs and the host formats them.

## Remaining legacy C# NIF code

`FO4RecordEditor/Services/NifService.cs`, `Services/Ai/PluginToolExecutor.cs` and `Services/ToolPaths.cs`
still look for an external NIF program and shell out to it. The native host does not use that path; the
legacy MCP `nif_*` tools do until they are moved onto the native host.

## File map

| File | Role |
|---|---|
| `Radicle/src/nif/*.cpp` | the reader, describe/verify/tree, fix, import and edit writer |
| `Radicle/include/fo4/c_api_nif.h` | C ABI for the reader and its describes |
| `Radicle/include/fo4/nif/nif_write.hpp` | `write_nif`, `apply_edits`, `apply_edits_json` |
| `PluginEditTool/native/src/assets/NifService.cpp` | the in-process service surface used by the RPC, MCP and the panel |
| `PluginEditTool/native/src/assets/NifRender.cpp` | JSON for inspect, geometry, tree and verify |
| `…\FO4RecordEditor\Services\TextureService.cs` | DDS resolve (loose + BA2) + Texconv DDS→PNG (BC5 `-reconstructz`) + cache |
| `…\FO4RecordEditor\Services\NifInterop.cs` | WebView2 host object (`"nif"`) for the GUI |
| `…\web\src\NifPanel.tsx` | the panel (modes, forms, texture-root field) |
| `…\web\src\NifViewport.tsx` | three.js viewport + texture application |
| `…\web\src\NifEditor.tsx` | Edit-mode property editor (grouped cards, typed inputs, dirty tracking, Save) |
| `…\web\src\backend.ts` | `NifHost` interface + `getNif()` |

## Rebuild / run

```powershell
# 1) Radicle (C++), from Radicle\ : the NIF core the host links
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release ; cmake --build build

# 2) web frontend, from FO4RecordEditor\web\  (needed after any .tsx / backend.ts change)
npm run build                                          # tsc -b && vite build -> web\dist

# 3) C# app (copies web\dist in), from anywhere
dotnet build "…\FO4RecordEditor\FO4RecordEditor\FO4RecordEditor.csproj" -c Release
# then run bin\Release\net9.0-windows\FO4RecordEditor.exe ; for the MCP tools: /mcp reconnect
```

Gotcha: Texconv needs **backslash** paths (forward slashes give `ERROR_INVALID_NAME 0x8007007B`).
