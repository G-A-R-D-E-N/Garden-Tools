# FO4IDE integration path

FO4IDE should treat Radicle as the Fallout 4 plugin-format authority. `PluginEditTool/` owns application state, UI/RPC contracts, MO2 discovery, and user workflow. `Radicle/` owns plugin parsing, FormID/FormKey identity, FormLink discovery/remapping, and serialization.

## Native C++ sequence

For a plugin that may reference full or light masters:

```cpp
#include <fo4/plugin/master_kinds.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/plugin/writer.hpp>

fo4::Plugin plugin = fo4::read_plugin_file(plugin_path);

fo4::stamp_master_kinds_from_files(plugin, resolved_master_paths);
const fo4::PluginFormIdContext old_context = fo4::plugin_form_id_context(plugin);

plugin = fo4::remap_plugin_masters(
    std::move(plugin),
    destination_masters);

const std::vector<std::byte> bytes = fo4::write_plugin(plugin);
fo4::write_plugin_file(plugin, output_path);
```

`stamp_master_kinds_from_files` reads each available master's TES4 SmallFile flag. Missing or unreadable master files keep the parsed filename fallback instead of making the whole plugin load fail.

When the destination master list is already known with explicit full/light kinds, `remap_plugin_masters` is the authoritative operation. It rewrites on-disk master ordinals and the FormLink slots Radicle knows how to identify while preserving unknown bytes.

## Identity rules

`RawFormId` is the 32-bit value physically stored in a plugin or observed at runtime. It is not a stable cross-plugin identity.

`FormKey` is the stable identity Radicle exposes to FO4IDE: local object ID plus owning plugin, for example `000800:Example.esp`. Conflict, override, reference, and editor operations should key records by `FormKey`, not by a load-order-dependent raw integer.

Plugin-file encoding and runtime encoding are separate spaces:

- On disk, plugin records and references use the plugin's MAST ordinal space.
- Runtime full plugins use their compile/load index.
- Runtime light plugins use the `0xFE` light-slot encoding.
- A SmallFile-flagged `.esp` is light even though its filename does not end in `.esl`.
- FO4IDE must never rewrite an on-disk reference to runtime `0xFE` form just because the owner is light.

Use `plugin_form_id_context(plugin)` for plugin-file resolution. Use `build_runtime_form_id_context(load_order)` plus `resolve_runtime_form_id` only for values that are known to come from the running game/load-order space.

### Runtime load-order oracle

The runtime split follows the engine model exposed by CommonLibF4's `RE::TESFileCollection`: regular compiled files and SmallFile/light files are separate tables. A regular plugin's runtime high byte is its full compile index. A light plugin uses high byte `0xFE`, with `smallFileCompileIndex` selecting the light slot and the low 12 bits selecting the local FormID.

Radicle deliberately does not reuse those runtime indices when reading or writing an ESP/ESM/ESL. Plugin-file references continue to resolve through the file's ordered MAST table.

## Current PluginEditTool bridge

The native host receives the resolved MO2 plugin list through `environment.LoadMo2`. `RadiclePluginBackend` is the backend implementation behind the existing `IPluginBackend` seam, so the React/RPC surface does not need a second plugin model.

The intended ownership chain is:

```text
MO2 profile / Data resolution
        |
        v
PluginEditTool native host
        |
        v
RadiclePluginBackend
        |
        +--> fo4::read_plugin_file
        +--> FormKey / FormLink / conflict graph
        +--> mutation + master remap
        +--> fo4::write_plugin_file
```

Do not add new Mutagen-only behavior to the native rewrite. Missing record/schema coverage should be implemented in Radicle and exercised through the existing PluginEditTool backend contract.

## C ABI

Consumers that cannot link C++ directly can use `fo4/c_api.h`. It exposes opaque plugin handles, file/byte serialization, master-kind stamping, explicit master remapping, and UTF-8 FormKey/FormLink collection without allowing C++ exceptions to cross the ABI.

Call `radicle_abi_version()` before any other entry point and reject the library if it does not equal the `RADICLE_ABI_VERSION` the consumer was compiled against.

Handles are owned by the caller and released with the matching `_destroy` call. A handle's read-only
accessors are safe from several threads at once; `_destroy` must not race any other call on the same
handle, and handles are independent of one another. `radicle_last_error` is thread-local and stays
valid until the next Radicle C API call on the same thread. Arrays a call returns are owned by the
caller and released with the matching `radicle_free_*` function: `radicle_free_bytes` for byte and
string buffers, and `radicle_free_floats`, `radicle_free_triangles` or `radicle_free_node_refs` for
the typed ones. `radicle_nif_*` and `radicle_dds_*` follow this contract; the NIF and texture detail
is in `NIF.md` and `TEXTURES.md`.

## Verification

Useful committed fixtures live under `Radicle/testdata/`. The GoogleTest suite covers parsing, compression/XXXX, FormKey resolution, runtime full/light load-order resolution, FormLinks, VMAD, writer behavior, C ABI behavior, master-kind stamping, and conflict/reference graph behavior.

For manual inspection, build `fo4inspect` and use its FormLink inspection command against a real plugin before and after a master remap. The expected invariant is that logical `FormKey` targets stay the same even when their raw MAST ordinal changes.
