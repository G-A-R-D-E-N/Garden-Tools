# FO4IDE as an MCP server

## What this is

MCP (Model Context Protocol) is a standard way to hand an AI assistant a set of tools it can call.
FO4IDE implements it: launched with `--mcp`, `FO4RecordEditor.exe` opens no window and speaks
line-delimited JSON-RPC on stdin and stdout (protocol version 2024-11-05, server name `fo4editor`).
It advertises **56 tools**: 17 for plugins and records, 16 for typed records, 5 for MO2 profile and host state, 5 for
Papyrus, 4 for NIFs, 2 for materials, 4 for audio, 1 for BA2 archives and 2 for PACK procedure trees.

The server is the native C++23 host that the desktop editor also runs on, and it needs no other
runtime. Radicle, the C++ plugin library built into the app, parses, edits and writes the plugins.
The practical effect is that your AI stops guessing about your load order and starts querying it:
it can open a plugin, read the winning version of a record, change a field in memory and, when you
say so, save.

It has no model inside it. It is the hands; your AI client is the brain. The executable keeps its
historical name `FO4RecordEditor.exe` so existing MO2 entries and client configurations keep working.

## Wiring it up

Add the server to your client's MCP configuration. Clients that read a project-level `.mcp.json`
use this shape (a ready-made copy ships next to the exe as `mcp.sample.json`):

```jsonc
{
  "mcpServers": {
    "fo4editor": {
      "type": "stdio",
      "command": "C:\\Tools\\FO4RecordEditor\\FO4RecordEditor.exe",
      "args": ["--mcp", "--mo2", "C:\\Modlists\\My Modlist"]
    }
  }
}
```

Use **backslashes, doubled**: it is JSON, so `C:\Tools` is written `C:\\Tools`. Use the absolute path
to the exe. If your client supports an `env` object next to `args`, that is the place for the
environment variables listed under "Helper programs and settings".

### Arguments

| Argument | What it does |
|---|---|
| `--mcp` | Run as a stdio MCP server. No window opens. |
| `--mo2 <instance>` | Load the active profile of a Mod Organizer 2 instance on the first tool call rather than before serving. The instance is the folder that holds `ModOrganizer.ini` and `profiles\`. |
| `--health` | Print a JSON status and exit. A quick check that the exe starts at all. |

The other flags (`--window`, `--browser`, `--headless`, `--port`, `--web-root`) start the desktop
editor and are not used for MCP.

`--mo2` reads the profile named by `selected_profile` in `ModOrganizer.ini` and finds each plugin the
way MO2's virtual file system would: the game `Data` folder, then enabled mods from lowest to highest
priority, then the overwrite folder. It assembles the load order with the official and Creation Club
plugins included and loads every enabled plugin, so winners and conflicts resolve the way they do in
game. The load, and recording the instance path in `settings.json`, happen on that first tool call, so
the first call takes as long as the load does.

A missing instance folder is checked at startup: the process prints the reason to stderr and exits
with code 6. Any other load failure (a profile without `plugins.txt`, or a load order the engine would
refuse, such as a missing master) also prints the reason to stderr, and every tool call returns that
error and tries the load again until it succeeds. MCP clients show stderr in their server log.

Without `--mo2` the server starts with nothing loaded. `open_plugin` still loads a plugin by absolute
path, and `reload_mo2_profile` loads the instance saved in `settings.json` (the one the desktop editor
used last), if there is one. `create_plugin` needs a loaded MO2 profile, because new plugins are
written to its overwrite folder.

### Confirming it connected

Restart the client, or use its reconnect action for MCP servers. Then ask the assistant to call
`list_plugins`: if you get your load order back, it works. Each row has a `Pending` flag for unsaved
edits. (`ping` is a protocol message that clients use for their own health checks, not a tool; asking
the assistant to "call ping" will not work.)

> **After you rebuild or replace the exe, the running server is the old code.** Stop every
> `FO4RecordEditor.exe` process, then reconnect. Skipping the reconnect is the single most common way
> to spend twenty minutes debugging a fix that already worked.

### Reading results

A failed plugin or host call (the first two tables below) is a tool error (`isError: true`) whose text
is `ErrorType: message`, for example `PluginChangedOnDisk: ...`. The plugin tools check their
arguments strictly: an unknown, missing or mistyped argument is rejected before anything runs.

The Papyrus, NIF, material, audio and archive tools return failures as an ordinary result instead
(`isError` stays false). The text starts with `Error:`, except that the Papyrus tools answer with a
plain sentence such as `Source not found: ...` and `material_inspect` returns an `error` key. Read the
result text, not just the error flag.

---

## Tool reference

The **Writes** column says what a call can change: **no** (nothing), **memory** (the loaded plugins or
their unsaved edits; no plugin file is touched) or **disk** (it writes files). A plugin with unsaved
edits shows `Pending: true` until it is saved or discarded. Argument names are exact: the plugin tools use
`snake_case`, the audio and archive tools use `camelCase`. Arguments in brackets are optional.

`plugin` is always a plugin file name such as `MyMod.esp`. A form key is written `001234:Fallout4.esm`:
six hex digits, a colon and the plugin that owns the record.

### Plugins and records

| Tool | Arguments | Writes | What it does |
|---|---|---|---|
| `list_plugins` | none | no | One row per loaded plugin: `Key` (file name), `FilePath`, `Pending`, plus the tree fields the editor UI uses. |
| `open_plugin` | `path` | memory | Loads one `.esp`, `.esm` or `.esl` by absolute path. Opening a plugin that is already loaded refreshes it, but is refused while it has pending edits (`PluginHasPendingChanges`); a different file with the same name is refused (`PluginAlreadyLoaded`). |
| `create_plugin` | `plugin` | disk | Writes a new empty plugin into the active profile's overwrite folder and loads it. `plugin` is a file name, not a path. `.esp` is added when it has no plugin extension, and an `.esm` or `.esl` name sets the master or light flag. Refuses official and Creation Club names, names already loaded, files that already exist and invalid file names. |
| `create_record` | `plugin`, `type`, `payload` | memory | Creates a record from a four-letter signature and returns its `formKey`. The payload fields are listed under "create_record payloads". |
| `create_placed_object` | `plugin`, `cell`, `baseObject`, `x`, `y`, `z` [`rotX`, `rotY`, `rotZ`, `persistent`, `initially_disabled`] | memory | Adds a placed reference (REFR) over `baseObject` to a CELL and returns its `formKey`. `cell` and `baseObject` are FormKeys. `cell` must be a CELL this plugin owns (copy the cell as an override first if it is a master's); `baseObject` must be in this plugin or one of its masters. `rotX`, `rotY` and `rotZ` are radians and default to 0. `persistent` defaults to `true` (the cell's persistent list); `false` puts it in the temporary list. `initially_disabled` defaults to `false`. In an exterior cell `persistent` must be `false` and `x` and `y` must lie inside the cell's grid. The cell's child group is created when missing. |
| `create_interior_cell` | `plugin`, `editor_id` | memory | Creates an interior CELL with the given editor ID and returns its `formKey`. Refused when `editor_id` is empty or matches a CELL in this plugin, ignoring ASCII case. |
| `get_record` | `form_key` | no | Every loaded version of a record: `Winner`, `Plugins`, per-field `Rows` and an overall `Level` of `onlyone`, `override` or `conflict`. Includes pending edits. `RecordNotFound` when no loaded plugin has it. |
| `get_record_fields` | `plugin`, `form_key` | no | Field paths and values of one plugin's version of the record (`RecordVersionNotFound` if that plugin has none). |
| `list_record_types` | `plugin` | no | The record signatures in a plugin with counts, as strings such as `EYES (3)`. |
| `list_records` | `type` [`filter`, `limit`, `offset`] | no | Winning records of one signature across the loaded plugins. `filter` is a case-insensitive substring of the form key or EditorID. Rows: `FormKey`, `EditorID`, `Type`, `Plugin` (the winner). `limit` defaults to 200 and is capped at 5000. |
| `list_records_summary` | `plugin`, `type` [`limit`, `offset`] | no | The records of one signature that a plugin contains (added and overridden) as a grid: `columns`, `rows` (`formKey`, `editorId`, `cells`, `editable`), `total` and `offset`. The column paths are what `set_field` takes as `path`. `limit` defaults to 200 and is capped at 1000. |
| `list_plugin_worklist` | `plugin` [`filter`, `type`, `limit`, `offset`] | no | Every record the plugin adds or overrides, with override depth, winner and later overriding plugins. `filter` is `all` (default), `added`, `overrides`, `winning`, `losing` or `conflicted`; `type` is one signature. Returns `Total`, `Offset`, `Counts` and `Rows`. `limit` defaults to 200 and is capped at 5000. |
| `search_records` | `query` [`type`] | no | Case-insensitive substring search over form keys, EditorIDs and signatures across all loaded plugins; `type` limits it to one signature. At most 500 rows. |
| `set_field` | `plugin`, `form_key`, `path`, `value` | memory | Sets one field using a column path from `list_records_summary`, such as `EYES\Full`. `value` is always text. Paths that are not editable columns are refused (for example with `FieldWriteUnsupported`), and a value that does not fit leaves nothing changed. A form-link field checks that the target is loaded and of an allowed record type (`NULL` only where the field allows it), and adds the target's plugin to the edited plugin's masters when needed. |
| `save_plugin` | `plugin` | disk | Writes the plugin atomically to the path it was loaded from. No other path can be chosen over MCP. Refused for read-only plugins and for files changed on disk since loading. |
| `discard_plugin` | `plugin` | memory | Drops the plugin's pending edits by re-reading the plugin from disk. Refuses with `PluginChangedOnDisk` when the file changed since it was loaded, and with `PluginReadOnly` for a protected or game-Data plugin, leaving the edits pending. Writes nothing. |
| `list_masters` | `plugin` | no | The plugin's declared masters (`index`, `name`, `size`, `used`, `kind` of `light` or `full`) and whether the plugin itself is light. Returns `{}` if the plugin is not loaded. |

#### create_record payloads

`type` is the signature and `payload` is an object. These eighteen types have a verified layout. A payload
that does not fit is rejected with `InvalidRecordCreationPayload`, and the message names the field.
`editorId` may be written `editor_id`.

| Type | Required fields | Optional fields |
|---|---|---|
| `ARMO` | none | `editorId`, `maleWorldModel`, `maleIcon`, `maleMessageIcon`, `femaleWorldModel`, `femaleIcon`, `femaleMessageIcon`, `value`, `weight`, `health` |
| `ARMA` | none | `editorId`, `maleBipedModel`, `femaleBipedModel`, `maleFirstPersonModel`, `femaleFirstPersonModel`, `malePriority`, `femalePriority`, `maleWeightSlider`, `femaleWeightSlider`, `detectionSoundValue`, `weaponAdjust` |
| `CELL` | `worldspace` (a form key), `x`, `y` | `editorId`. Creates an exterior cell. |
| `EYES` | `fullText`, `icon` | `editorId`, `dataFlags`, `recordFlags` |
| `LVSP` | `entries`: a list of objects, each with a `spell` form key that resolves to a loaded `SPEL` or `LVSP` | `chanceNone`, `maxCount`, `flags`, `editorId`; per entry `level`, `count`, `chanceNone` |
| `LVLI`, `LVLN` | none | `editorId`; `chanceNone`, `maxCount` and `flags` (each 0..255, `flags` uses only 0x01, 0x02 and 0x04). Creates an empty list; entries are added afterwards. A local entry must reference a record of a type the list accepts; references into other plugins are not type-checked. |
| `FACT` | none | `editorId`, `fullName` (inline text; refused in localized plugins), `flags` (u32, bits 0x00000001 through 0x00010000). Creates a faction with no relations or ranks. |
| `RACE` | `description`, `maleHeight`, `femaleHeight` | `editorId`, `fullName`, `flags`, `flags2`, `size`, `accelerationRate`, `decelerationRate`, `injuredHealthPct` |
| `QUST`, `SPEL`, `MGEF`, `FLST`, `MESG` | `editorId` (non-empty; `editor_id` accepted) | none. Builds the schema's required subrecords from their registered defaults: `QUST` takes a zeroed `DNAM`, `MESG` an empty `DESC` plus `INAM` and `DNAM`, `MGEF` a zeroed `DATA`, `SPEL` `OBND`, `DESC` and `SPIT`, and `FLST` carries only its `EDID`. Nothing else is set at creation. |
| `GLOB` | `editorId` (non-empty), `type` (`short`, `long`, `float` or `boolean`) | none. The type is the required `FNAM` byte the schema has no default for; `FLTV` is created as a zero `f32`. |
| `PROJ`, `EXPL`, `HAZD` | none | `editorId`, `name`, `bounds` (six integers), and the type's own numbers and form-key links, for example `speed`, `range` and `explosionType` on `PROJ`; `force`, `damage` and `innerRadius` on `EXPL`; `radius`, `lifetime` and `spell` on `HAZD` |

Any other signature is refused (`RecordCreationRequiresSchema`) unless the payload contains
`"schema": true`. The record is then built from the schema's defaults, with an optional `editorId`, and
only for record types the schema layer can create on its own (`RecordNotAuthorable`, `RecordNoDefault`).

### Typed records

These read the winning version of a leveled list, NPC_, FACT or WEAP record and edit one plugin's version in memory. An edit needs `plugin` to already hold the record; call `save_plugin` to persist. `op` is `add`, `set` or `remove`. A refused edit leaves the plugin unchanged.

| Tool | Arguments | Writes | What it does |
|---|---|---|---|
| `get_leveled_list` | `form_key` | no | An LVLI, LVLN or LVSP record's chance none, max count, flags, use global and entries. |
| `set_leveled_list_settings` | `plugin`, `form_key`, `chance_none`, `max_count`, `flags` | memory | Sets the chance none, max count and flags bytes (0-255). |
| `edit_leveled_list_entry` | `plugin`, `form_key`, `op`, `index`, [`value`] | memory | Adds (inserted at `index`), sets or removes one entry. `value` is `{level, item, count, chanceNone, extra}` and is required for `add` and `set`; `set` keeps the entry's reserved bytes. |
| `get_npc` | `form_key` | no | An NPC_ record's factions, perks and inventory. |
| `edit_npc_entries` | `plugin`, `form_key`, `list`, `op`, [`index`], [`value`] | memory | Adds, sets or removes one entry; `list` is `factions`, `perks` or `inventory`. `index` is required for `set` and `remove`, `value` for `add` and `set`. |
| `get_faction` | `form_key` | no | A FACT record's relations and ranks. A localized rank title reads as `{"stringId": n}`. |
| `edit_faction_entries` | `plugin`, `form_key`, `list`, `op`, [`index`], [`value`] | memory | Adds, sets or removes one relation or rank; `list` is `relations` or `ranks`. |
| `add_sound_file` | `plugin`, `form_key`, `file` | memory | Adds one ANAM file name to an SNDR sound descriptor, after the last file and before the descriptors. The result names the new index. Call `save_plugin` to persist. |
| `add_package_procedure` | `plugin`, `form_key`, `parent_index`, `procedure` | memory | Adds one leaf procedure under a decoded PACK procedure-tree branch and recomputes the branch counts. A procedure with a verified data-input count gets that many data inputs (existing inputs are reused in block order, missing ones are created as Bool) and PKC2 indexes; an unknown procedure, a templated package that would need inputs created, and an unextendable input block are refused. The result names the new node index. Call `save_plugin` to persist. |
| `remove_package_procedure` | `plugin`, `form_key`, `node_index` | memory | Removes one leaf procedure node, its PKC2 indexes and PFO2/PFOR override, and every data input only that leaf referenced, then reindexes the remaining input ids and PKC2 references. A container, the root, an unattributable leaf payload, duplicate input ids and a data input still referenced elsewhere are refused. Call `save_plugin` to persist. |
| `remove_sound_file` | `plugin`, `form_key`, `file_index` | memory | Removes one SNDR file name by its ANAM index. Call `save_plugin` to persist. |
| `set_sound_base` | `plugin`, `form_key` [`target`] | memory | Sets or removes the BNAM base-descriptor link of an SNDR. Only an AutoWeapon descriptor carries that arm; an empty or omitted `target` removes the link. Call `save_plugin` to persist. |
| `create_sound` | `plugin`, `signature`, `editor_id` | memory | Creates a SOUN sound marker or an FSTS footstep set from proved defaults; any other signature is refused. Call `save_plugin` to persist. |
| `add_script` | `plugin`, `form_key`, `script` [`flags`, `index`] | memory | Adds one script to the record's VMAD, creating an empty VMAD when the record has none. `script` is the Papyrus script name, `flags` the optional script flags byte and `index` the optional script-list position; the result names the new index. Call `save_plugin` to persist. |
| `remove_script` | `plugin`, `form_key`, `script_index` | memory | Removes one script and its properties by script index. Call `save_plugin` to persist. |
| `add_script_property` | `plugin`, `form_key`, `script_index`, `name`, `type` [`flags`, `index`] | memory | Adds one property to a script. `type` is the VMAD property type byte (1 object, 2 string, 3 int, 4 float, 5 bool, 7 struct, 11-15 arrays, 17 struct array); `flags` is the optional property flags byte and `index` the optional property-list position. Set the value with `set_field` or the typed VMAD setters. Call `save_plugin` to persist. |
| `remove_script_property` | `plugin`, `form_key`, `script_index`, `property_index` | memory | Removes one property by property index. Call `save_plugin` to persist. |
| `get_weapon_damage_types` | `form_key` | no | A WEAP record's damage types (read-only). |

### Profile and host state

| Tool | Arguments | Writes | What it does |
|---|---|---|---|
| `native_status` | none | no | Host status: `pluginBackend`, `pluginRead`, `pluginWrite`, `archiveReadBackend` and `embeddedWindow`. `materialCodec` and `audioTools` are fixed at `true` and do not check for helper programs. |
| `get_mo2_status` | none | no | `loaded`; for a loaded profile also `instancePath`, `profile`, `dataFolder`, `overwriteFolder`, `pluginCount`, `missingPluginCount` and `loadedAtUnixMs`. Always `pendingPlugins` (plugins with unsaved edits) and staleness flags: `staleConfig` (`ModOrganizer.ini` changed), `staleLoadOrder` (`plugins.txt`, `modlist.txt` or `loadorder.txt` changed) and `stale` (either). |
| `reload_mo2_profile` | [`discard_pending`] | memory | Reloads the enabled plugins from the configured profile, replacing everything that is loaded (plugins added with `open_plugin` included), and returns the `get_mo2_status` fields. Refused with `PluginHasPendingChanges` while any plugin has unsaved edits, unless `discard_pending` is `true`, which throws those edits away. Also saves the instance path to `settings.json`. |
| `release_memory` | [`drop_conflict_graph`] | memory | Pages the process working set out to the OS after a large load, save or reload. Plugin data stays loaded and pages back in on the next access, so use it when the editor is idle and memory use matters. The result reports the working set before and after the trim. `drop_conflict_graph: true` also drops the cached conflict/reference graph (the largest optional allocation); it rebuilds on the next conflict or reference query. The reference half of that graph is also released automatically after an idle period. |
| `get_settings` | none | no | The current settings: folders, tool paths and flags, including values that came from environment variables. Secrets are redacted. |

### Papyrus

| Tool | Arguments | Writes | What it does |
|---|---|---|---|
| `compile_papyrus` | `source` [`output`, `imports`, `flags`, `all`, `optimize`, `release`, `engine`, `debug_info`, `compiler_path`] | disk | Compiles `.psc` source to `.pex`: one file, or a folder. `engine` is `auto` (default: the Creation Kit compiler if `PapyrusCompiler.exe` is found, otherwise the built-in compiler), `builtin` or `creationkit`. `imports` is a semicolon-separated list of extra source roots. The `.pex` files go to `output`, by default next to the source. `optimize` applies to the Creation Kit compiler only. The built-in compiler ends with a `RESULT: X succeeded, Y failed` summary; the Creation Kit route returns the compiler's own output and exit code. |
| `decompile_papyrus` | `source` [`output`, `assembly`, `write`] | no, or disk | Decompiles `.pex` to `.psc` (a `.pas` bytecode listing with `assembly`). A single file comes back as text unless `write` is `true` or `output` is given. A folder is always written, into `output` or by default the source folder, with namespaced scripts in namespace subfolders. |
| `papyrus_check` | `source` [`semantic`, `imports`] | no | Checks `.psc` source with the built-in front end, with no Creation Kit: syntax, then (with `semantic`, default `true`) name resolution and types. Reports `file(line,col): error CODE: message`. Files that refer to scripts not found on the import roots are counted separately. |
| `papyrus_function_lookup` | `function` [`script`] | no | Looks a function up in the offline Creation Kit Wiki mirror. |
| `papyrus_script_info` | `script` | no | A script's overview (Extends, global and member functions, events) from the same mirror. |

`.pas` is assembly, not source: `compile_papyrus` compiles `.psc`.

### NIFs, materials, audio and archives

| Tool | Arguments | Writes | What it does |
|---|---|---|---|
| `nif_inspect`, `nif_geometry` | `path` | no | Read a NIF: summary and geometry, in process through Radicle. |
| `nif_verify`, `nif_tree` | `path` | no | Verification and block tree, in process through Radicle. |
| `material_inspect` | `path` | no | Reads a BGSM or BGEM material: `fileName`, `format`, `version` and its named `fields`. |
| `material_set_fields` | `path`, `fields` [`output`] | disk | Sets named material fields (`fields` maps a name to a text value) and writes the result atomically to `output`, or back to `path` when no output is given. A conditional field such as `EmittanceColor` can be set only when its switch (`EmitEnabled`) is already true in the saved file: set the switch in one call and the field in the next. |
| `audio_convert_to_xwm` | `source` [`output`, `bitrateBps`] | disk | Converts one audio or video file, or every supported file under a folder, to Fallout 4 XWM. Without `output` each `.xwm` goes next to its source; with a folder source, `output` is a folder that mirrors the layout. Needs xWMAEncode, plus ffmpeg for anything that is not PCM WAV. |
| `audio_convert_from_xwm` | `source` [`output`, `targetExt`] | disk | Decodes one XWM file to WAV (default) or another format ffmpeg supports. Without `output` the result goes next to the source. Needs xWMAEncode, plus ffmpeg for anything but WAV. |
| `audio_make_fuz` | `audioSource`, `fuzOutput` [`lipPath`, `noLip`] | disk | Packs audio into a FUZ together with a LIP file. Audio that is not already `.xwm` is converted first. Without `lipPath` it looks for a `.lip` next to the audio; `noLip` leaves the LIP out. Needs BmlFuzEncode (and the XWM tools for the conversion). |
| `audio_extract_fuz` | `fuzPath` [`xwmOutput`, `lipOutput`, `alsoWav`] | disk | Splits a FUZ into XWM and LIP files, next to the FUZ unless outputs are given; `alsoWav` also decodes a WAV. Needs BmlFuzDecode (and xWMAEncode for `alsoWav`). |
| `archive_pack` | `sources`, `output`, `rootDir` [`format`, `compress`] | disk | Packs folders into a BA2 with `Archive2.exe`. `sources` is a list of folders and `rootDir` is Archive2's root folder (`-r`). `format` is `General` (default) or `DDS`; `compress` defaults to `true`. `.ba2` is added to `output` when missing and missing parent folders are created. Gives up after 10 minutes. |

Materials go through Radicle's BGSM and BGEM codec. Its round trip is checked against the vanilla
`Fallout4 - Materials.ba2` (6,623 `.bgsm` and 283 `.bgem` files when the figure was first recorded):
reading a file and writing it back must reproduce its bytes. `Radicle/tests/test_material_file_real.cpp`
runs that check when `FO4_DATA_DIR` names a Fallout 4 `Data` folder.

The audio tools refuse to overwrite their own source file. The authoritative list is whatever the
server's `tools/list` returns; this reference follows `native/apps/fo4ide/McpTools.cpp`.

---

## Write safety

These rules are enforced by the backend, not by the client, so they hold whichever AI is connected.

- **Edits stay in memory until you save.** `create_record` and `set_field` work on a copy of the
  plugin and swap it in only after the change has validated, so a rejected call leaves nothing
  half-applied. The file on disk is untouched until `save_plugin`, and `discard_plugin` throws the
  edits away. `list_plugins` and `get_mo2_status` show which plugins are dirty. (`create_plugin` is the
  exception: it writes the new empty file at once.)
- **Saves are atomic and in place.** The plugin is written to a temporary file next to the original,
  checked, then moved over it, at the path the plugin was loaded from. There is no "save as" over MCP.
- **Files changed behind the server's back are not overwritten.** Every loaded plugin remembers the
  modification time it had when it was read. If the file on disk has a different time later, edits and
  saves fail with `PluginChangedOnDisk`. Discard any pending edits, then `open_plugin` the plugin again
  (or `reload_mo2_profile`) to pick up the new file.
- **Game files are read-only.** `Fallout4.esm`, the official DLC plugins, any plugin whose file name
  starts with `cc` (the Creation Club naming) and anything inside the game's `Data` folder refuse edits
  and saves (`PluginReadOnly`). `create_plugin` refuses those names too.
- **New plugins go to MO2's overwrite folder and nowhere else.** `create_plugin` takes a file name,
  never a path, and refuses a name that already exists there.
- **Unproven structures fail closed.** `create_record` builds only record types it has a verified
  layout for, and `set_field` refuses paths it cannot validate, rather than guessing.
- **Reloading cannot silently lose work.** `reload_mo2_profile` fails while any plugin has unsaved
  edits unless `discard_pending` is `true`.

The MCP tools cannot copy a record into another plugin as an override; the desktop editor can. Edit
the plugin you own directly rather than stacking a fix plugin on top, unless you specifically want an
override: fewer plugins, fewer conflicts, fewer masters.

---

## Helper programs and settings

Settings live in `%APPDATA%\FO4RecordEditor\settings.json` (on Linux
`$XDG_CONFIG_HOME/FO4RecordEditor/settings.json`, falling back to `~/.config`); `get_settings` shows
them. The log is `logs\FO4IDE.log` in the same folder (5 MiB per file, the current file plus three rotated ones).

Some tools run helper programs. Each is looked for in this order, and the first hit wins: its
environment variable, the matching `settings.json` key (where there is one), the location in the last
column, then `PATH` for programs.

| Helper | Used by | Environment variable | `settings.json` key | Location |
|---|---|---|---|---|
| Creation Kit Wiki mirror (a folder) | `papyrus_function_lookup`, `papyrus_script_info` | `CK_WIKI_PATH` | `CkWikiPath` | `tools\ckwiki\fallout4` |
| `xWMAEncode.exe` | `audio_convert_to_xwm`, `audio_convert_from_xwm` | `XWMAENCODE_PATH` | `XwmaEncodePath` | `tools\audio\` |
| ffmpeg | `audio_convert_to_xwm` (input that is not PCM WAV), `audio_convert_from_xwm` (output other than WAV) | `FFMPEG_PATH` | `FfmpegPath` | `tools\audio\` |
| `BmlFuzEncode.exe` | `audio_make_fuz` | `BMLFUZENCODE_PATH` | none | `tools\audio\` |
| `BmlFuzDecode.exe` | `audio_extract_fuz` | `BMLFUZDECODE_PATH` | none | `tools\audio\` |
| `PapyrusCompiler.exe` | `compile_papyrus` with the Creation Kit engine | `PAPYRUS_COMPILER_PATH` | `PapyrusCompilerPath` | `<Fallout 4>\Papyrus Compiler\` |
| `Archive2.exe` | `archive_pack` | `ARCHIVE2_PATH` | `Archive2Path` | `<Fallout 4>\Tools\Archive2\` |

`<Fallout 4>` is the install folder from the `Fallout4Path` setting, `FALLOUT4_PATH`, the registry or
a Steam library. `PapyrusCompiler.exe` and `Archive2.exe` come with the Creation Kit and are not
shipped in the package. The package always includes texconv and ffmpeg. The Creation Kit Wiki mirror
and the xWMAEncode and BmlFuz tools are included only if they were available at build time; a missing
one disables just the tools in its row. The NIF verbs need no helper program: Radicle reads, fixes and
writes NIFs inside the server.

The built-in Papyrus engine (`compile_papyrus` with `builtin`, or with `auto` when there is no
Creation Kit compiler, and `papyrus_check`) needs no `PapyrusCompiler.exe`, but it does need the
vanilla script **sources** to resolve types such as `Form` and `Actor`. Give it the folder, normally
`<Fallout 4>\Data\Scripts\Source\Base`, through the `imports` argument or by setting
`PAPYRUS_BASE_IMPORTS` (semicolon-separated) in the server's environment. The built-in engine reads
that environment variable and `imports`; the `PapyrusBaseImports` setting and `tools\papyrus\Base`
only feed the Creation Kit compiler route.

---

## When something is wrong

- **A tool is missing, or a fix has no effect.** The running server is an older build, or it never
  started. Stop every `FO4RecordEditor.exe`, reconnect, and check the client's server log.
- **The server exits at once.** Read stderr in the client's log. `--mo2 requires an instance folder`
  or `MO2 load failed: instance folder not found` (exit code 6) means the instance path is missing or
  wrong.
- **Every tool returns `MO2 load failed: ...`.** The profile has no `plugins.txt`, or its load order is
  invalid. Fix it, then call the tool again; the load is retried on every call until it succeeds.
- **`list_plugins` is empty.** You started without `--mo2` (call `reload_mo2_profile` or
  `open_plugin`), or the profile has no enabled plugins.
- **One plugin is missing from `list_plugins`.** It is not enabled in the profile, its file was not
  found (`get_mo2_status` shows `missingPluginCount`), or it could not be read. `open_plugin` on its
  path shows the read error.
- **`PluginChangedOnDisk`.** Something else rewrote the plugin after it was loaded. See "Write
  safety".
- **`PluginHasPendingChanges`.** The call would throw away unsaved edits. Save or discard them first.
- **`PluginReadOnly`.** The plugin is protected (see "Write safety"). Edit a plugin of your own
  instead.
- **`compile_papyrus` says `PapyrusCompiler.exe` was not found.** You asked for
  `engine="creationkit"` and there is none. Install the Creation Kit, set `PAPYRUS_COMPILER_PATH` (the
  compiler is Bethesda's and is not shipped here), or use `engine="builtin"`.
- **The built-in engine reports every vanilla type as unknown.** It has no base script sources to
  resolve against. Set `PAPYRUS_BASE_IMPORTS` or pass `imports`, as described above.
- **NIF import with box collision says `box collision is not supported yet`.** Box collision needs
the HKX writer (issue #1048), which is separate from the read-only HKX reader in #814. Every other
NIF verb runs in process: fix repairs tangents, adds a missing
BSX flag and trims texture and material name whitespace; it does not change shader or BSX flags, and it
leaves the tangents of shapes without normal, tangent and UV attributes alone.
- **`archive_pack` says `Archive2.exe is not configured`.** Install the Creation Kit or set
  `ARCHIVE2_PATH`.
- **An audio tool answers `Error: set ..._PATH or place ... in tools/audio`.** A helper program is
  missing. Set the environment variable the message names, or put the program in `tools\audio`.
- **Nothing starts, with no window and no output.** Run the exe with `--health` from a terminal. On a
  clean Windows install the usual cause is a missing Microsoft Visual C++ 2015-2022 Redistributable
  (x64), which the package needs.
