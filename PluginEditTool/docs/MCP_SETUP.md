# FO4IDE as an MCP server

## What this is

MCP (Model Context Protocol) is a standard way to hand an AI assistant a set of tools it can call.
FO4IDE implements it: launched with `--mcp`, `FO4RecordEditor.exe` opens no window and speaks
line-delimited JSON-RPC on stdin and stdout (protocol version 2024-11-05, server name `fo4editor`).
It advertises **228 tools**: 81 hand-written tools (79 `if (!add(` registrations in `McpTools.cpp` plus `material_set_fields` and `archive_pack`) and 147 catalog
tools. The 147 catalog tools are the editor parity set: 36 read-side (`McpCatalogRead.cpp`), 79 edit-side
(`McpCatalogEdit.cpp`) and 32 service-side (`McpCatalogService.cpp`: Papyrus source, string tables, archives,
blueprint graphs and `load_script`).

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
| `get_record` | `form_key` | no | Compact record details: `FormKey`, the raw form id, `Signature`, `EditorId`, winning plugin (`Winner`), `BaseForm` and `OverrideCount`. Per-plugin versions are in `get_record_plugin_matrix`. `RecordNotFound` when no loaded plugin has it. |
| `get_conflict_matrix` | `form_key` | no | Full conflict matrix for a record: every loaded version and its per-field `Rows` with `Winner`, `Plugins` and an overall `Level` of `onlyone`, `override` or `conflict`. Large payloads are possible on big load orders; prefer `get_record` for a compact view. |
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
| `add_master` | `plugin`, `master_file` | memory | Declares one master on a plugin, fresh or existing, so records whose links live in that master can be authored next. `master_file` is a plugin file name, not a path. Masters are load-order significant: a master that loads before an already-declared master is refused with `MasterOrder`, and so is one that does not load before the plugin. Re-declaring an existing master is a no-op (`No new masters were needed`). `SelfMaster`, `MasterNotLoaded` for a full or light-unknown master that is not loaded, and `PluginReadOnly` for protected or game-Data plugins. A refusal leaves the plugin unchanged. Call `save_plugin` to persist. |
| `add_masters` | `plugin`, `master_file` | memory | Same as `add_master` for several masters in one call: `master_file` is a comma-separated list and the declaration order is the order given. |

#### create_record payloads

`type` is the signature and `payload` is an object. These twenty types have a verified layout. A payload
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
| `QUST`, `SPEL`, `MGEF`, `FLST`, `MESG` | `editorId` (non-empty; `editor_id` accepted) | none. Builds the schema's required subrecords from their registered defaults: `QUST` takes a zeroed `DNAM` plus an ANAM Next Alias ID of zero; its optional `name` (an inline title, refused in a localized plugin) becomes FULL, and optional `flags` (u16), `priority` and `type` (u8) fill DNAM. `MESG` an empty `DESC` plus `INAM` and `DNAM`, `MGEF` a zeroed `DATA`, `SPEL` `OBND`, `DESC` and `SPIT`, and `FLST` carries only its `EDID`. Nothing else is set at creation. |
| `GLOB` | `editorId` (non-empty), `type` (`short`, `long`, `float` or `boolean`) | none. The type is the required `FNAM` byte the schema has no default for; `FLTV` is created as a zero `f32`. |
| `PROJ`, `EXPL`, `HAZD` | none | `editorId`, `name`, `bounds` (six integers), and the type's own numbers and form-key links, for example `speed`, `range` and `explosionType` on `PROJ`; `force`, `damage` and `innerRadius` on `EXPL`; `radius`, `lifetime` and `spell` on `HAZD` |
| `PERK` | `editorId` (non-empty; `editor_id` accepted) | `full` and `desc` (inline text in a non-localized plugin; in a localized plugin they become new string IDs in the staged string tables, so call `strings_save` to persist them; `language` picks the table language, default `en`), `trait` (0 or 1, default 0), `level` (0-255, default 0), `ranks` (0-255, default 1), `playable` (0 or 1, default 1), `conditions` (the `set_conditions` array; the root condition list), `nextPerk` (a PERK FormKey), `sound` (an SNDR FormKey), `swf` (text, the FNAM), `effects` (an array in the `set_perk_effects` shape; `quest` must be a loaded QUST, `ability` a loaded SPEL, and the typed parameter form and actor value a loaded record of the type the effect expects) |
| `PACK` | `editorId` (non-empty; `editor_id` accepted), `location` (an object: `type` is `reference`, `cell`, `near_package_start`, `near_editor_location` or `near_self`; `form` is a FormKey and is required for `reference` and `cell` and refused for the others) | `location.radius` (signed 32-bit, default 0), `speed` (`walk`, `jog`, `run` or `fast_walk`, default `jog`), `preferPreferredPath`, `useSoftRadius` and `ignoreMarkerOrientation` (booleans, default false). Builds a Travel procedure with a Location input and three Bool inputs. |

Any other signature is refused (`RecordCreationRequiresSchema`) unless the payload contains
`"schema": true`. The record is then built from the schema's defaults, with an optional `editorId`, and
only for record types the schema layer can create on its own (`RecordNotAuthorable`, `RecordNoDefault`).

### Typed records

These read the winning version of a leveled list, NPC_, FACT or WEAP record (PERK reads `plugin`'s version) and edit one plugin's version in memory. An edit needs `plugin` to already hold the record; call `save_plugin` to persist. `op` is `add`, `set` or `remove`. A refused edit leaves the plugin unchanged.

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
| `set_script_value` | `plugin`, `form_key`, `script_index`, `property_index`, `kind` [`value` `alias` `element`] | memory | Sets one VMAD property value. `kind` is `object`, `string`, `int`, `float` or `bool` and must match the stored property type; for `object` the `value` is a FormKey and `alias` an optional signed 16-bit index (default -1); for `int`/`float`/`bool` the value is text. `element` addresses an array element and defaults to 0. Refusals name the mismatch (`PropertyTypeMismatch`, `PropertyIndexOutOfRange`, `ElementIndexOutOfRange`, `InvalidPropertyValue`); call `save_plugin` to persist. |
| `add_alias_script` | `plugin`, `form_key`, `alias_id`, `script` [`flags` `index`] | memory | Adds one script to a QUST quest alias. An alias with no entry is created at its ascending `alias_id` position with one script (`index` must be 0). `script` is the Papyrus name; `flags` the optional script flags byte and `index` the optional script-list position within the alias. Call `save_plugin` to persist. |
| `remove_alias_script` | `plugin`, `form_key`, `alias_id`, `script_index` | memory | Removes one script of a QUST quest alias by script index. Removing the last script removes the whole alias entry. Call `save_plugin` to persist. |
| `add_alias_script_property` | `plugin`, `form_key`, `alias_id`, `script_index`, `name`, `type` [`flags`, `index`] | memory | Adds one property to a script of a QUST quest alias; `type` and the optional `flags` and `index` follow `add_script_property`. Set the value with `set_alias_script_value`. Call `save_plugin` to persist. |
| `remove_alias_script_property` | `plugin`, `form_key`, `alias_id`, `script_index`, `property_index` | memory | Removes one property of a QUST quest alias script by property index. Call `save_plugin` to persist. |
| `set_alias_script_value` | `plugin`, `form_key`, `alias_id`, `script_index`, `property_index`, `kind` [`value` `alias` `element`] | memory | Sets one VMAD property value of a QUST quest alias script; arguments and refusals follow `set_script_value`, with `QuestAliasMissing` when the quest has no alias entry for `alias_id`. Call `save_plugin` to persist. |
| `get_weapon_damage_types` | `form_key` | no | A WEAP record's damage types (read-only). |
| `get_perk_effects` | `plugin`, `form_key` | no | A PERK record's effects: `type`, `rank`, `priority`, then `quest` and `questStage`, `ability`, or `entryPoint` and `function`; each effect's condition `tabs`; and `parameters` with `data` (hex) plus the typed fields its EPFT carries: `value` (EPFT 1, 2 and 8), `value2` (EPFT 2 float pair), `form` (EPFT 3, 4 and 5), `actorValue` (EPFT 8, or EPFT 2 with an actor-value function) and `text` (EPFT 6). |
| `set_perk_effects` | `plugin`, `form_key`, `effects` | memory | Replaces the whole effect list with `effects`, in the shape `get_perk_effects` returns. Each effect's `parameters` take `data` (hex), the typed fields for its EPFT, or both when the typed fields encode exactly the bytes of `data` (a mismatch is refused, naming both), so the `get_perk_effects` output is accepted unchanged; `form` and `actorValue` accept a FormKey or an editor ID of a loaded record (the form type is checked as before: LVLI for EPFT 3, SPEL for EPFT 4 and 5, AVIF for the actor value). An effect that omits `tabs` has its condition tabs cleared. Call `save_plugin` to persist. |
| `get_perk_links` | `plugin`, `form_key` | no | A PERK record's `nextPerk` (NNAM), `sound` (SNAM), `swf` (FNAM) and the root `conditions` that gate it, in the `get_conditions` shape. Absent links read as `null`. |
| `set_perk_next` | `plugin`, `form_key`, `next` | memory | Sets the NNAM next-perk link. `next` must be a PERK FormKey; any other record, an unresolved key or an editor ID is refused by name. A link to the same PERK and an empty (null) `next` are refused as `InvalidPerkNext`. A record without NNAM gets one inserted before its FNAM or effects. Clear the link with `set_field` `PERK\NextPerk` = `NULL`. Call `save_plugin` to persist. |
| `set_perk_sound` | `plugin`, `form_key`, `sound` | memory | Sets the SNAM sound link. `sound` must be an SNDR FormKey (any other record is refused by name); an empty `sound` removes the SNAM. A missing SNAM is inserted after DATA, and an existing one is rewritten in place. Call `save_plugin` to persist. |
| `set_perk_swf` | `plugin`, `form_key`, `swf` | memory | Sets the FNAM SWF text from UTF-8 text that must be representable in Windows-1252 and contain no NUL byte; an empty `swf` removes the FNAM. A missing FNAM is inserted before the effects, and an existing one is rewritten in place. Call `save_plugin` to persist. |

PERK root conditions are written with `set_conditions` and read with `get_conditions`. `set_field` with `PERK\Sound` or `PERK\SWF` still edits only an existing SNAM or FNAM; use `set_perk_sound` and `set_perk_swf` to insert one.
| `get_magic_effects` | `plugin`, `form_key` | no | A SPEL, ALCH, ENCH or INGR record's effect list: count plus each effect's index, `effect` FormKey, `magnitude`, `area` and `duration`. |
| `add_magic_effect` | `plugin`, `form_key`, `effect`, `magnitude`, `area`, `duration` | memory | Appends one effect (EFID/EFIT) to a SPEL, ALCH, ENCH or INGR record and returns its index. `effect` must be an MGEF FormKey, `magnitude` a number, `area` and `duration` integers. Call `save_plugin` to persist. |
| `set_magic_effect` | `plugin`, `form_key`, `index`, `effect`, `magnitude`, `area`, `duration` | memory | Replaces one effect of the list by index; the record must be SPEL, ALCH, ENCH or INGR and `effect` an MGEF FormKey. Call `save_plugin` to persist. |
| `remove_magic_effect` | `plugin`, `form_key`, `index` | memory | Removes one effect of a SPEL, ALCH, ENCH or INGR record by index; the whole block is dropped when the last effect goes. Call `save_plugin` to persist. |


### Editor parity tools

These map onto the same native RPC operations the desktop editor uses, so the MCP surface now matches the visual one. `Writes` follows the rest of this document: `memory` edits must be persisted with `save_plugin` (or the operation's own save call), `files` tools write the file they name.

| Tool | Arguments | Writes | What it does |
|---|---|---|---|
| `get_conflicts` | none | no | Editor query: get conflicts. Same RPC the desktop editor uses. |
| `get_referenced_by` | `form_key` | no | Editor query: get referenced by. Same RPC the desktop editor uses. |
| `get_problems` | `form_key` | no | Editor query: get problems. Same RPC the desktop editor uses. |
| `compare_plugin_to` | `plugin`, `other_path`, `form_key` (optional) | no | Editor query: compare plugin to. Same RPC the desktop editor uses. |
| `describe_element` | `plugin`, `form_key`, `path` | no | Editor query: describe element. Same RPC the desktop editor uses. |
| `get_conditions` | `plugin`, `form_key` | no | Editor query: get conditions. Same RPC the desktop editor uses. |
| `get_conditions_at` | `plugin`, `form_key`, `path` | no | Editor query: get conditions at. Same RPC the desktop editor uses. |
| `get_quest_objectives` | `plugin`, `form_key` | no | Editor query: get quest objectives with their alias targets and flags. Each objective also returns `flagNames` (named FNAM bits set: `ORedWithPrevious`, `NoStatsTracking`) and `unknownFlagBits` (remaining raw bits, unnamed). Same RPC the desktop editor uses. |
| `get_quest_stages` | `plugin`, `form_key` | no | Editor query: get quest stages with log entries, log text and notes. Each stage also returns raw `flags`, `startUp`, `flagNames` (named INDX bits set: `RunOnStart`, `RunOnStop`, `KeepInstanceDataFromHereOn`) and `unknownFlagBits` (remaining raw bits, unnamed). Conditions are not returned. Same RPC the desktop editor uses. |
| `get_quest_aliases` | `plugin`, `form_key` | no | Editor query: get quest aliases with kind, ID, name, flags and collection entries. Same RPC the desktop editor uses. |
| `get_dialogue` | `plugin`, `quest_form_key` | no | Get a quest's dialogue branches, topics and INFO responses with conditions. INFO resetHours is in hours; resetTimeEncoded is the raw stored value. setStageOnBegin and setStageOnEnd appear only when the INFO has a parent quest stage subrecord; null means none. Reads the in-memory plugin. |
| `get_voice_path` | `plugin`, `info_form_key`, `voicetype`, [`language`], [`extension`] | no | Get the expected voice file paths of an INFO: `Sound\Voice\<plugin file>\<voicetype editor id>\<INFO local id 8 hex>_<response number>.<extension>` for every stored response number. extension is `fuz` or `hkx` (default `fuz`); language inserts an optional language directory. Reads the in-memory plugin. |
| `get_scene` | `plugin`, `scene_form_key` | no | Get a scene's flags, parent quest, template scene, index, phases with start and completion conditions, actors, actions with their payload links, and the INFO response texts of the dialogue topics its actions reference. Reads the in-memory plugin. |
| `get_condition_functions` | none | no | Editor query: get condition functions. Same RPC the desktop editor uses. |
| `get_condition_run_on_types` | none | no | Editor query: get condition run on types. Same RPC the desktop editor uses. |
| `get_condition_function_params` | none | no | Editor query: get condition function params. Same RPC the desktop editor uses. |
| `get_record_type_index` | none | no | Editor query: get record type index. Same RPC the desktop editor uses. |
| `get_containment_path` | `form_key` | no | Editor query: get containment path. Same RPC the desktop editor uses. |
| `get_record_details` | `form_key` | no | Editor query: get record details. Same RPC the desktop editor uses. |
| `get_record_plugin_matrix` | `form_key` | no | Editor query: get record plugin matrix. Same RPC the desktop editor uses. |
| `get_dependencies` | `form_key` | no | Editor query: get dependencies. Same RPC the desktop editor uses. |
| `get_history` | `form_key` | no | Editor query: get history. Same RPC the desktop editor uses. |
| `get_load_order_summary` | none | no | Editor query: get load order summary. Same RPC the desktop editor uses. |
| `get_texture` | `rel_model_path`, `rel_tex_path` | no | Editor query: get texture. Same RPC the desktop editor uses. |
| `get_placed_references` | `cell_id` | no | Editor query: get placed references. Placed-reference or cell operation. |
| `get_placed_references_at_grid` | `worldspace`, `grid_x`, `grid_y` | no | Editor query: get placed references at grid. Placed-reference or cell operation. |
| `resolve_worldspace` | `cell_id` | no | Editor query: resolve the worldspace that owns a cell. Placed-reference or cell operation. |
| `get_cell_properties` | `cell_id` | no | Editor query: get cell properties (lighting, water, owner, regions and so on) with the descriptors that `set_cell_properties` accepts. Placed-reference or cell operation. |
| `plan_cell_cleanup` | `cell_id`, `patch_plugin`, `mode`, [`max_count`], [`by_model`] | no | Editor query: plan removing duplicate or excess placed references from a cell in a patch plugin. `mode` is `dedupe` or `excess`; `max_count` is the number of non-persistent references kept in `excess` mode. Read-only: the patch is not changed. |
| `search_worldspaces` | `query`, [`limit`] | no | Editor query: search worldspaces. Placed-reference or cell operation. |
| `get_geometry_batch` | `rel_model_paths_json` | no | Editor query: get geometry batch. Same RPC the desktop editor uses. |
| `search_cells` | `query`, [`limit`] | no | Editor query: search cells. Placed-reference or cell operation. |
| `get_geometry_batch_progress` | none | no | Editor query: get geometry batch progress. Same RPC the desktop editor uses. |
| `set_components` | `plugin`, `form_key`, `components_json` | memory | Editor operation: set components. Same RPC the desktop editor uses; call save_plugin to persist in-memory edits. |
| `set_quest_aliases` | `plugin`, `form_key`, `aliases_json` | memory | Editor operation: set quest aliases. Same RPC the desktop editor uses; call save_plugin to persist in-memory edits. |
| `set_quest_stages` | `plugin`, `form_key`, `stages_json` | memory | Editor operation: set quest stages. Same RPC the desktop editor uses; call save_plugin to persist in-memory edits. |
| `set_quest_objectives` | `plugin`, `form_key`, `objectives_json` | memory | Editor operation: set quest objectives. Replaces the whole objective list. Each target names an alias by ID or name (never a reference); conditions are optional. Same RPC the desktop editor uses; call save_plugin to persist in-memory edits. |
| `set_quest_fragment` | `plugin`, `form_key`, `stage`, `log_entry_index`, `script_name`, `output_directory`, `imports_directory`, `actions_json` | memory | Editor operation: stage a quest stage fragment (script and plugin change). The stage must exist; directories must be trusted save roots. A `display` action may set `visible` (default true) and `force` (default false), emitting `SetObjectiveDisplayed(N, visible, force)`; a `complete` action may set `completed` (default true), emitting `SetObjectiveCompleted(N, completed)`. Other flags are refused; unset flags keep the one-argument call. save_plugin persists it and needs recoverable publication; discard_plugin drops a pending edit. |
| `remove_quest_fragment` | `plugin`, `form_key`, `stage`, `log_entry_index`, `script_name`, `output_directory`, `imports_directory` | memory | Editor operation: remove the quest stage fragment binding `Fragment_Stage_<stage>_Item_<log_entry_index>` from the QUST's VMAD. The binding must exist and its script must be a GardenTools managed quest fragment script (created by set_quest_fragment); unmanaged scripts are refused with `UnmanagedQuestFragmentSource` and the plugin is unchanged. save_plugin persists it; discard_plugin drops a pending edit. |
| `get_quest_fragments` | `plugin`, `form_key` | no | Editor query: list a quest's fragment bindings (stage, logEntryIndex, script, function, fragmentScript) from the plugin's in-memory version. Read-only; use it before remove_quest_fragment. |
| `get_quest_script` | `script_name`, [`imports_directory`] | no | Editor query: read a quest script's Papyrus source before `edit_quest_script`. `script_name` may be namespaced with `:`, `/` or `\` (for example `Fragments:Quests:QF_X` or `DLC01:Fragments:Quests:QF_X`). Source order: a loose `<namespace folders>/<script>.psc` in `imports_directory`, then a loose `.pex` there (decompiled), then a loose `.pex` under the Data `Scripts` folder (`archive` is `loose`), then the vanilla PEX under `scripts/<namespace>/<script>.pex` in the BA2 archives under Data (decompiled). `Fallout4 - Misc.ba2` is read first, the other archives follow in name order, archives named for textures, meshes, sounds, voices, materials, shaders, interface or animations are skipped, and the last archive holding the script wins. Returns `script`, `origin`, `archive`, `source`, `extends`, `parses` and `notes` (unreadable non-Misc archives that were skipped). The vanilla source is a decompilation: comments and doc-strings are lost, and the PEX was compiled without debug info or debug-only/beta-only code, so its header compile metadata differs. Read-only; an unknown or malformed name is refused. |
| `edit_quest_script` | `plugin`, `script_name`, `output_directory`, [`source`, `edits_json`, `imports_directory`] | memory | Editor operation: edit any existing quest script, using the same source order as `get_quest_script` (a script already staged by this plugin comes first). `source` replaces the whole text and/or `edits_json` (a JSON array of `{find, replace}` objects applied in order; each `find` must match exactly once or the call is refused) edits it. The source text uses LF line endings, so a `find` containing CRLF will not match. The vanilla source is a decompilation (comments and doc-strings lost, compiled without debug info or debug-only/beta-only code, header compile metadata differs), so review the diff before saving. The ScriptName must equal `script_name` and the script must extend Quest. It compiles against the vanilla Base, User, CreationClub and DLC roots plus `imports_directory` and refuses with the diagnostics on failure. The `.psc` and `.pex` are staged under `output_directory` (outside the game Data folder, inside a trusted save root) for `save_plugin` to publish through the journal; `discard_plugin` drops them. Quest VMAD bindings are not checked. Caveats: a script first staged by `edit_quest_script` refuses `set_quest_fragment` as already staged, while one staged by `set_quest_fragment` for the same quest can be edited and re-bound. Plugin records and quest fragment bindings are not changed. |
| `set_conditions` | `plugin`, `form_key`, `conditions_json` | memory | Editor operation: set conditions. Same RPC the desktop editor uses; call save_plugin to persist in-memory edits. |
| `add_list_item` | `plugin`, `form_key`, `field`, `value` | memory | Editor operation: add list item. Same RPC the desktop editor uses; call save_plugin to persist in-memory edits. |
| `remove_list_item` | `plugin`, `form_key`, `field`, `value` | memory | Editor operation: remove list item. Same RPC the desktop editor uses; call save_plugin to persist in-memory edits. |
| `delete_record` | `plugin`, `form_key` | memory | Editor operation: delete record. Same RPC the desktop editor uses; call save_plugin to persist in-memory edits. |
| `copy_as_override` | `source_plugin`, `form_key`, `patch_plugin`, `overwrite`, `allow_losing` (optional) | memory | Editor operation: copy as override. Same RPC the desktop editor uses; call save_plugin to persist in-memory edits. |
| `copy_as_override_many` | `items_json`, `patch_plugin`, `overwrite`, `allow_losing` (optional) | memory | Editor operation: copy as override many. Same RPC the desktop editor uses; call save_plugin to persist in-memory edits. |
| `revert_overrides` | `bad_plugin`, `patch_plugin`, [`signature`], [`contains_component`], [`apply`], [`limit`] | memory | Editor operation: revert overrides. Same RPC the desktop editor uses; call save_plugin to persist in-memory edits. |
| `compact_to_esl` | `plugin` | memory | Editor operation: compact to esl. Same RPC the desktop editor uses; call save_plugin to persist in-memory edits. |
| `clean_unused_masters` | `plugin`, `apply` | memory | Editor operation: clean unused masters. Same RPC the desktop editor uses; call save_plugin to persist in-memory edits. |
| `check_for_errors` | `plugin`, [`limit`] | no | Editor query: check for errors. Same RPC the desktop editor uses. |
| `check_quest` | `plugin`, `form_key`, [`limit`] | no | Editor query: check one quest. Returns the `check_for_errors` rows restricted to the quest record, its dialogue branches, topics and INFO records, and its scene records. Skips the load-order passes (leveled-list cycles, injected records, cross-plugin dialogue cycles), so it runs in time proportional to the quest. Same RPC the desktop editor uses. |
| `find_unreachable` | `plugin`, [`limit`] | no | Editor query: find unreachable. Same RPC the desktop editor uses. |
| `loot_cleaning_report` | `plugin` | no | Editor query: loot cleaning report. Same RPC the desktop editor uses. |
| `update_onam` | `plugin` | memory | Editor operation: update onam. Same RPC the desktop editor uses; call save_plugin to persist in-memory edits. |
| `copy_as_disabled_override` | `source_plugin`, `form_key`, `patch_plugin`, `overwrite` | memory | Editor operation: copy as disabled override. Same RPC the desktop editor uses; call save_plugin to persist in-memory edits. |
| `copy_as_wrapper` | `source_plugin`, `form_key`, `patch_plugin`, `editor_id` | memory | Editor operation: copy as wrapper. Same RPC the desktop editor uses; call save_plugin to persist in-memory edits. |
| `check_esl_eligibility` | `plugin` | no | Editor query: check esl eligibility. Same RPC the desktop editor uses. |
| `clean_plugin` | `plugin` | memory | Editor operation: clean plugin. Same RPC the desktop editor uses; call save_plugin to persist in-memory edits. |
| `renumber_form_id` | `plugin`, `form_key`, `new_id` | memory | Editor operation: renumber form id. Same RPC the desktop editor uses; call save_plugin to persist in-memory edits. |
| `add_element` | `plugin`, `form_key`, `path`, [`template`] | memory | Adds one element to a record: an authoring-path subrecord, or the 4-character subrecord signature named by `template`; a nullable FormLink list gets a null element. Refusals name why the type cannot take the new element (`ElementNotAuthorable`, `ElementPathUnsupported`, `already present`). Call `save_plugin` to persist. |
| `remove_element` | `plugin`, `form_key`, `path` | memory | Editor operation: remove element. Same RPC the desktop editor uses; call save_plugin to persist in-memory edits. |
| `move_element` | `plugin`, `form_key`, `path`, `delta` | memory | Editor operation: move element. Same RPC the desktop editor uses; call save_plugin to persist in-memory edits. |
| `clear_element` | `plugin`, `form_key`, `path` | memory | Editor operation: clear element. Same RPC the desktop editor uses; call save_plugin to persist in-memory edits. |
| `copy_element_to_selected` | `source_plugin`, `source_form_key`, `element_path`, `targets_json` | memory | Editor operation: copy element to selected. Same RPC the desktop editor uses; call save_plugin to persist in-memory edits. |
| `remove_element_from_selected` | `element_path`, `targets_json` | memory | Editor operation: remove element from selected. Same RPC the desktop editor uses; call save_plugin to persist in-memory edits. |
| `set_conditions_at` | `plugin`, `form_key`, `path`, `conditions_json` | memory | Editor operation: set conditions at. Same RPC the desktop editor uses; call save_plugin to persist in-memory edits. |
| `create_dialogue_branch` | `plugin`, `quest_form_key`, `spec` | memory | Editor operation: create a dialogue branch (DLBR) under a quest. spec keys: editorId, category, flags. Returns the new form key; call save_plugin to persist in-memory edits. |
| `create_dialogue_topic` | `plugin`, `quest_form_key`, `spec` | memory | Editor operation: create a dialogue topic (DIAL) under a quest. spec keys: editorId, name, priority, branch, keyword, topicFlags, category, subtype, subtypeName. Returns the new form key; call save_plugin to persist in-memory edits. |
| `set_dialogue_branch_start` | `plugin`, `branch_form_key`, `topic_form_key` | memory | Editor operation: set the topic a dialogue branch starts with; call save_plugin to persist in-memory edits. |
| `create_info` | `plugin`, `topic_form_key`, `spec` | memory | Editor operation: append a response (INFO) to a dialogue topic. spec keys: flags, resetHours (hours from 0 to 24, stored as round(hours * 2730.625)), responses (text, scriptNotes, emotion, sound, number, interruptPercent, cameraTargetAlias, cameraLocationAlias), conditions, setStageOnBegin and setStageOnEnd (quest stage 0 to 65534 the parent quest is set to when this INFO begins or ends; omit for none). Returns the new form key; call save_plugin to persist in-memory edits. |
| `create_dialogue_topic_bulk` | `plugin`, `quest_form_key`, `spec` | memory | Editor operation: create a dialogue topic (DIAL) and one INFO per input line. spec keys: the create_dialogue_topic keys plus `lines` (non-empty array of strings) and `maxChars` (default 160, 1-4000). A longer line is split at the last sentence end, else the last space, else hard; each part becomes a numbered response of that line's INFO. The limit is an authoring convention, not an engine limit. Refused on localized plugins because response text needs a string table. Returns the topic form key and each INFO form key; call save_plugin to persist in-memory edits. |
| `set_info` | `plugin`, `info_form_key`, `spec` | memory | Editor operation: update an INFO in place. spec keys as create_info, all optional: an absent or null key leaves that part unchanged, responses and conditions replace their run, and [] clears it. setStageOnBegin and setStageOnEnd are the exception: null clears that stage, absent leaves it. Call save_plugin to persist in-memory edits. |
| `remove_info` | `plugin`, `info_form_key` | memory | Editor operation: remove an INFO from its topic and relink the chain; call save_plugin to persist in-memory edits. |
| `create_scene` | `plugin`, `quest_form_key`, `spec` | memory | Editor operation: create a scene (SCEN) under a quest. spec keys: editorId, flags, index. Returns the new form key; call save_plugin to persist in-memory edits. |
| `set_scene` | `plugin`, `scene_form_key`, `spec` | memory | Editor operation: set scene level values. spec keys: flags, parentQuest, templateScene, index. Call save_plugin to persist in-memory edits. |
| `set_scene_phase` | `plugin`, `scene_form_key`, `phase_index`, `spec` | memory | Editor operation: set one scene phase. spec keys: name, editorWidth, flags, questStageOnStart, questStageOnCompletion, startConditions, completionConditions. Call save_plugin to persist in-memory edits. |
| `add_scene_phase` | `plugin`, `scene_form_key`, `index`, [`spec`] | memory | Editor operation: insert a scene phase at index. spec key: name. Later phase references are renumbered. Call save_plugin to persist in-memory edits. |
| `remove_scene_phase` | `plugin`, `scene_form_key`, `phase_index` | memory | Editor operation: remove a scene phase and renumber the phase references that follow it. Call save_plugin to persist in-memory edits. |
| `move_scene_phase` | `plugin`, `scene_form_key`, `from_index`, `to_index` | memory | Editor operation: move a scene phase and rewrite every phase reference that must follow it. Call save_plugin to persist in-memory edits. |
| `set_scene_actor` | `plugin`, `scene_form_key`, `actor_index`, `spec` | memory | Editor operation: set one scene actor. spec keys: alias, flags, behaviorFlags. Call save_plugin to persist in-memory edits. |
| `add_scene_actor` | `plugin`, `scene_form_key`, `index` | memory | Editor operation: insert a scene actor at index. Call save_plugin to persist in-memory edits. |
| `remove_scene_actor` | `plugin`, `scene_form_key`, `actor_index` | memory | Editor operation: remove a scene actor. Refused while any action still references its alias; clear actorAlias on those actions first. Call save_plugin to persist in-memory edits. |
| `set_scene_action` | `plugin`, `scene_form_key`, `action_index`, `spec` | memory | Editor operation: set one scene action. spec keys: name, actorAlias, flags, startPhase, endPhase, topic, dialogueSubtype, animArchType, audioOutputOverride, startSceneTopic, playSound, player and NPC response topics and subtypes, loopingMax, loopingMin, cameraFovOnPlayerCamera, cameraRateOfChange, emotionType, emotionValue, timerMax, timerMin, dialogueTargetActor. Missing members are inserted at their stored position; the action type is fixed at creation. Call save_plugin to persist in-memory edits. |
| `add_scene_action` | `plugin`, `scene_form_key`, `index`, `type` | memory | Editor operation: insert a scene action at index. type is 0 Dialogue, 1 Package, 2 Timer, 3 Player Dialogue, 4 Start Scene, 5 NPC Response Dialogue, 6 Radio. Later action indexes are renumbered. Call save_plugin to persist in-memory edits. |
| `remove_scene_action` | `plugin`, `scene_form_key`, `action_index` | memory | Editor operation: remove a scene action and renumber the actions that follow it. Call save_plugin to persist in-memory edits. |
| `move_scene_action` | `plugin`, `scene_form_key`, `from_index`, `to_index` | memory | Editor operation: move a scene action and renumber every action index that must follow it. Call save_plugin to persist in-memory edits. |
| `set_info_fragment` | `plugin`, `info_form_key`, `index`, `script_name`, `function_name` | memory | Bind or replace one INFO script fragment in memory. index is 0 OnBegin or 1 OnEnd; the VMAD and its fragment section are created when absent. Call save_plugin to persist |
| `add_info_fragment` | `plugin`, `info_form_key`, `index`, `script_name`, `function_name` | memory | Add one INFO script fragment at index in memory; refuses when that index is already bound and when the record has no fragment section yet (use set_info_fragment to create it). index is 0 OnBegin or 1 OnEnd. Call save_plugin to persist |
| `remove_info_fragment` | `plugin`, `info_form_key`, `index` | memory | Remove the INFO script fragment at index in memory and clear its flag bit. Call save_plugin to persist |
| `set_scene_phase_fragment` | `plugin`, `scene_form_key`, `phase_index`, `phase_flag`, `script_name`, `function_name` | memory | Bind or replace a scene phase script fragment in memory. phase_flag is 1 for start and 2 for completion. Call save_plugin to persist |
| `add_scene_phase_fragment` | `plugin`, `scene_form_key`, `phase_index`, `phase_flag`, `script_name`, `function_name` | memory | Add a scene phase script fragment in memory at phase_index and phase_flag; refuses when that pair is already bound and when the scene has no fragment section yet (use set_scene_phase_fragment to create it). phase_flag is 1 for start and 2 for completion. Call save_plugin to persist |
| `remove_scene_phase_fragment` | `plugin`, `scene_form_key`, `phase_index`, `phase_flag` | memory | Remove a scene phase script fragment in memory by phase_index and phase_flag (1 start, 2 completion). Call save_plugin to persist |
| `resolve_form_key_labels` | `form_keys_csv` | no | Editor query: resolve form key labels. Same RPC the desktop editor uses. |
| `copy_as_new_record` | `source_plugin`, `form_key`, `target_plugin`, `new_editor_id` | memory | Editor operation: copy as new record. Same RPC the desktop editor uses; call save_plugin to persist in-memory edits. |
| `remove_identical_to_master` | `plugin`, [`apply`], [`limit`] | memory | Editor operation: remove identical to master. Same RPC the desktop editor uses; call save_plugin to persist in-memory edits. |
| `create_merged_patch` | `plugins`, `patch_plugin`, [`apply`] | memory | Editor operation: create merged patch. Same RPC the desktop editor uses; call save_plugin to persist in-memory edits. |
| `renumber_plugin_form_ids` | `plugin`, `start`, [`apply`] | memory | Editor operation: renumber plugin form ids. Same RPC the desktop editor uses; call save_plugin to persist in-memory edits. |
| `inject_forms_into_master` | `plugin`, `master`, `start`, [`apply`] | memory | Editor operation: inject forms into master. Same RPC the desktop editor uses; call save_plugin to persist in-memory edits. |
| `create_seq_file` | `plugin`, `output_dir` | files | Editor operation: create seq file. Same RPC the desktop editor uses; writes the seq file to disk at once, not through save_plugin. |
| `check_circular_leveled_lists` | [`plugin`, `limit`] | no | Editor query: check circular leveled lists. Same RPC the desktop editor uses. |
| `deep_copy_as_override` | `source_plugin`, `form_key`, `patch_plugin`, [`apply`], [`overwrite`], `max_records` (optional), `allow_losing` (optional) | memory | Editor operation: deep copy as override. Same RPC the desktop editor uses; call save_plugin to persist in-memory edits. |
| `change_referencing_records` | `from`, `to`, `patch_plugin`, [`apply`] | memory | Editor operation: change referencing records. Same RPC the desktop editor uses; call save_plugin to persist in-memory edits. |
| `resolve_conflict` | `form_key`, `winner`, `patch`, `allow_losing` (optional) | memory | Editor operation: resolve conflict. Same RPC the desktop editor uses; call save_plugin to persist in-memory edits. |
| `scan_conflicts` | none | no | Editor query: scan conflicts. Same RPC the desktop editor uses. |
| `scan_broken_refs` | none | no | Editor query: scan broken refs. Same RPC the desktop editor uses. |
| `fix` | `nif_path`, `out_nif` | files | Editor operation: fix. Same RPC the desktop editor uses; writes out_nif to disk at once, not through save_plugin. |
| `reorder` | `plugin`, `order_json` | memory | Editor operation: reorder. Same RPC the desktop editor uses; call save_plugin to persist in-memory edits. |
| `set_light` | `plugin`, `light` | memory | Editor operation: set light. Same RPC the desktop editor uses; call save_plugin to persist in-memory edits. |
| `clean_unused` | `plugin`, `apply` | memory | Editor operation: clean unused. Same RPC the desktop editor uses; call save_plugin to persist in-memory edits. |
| `set_placed_reference_transform` | `form_key`, `patch_plugin`, `x`, `y`, `z`, `rx`, `ry`, `rz` | memory | Editor operation: set placed reference transform. Placed-reference or cell operation. |
| `edit_placed_reference` | `form_key`, `patch_plugin`, `changes` | memory | Editor operation: edit a placed reference in a patch plugin. `changes` may hold `transform`, `scale` (number, or null to clear), `editorId` (string, or null or empty to clear) and `enableParent` (`{parent, flags}`, or null to clear). Recorded for undo and redo. |
| `undo_cell_edit` | `patch_plugin` | memory | Editor operation: undo the last placed-reference edit in a patch plugin. |
| `redo_cell_edit` | `patch_plugin` | memory | Editor operation: redo the last undone placed-reference edit in a patch plugin. |
| `get_cell_edit_history` | `patch_plugin` | no | Editor query: undo and redo step counts for a patch plugin's placed-reference edits. |
| `duplicate_placed_reference` | `form_key`, `patch_plugin`, [`offset`] | memory | Editor operation: duplicate a placed reference into a patch plugin. `offset` adds `x`, `y`, `z`, `rx`, `ry`, `rz` to the copy's transform. Recorded for undo and redo. |
| `delete_placed_reference` | `form_key`, `patch_plugin` | memory | Editor operation: delete a placed reference in a patch plugin. Returns status `deleted` or `markedDeleted`. Recorded for undo and redo. |
| `set_cell_properties` | `form_key`, `patch_plugin`, `changes` | memory | Editor operation: edit cell properties in a patch plugin. `changes` keys are the names from `get_cell_properties`; a null link clears it. Recorded for undo and redo. |
| `cleanup_cell` | `cell_id`, `patch_plugin`, `mode`, [`max_count`], [`by_model`] | memory | Editor operation: remove the placed references that `plan_cell_cleanup` lists from a patch plugin. Returns the entries and `removed`. Recorded for undo and redo. |
| `load_script` | `path` | no | Editor operation: load script. Same RPC the desktop editor uses. |
| `script_analyze` | `text`, [`path`] | no | Editor query: analyze. Papyrus source operation. |
| `script_symbol_at` | `text`, [`path`], [`offset`], [`imports`] | no | Editor operation: symbol at. Papyrus source operation. |
| `script_complete` | `text`, [`path`], [`offset`], [`imports`] | no | Editor query: complete. Papyrus source operation. |
| `script_signature_help` | `text`, [`path`], [`offset`], [`imports`] | no | Editor query: signature help. Papyrus source operation. |
| `script_check_text` | `text`, [`path`], [`revision`], [`imports`] | no | Editor query: check text. Papyrus source operation. |
| `script_references` | `text`, [`path`], [`offset`], [`imports`] | no | Editor query: references. Papyrus source operation; locals, parameters and script names only. |
| `script_rename` | `text`, [`path`], [`offset`], `new_name`, [`imports`] | no | Editor query: rename, dry run. Papyrus source operation; returns the edit list and writes no files, the caller applies the edits. |
| `script_workspace_symbols` | `query`, [`limit`], [`imports`] | no | Editor query: workspace symbols. Papyrus source operation. |
| `script_read_script` | `path` | no | Editor query: read script. Papyrus source operation. |
| `script_write_script` | `path`, `text` | files | Editor operation: write script. Papyrus source operation. |
| `strings_list_tables` | `plugin`, [`language`] | no | Editor query: list string tables. String-table operation. |
| `strings_get` | `plugin`, [`language`], `table`, [`filter`], [`offset`], [`limit`] | no | Editor query: get strings. String-table operation. |
| `strings_lookup` | `plugin`, [`language`], `form_key` | no | Editor query: lookup string. String-table operation. |
| `strings_set` | `plugin`, [`language`], `table`, `form_key`, `text` | memory/files | Editor operation: set string. String-table operation; persist with strings_save or drop with strings_discard. |
| `strings_add` | `plugin`, [`language`], `table`, `text` | memory/files | Editor operation: add string. The new ID is one past the maximum across every language's string tables, the staged tables, and every localized ID the plugin's records reference, so it never reuses an ID another table or record holds. String-table operation; persist with strings_save or drop with strings_discard. |
| `strings_remove` | `plugin`, [`language`], `table`, `form_key` | memory/files | Editor operation: remove string. String-table operation; persist with strings_save or drop with strings_discard. |
| `strings_export` | `plugin`, [`language`], `table` | no | Editor operation: export strings. String-table operation. |
| `strings_save` | `plugin`, [`language`] | memory/files | Editor operation: save string tables. String-table operation; persist with strings_save or drop with strings_discard. |
| `strings_discard` | `plugin`, [`language`] | memory/files | Editor operation: discard string edits. String-table operation; persist with strings_save or drop with strings_discard. |
| `archive_extract_file` | `archive_path`, `inner_path`, `out_path` | files | Editor operation: extract file. Archive operation; paths are validated by the editor backend. |
| `archive_extract_selected` | `archive_path`, `inner_paths_json`, `out_dir` | files | Editor operation: extract selected. Archive operation; paths are validated by the editor backend. |
| `archive_extract_all` | `archive_path`, `out_dir`, [`filter`], [`limit`], [`filter_mode`] | files | Editor operation: extract all. Archive operation; paths are validated by the editor backend. |
| `archive_compare` | `archive_path_a`, `archive_path_b` | no | Editor query: compare. Archive operation; paths are validated by the editor backend. |
| `graph_core_palette` | none | no | Editor query: get core palette. Blueprint behaviour-graph operation. |
| `graph_search_palette` | [`kind`], `query`, [`script_filter`], [`limit`] | no | Editor query: search palette. Blueprint behaviour-graph operation. |
| `graph_node_signature` | `node_type` | no | Editor query: get node signature. Blueprint behaviour-graph operation. |
| `graph_validate` | `document_json` | no | Editor query: validate graph. Blueprint behaviour-graph operation. |
| `graph_compile_to_source` | `document_json` | no | Editor operation: compile to source. Blueprint behaviour-graph operation. |
| `graph_compile_to_pex` | `document_json`, `output_directory` | files | Editor operation: compile to pex. Blueprint behaviour-graph operation. |
| `graph_load` | `path` | no | Editor operation: load graph. Blueprint behaviour-graph operation. |
| `graph_save` | `path`, `document_json` | files | Editor operation: save graph. Blueprint behaviour-graph operation. |

### Profile and host state

| Tool | Arguments | Writes | What it does |
|---|---|---|---|
| `native_status` | none | no | Host status: `pluginBackend`, `pluginRead`, `pluginWrite`, `archiveReadBackend`, `nifTool` (niftool was found at startup) and `embeddedWindow`. `materialCodec` and `audioTools` are fixed at `true` and do not check for helper programs. |
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
| `nif_inspect`, `nif_geometry` | `path` | no | Read a NIF: summary and geometry. Uses niftool when configured, otherwise reads in-process. No niftool needed. |
| `nif_verify`, `nif_tree` | `path` | no | Verification and block tree. Uses niftool when configured, otherwise reads in-process. No niftool needed. |
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
| niftool | NIF import with box collision | `NIFTOOL_PATH` | `NiftoolPath` | `tools\niftool\niftool.exe` beside the exe |
| Creation Kit Wiki mirror (a folder) | `papyrus_function_lookup`, `papyrus_script_info` | `CK_WIKI_PATH` | `CkWikiPath` | `tools\ckwiki\fallout4` |
| `xWMAEncode.exe` | `audio_convert_to_xwm`, `audio_convert_from_xwm` | `XWMAENCODE_PATH` | `XwmaEncodePath` | `tools\audio\` |
| ffmpeg | `audio_convert_to_xwm` (input that is not PCM WAV), `audio_convert_from_xwm` (output other than WAV) | `FFMPEG_PATH` | `FfmpegPath` | `tools\audio\` |
| `BmlFuzEncode.exe` | `audio_make_fuz` | `BMLFUZENCODE_PATH` | none | `tools\audio\` |
| `BmlFuzDecode.exe` | `audio_extract_fuz` | `BMLFUZDECODE_PATH` | none | `tools\audio\` |
| `PapyrusCompiler.exe` | `compile_papyrus` with the Creation Kit engine | `PAPYRUS_COMPILER_PATH` | `PapyrusCompilerPath` | `<Fallout 4>\Papyrus Compiler\` |
| `Archive2.exe` | `archive_pack` | `ARCHIVE2_PATH` | `Archive2Path` | `<Fallout 4>\Tools\Archive2\` |

`<Fallout 4>` is the install folder from the `Fallout4Path` setting, `FALLOUT4_PATH`, the registry or
a Steam library. `PapyrusCompiler.exe` and `Archive2.exe` come with the Creation Kit and are not
shipped in the package. The package always includes texconv and ffmpeg. niftool is not included;
supply `NIFTOOL_PATH`. The Creation Kit Wiki mirror and the xWMAEncode and BmlFuz tools are included
only if they were available at build time; a missing one disables just the tools in its row.
niftool is looked up once at startup, so restart the server after installing it.

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
- **NIF import with box collision says `box collision needs niftool`, or a NIF command says `niftool executable not found`.** Only box-collision import
  needs niftool; fix, import, inspect, geometry, verify, tree and saving NIF edits run in-process. Fix repairs tangents, adds a missing BSX flag and trims texture and material name whitespace; it does not change shader or BSX flags, and it leaves the tangents of shapes without normal, tangent and UV attributes alone. niftool is missing from
  the package, or `NIFTOOL_PATH` points somewhere stale. Restart the server after fixing it.
- **`archive_pack` says `Archive2.exe is not configured`.** Install the Creation Kit or set
  `ARCHIVE2_PATH`.
- **An audio tool answers `Error: set ..._PATH or place ... in tools/audio`.** A helper program is
  missing. Set the environment variable the message names, or put the program in `tools\audio`.
- **Nothing starts, with no window and no output.** Run the exe with `--health` from a terminal. On a
  clean Windows install the usual cause is a missing Microsoft Visual C++ 2015-2022 Redistributable
  (x64), which the package needs.
