# FO4IDE as an MCP server

## What this is

MCP (Model Context Protocol) is a standard way to hand an AI assistant a set of tools it can call.
FO4IDE implements it: launched with `--mcp`, `FO4RecordEditor.exe` opens no window and speaks
line-delimited JSON-RPC on stdin and stdout (protocol version 2024-11-05, server name `fo4editor`).
It advertises **359 tools**: 152 hand-written tools (150 `if (!add(` registrations in `McpTools.cpp` plus `material_set_fields` and `archive_pack`), 3 preview tools (`McpPreviewTools.cpp`: `render_cell_preview`, `render_model_preview`, `get_cell_layout_summary`) and 204 catalog
tools. The 204 catalog tools are the editor parity set: 47 read-side (`McpCatalogRead.cpp`, including `previs_scan` and `previs_resolve_oc_template`), 102 edit-side
(`McpCatalogEdit.cpp`), 44 service-side (`McpCatalogService.cpp`: Papyrus source, string tables, archives,
blueprint graphs and `load_script`), 5 asset (`McpCatalogAssets.cpp`) and 6 layout (`McpCatalogLayout.cpp`).

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
written to its new-plugin folder.

### Attaching to a running editor

With `--attach`, the MCP server does not load plugins itself. It forwards each tool call to the desktop editor that is already open, so an AI edit lands in the cell or plugin the user is looking at.

| Argument | What it does |
|---|---|
| `--attach` or `--attach=auto` | Find the running editor through its endpoint file. |
| `--attach=<port>` | Use the editor on this loopback port. Needs `--token`. |
| `--token <value>` | The editor's access token: 32 lowercase hex characters. Optional with `auto`, where the token comes from the endpoint file. |

The editor writes `gui-endpoint.json` into its config folder (`%APPDATA%\FO4RecordEditor\`) while it runs, with its port, token and process id. A stale file, for a process that has exited, is ignored.

- Edits made through an attached server share the editor's undo stack, so the user can press Ctrl+Z on them in the editor.
- After a successful edit, the open cell reloads and shows the new objects. The editor reloads the cell once the current edit finishes.
- If no editor is running, or the attach fails, the server prints `in-process, GUI attach unavailable` to stderr and serves from its own copy, the same as plain `--mcp`.

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
| `create_plugin` | `plugin` | disk | Writes a new empty plugin into the new-plugin folder and loads it. The folder is `<MO2 mods>/FO4IDE New Plugins` by default, or the `NewPluginFolder` setting when one is set; the setting is read when the MO2 profile loads (see the new-plugins bullet below). `plugin` is a file name, not a path. `.esp` is added when it has no plugin extension, and an `.esm` or `.esl` name sets the master or light flag. The reply gives the full path and, for a folder in MO2's mods folder, the MO2 steps (press F5, enable the mod, place the plugin; MO2 behaviour, not verified here). Refuses official and Creation Club names, names already loaded, files that already exist (never overwritten), folders inside the game Data folder and invalid file names. |
| `create_record` | `plugin`, `type`, `payload` | memory | Creates a record from a four-letter signature and returns its `formKey`. The payload fields are listed under "create_record payloads". |
| `create_placed_object` | `plugin`, `cell`, `baseObject`, `x`, `y`, `z` [`rotX`, `rotY`, `rotZ`, `persistent`, `initially_disabled`] | memory | Adds a placed reference (REFR) over `baseObject` to a CELL and returns its `formKey`. `cell` and `baseObject` are FormKeys. `cell` must be a CELL this plugin owns (copy the cell as an override first if it is a master's); `baseObject` must be in this plugin or one of its masters. `rotX`, `rotY` and `rotZ` are radians and default to 0. `persistent` defaults to `true` (the cell's persistent list); `false` puts it in the temporary list. `initially_disabled` defaults to `false`. In an exterior cell `persistent` must be `false` and `x` and `y` must lie inside the cell's grid. The cell's child group is created when missing. |
| `create_interior_cell` | `plugin`, `editor_id` | memory | Creates an interior CELL with the given editor ID and returns its `formKey`. Refused when `editor_id` is empty or matches a CELL in this plugin, ignoring ASCII case. |
| `get_record` | `form_key` | no | Compact record details: `FormKey`, the raw form id, `Signature`, `EditorId`, winning plugin (`Winner`), `BaseForm` and `OverrideCount`. Per-plugin versions are in `get_record_plugin_matrix`. `RecordNotFound` when no loaded plugin has it. |
| `get_conflict_matrix` | `form_key` [`conflict_categories`] | no | Full conflict matrix for a record: every loaded version and its per-field `Rows` with `Winner`, `Plugins`, an overall `Level` of `onlyone`, `override` or `conflict`, and the record's `Category`. `conflict_categories` is an optional comma-separated filter of `unknown`, `only_one`, `no_conflict`, `benign`, `override`, `conflict`, `critical`; a record whose category is not listed returns no `Rows`. Large payloads are possible on big load orders; prefer `get_record` for a compact view. |
| `get_record_fields` | `plugin`, `form_key` | no | Field paths and values of one plugin's version of the record (`RecordVersionNotFound` if that plugin has none). |
| `list_record_types` | `plugin` | no | The record signatures in a plugin with counts, as strings such as `EYES (3)`. |
| `list_records` | `type` [`filter`, `limit`, `offset`] | no | Winning records of one signature across the loaded plugins. `filter` is a case-insensitive substring of the form key or EditorID. Rows: `FormKey`, `EditorID`, `Type`, `Plugin` (the winner). `limit` defaults to 200 and is capped at 5000. |
| `list_records_summary` | `plugin`, `type` [`limit`, `offset`] | no | The records of one signature that a plugin contains (added and overridden) as a grid: `columns`, `rows` (`formKey`, `editorId`, `cells`, `editable`), `total` and `offset`. The column paths are what `set_field` takes as `path`. `limit` defaults to 200 and is capped at 1000. |
| `list_plugin_worklist` | `plugin` [`filter`, `type`, `limit`, `offset`] | no | Every record the plugin adds or overrides, with override depth, winner and later overriding plugins. `filter` is `all` (default), `added`, `overrides`, `winning`, `losing` or `conflicted`; `type` is one signature. Returns `Total`, `Offset`, `Counts` and `Rows`. `limit` defaults to 200 and is capped at 5000. |
| `search_records` | `query` [`type`] [`form_id`] [`editor_id`] [`editor_id_pattern`] [`display_name`] [`field_value`] [`conflict_categories`] [`limit`] [`language`] [`regex`] | no | Case-insensitive substring search over FormKeys, EditorIDs, signatures and display names across all loaded plugins; arguments are positional in the listed order. `query` and `display_name` both match the record's FULL name: inline text directly, and a localized name (a string ID) through the plugin's selected-language `.STRINGS` table, so a localized name with no readable table does not match. `type` is one four-character signature (any other non-empty length returns no rows). `form_id` is six hex digits (local ID in any plugin), eight digits (a runtime form ID: full plugins are numbered in load order skipping light plugins, and `FE` plus a 3-digit light slot plus a 3-digit local ID, such as `FE000801`, selects a light plugin; an index or slot with no plugin matches nothing) or a FormKey such as `000800:Base.esm`. `editor_id_pattern` is a whole-EditorID glob (`*` any run, `?` one character, case-insensitive, at most 128 characters; no regex; use `regex` for the main query instead). `field_value` matches decoded scalar fields or linked FormKeys. `conflict_categories` is a comma-separated list of `unknown`, `only_one`, `no_conflict`, `benign`, `override`, `conflict`, `critical`. `limit` lowers the 500-row cap; `language` selects localized names and defaults to `en`. Set `regex` to true to read `query` as a case-insensitive ECMAScript regular expression instead of a substring (at most 256 characters); an invalid pattern returns `InvalidRegex`. |
| `set_field` | `plugin`, `form_key`, `path`, `value` | memory | Sets one field using a column path from `list_records_summary`, such as `EYES\Full`. `<SIG>\Full` (for example `WEAP\Full`) is the record's name. The grid lists it as a column on every record type that has a name, empty when the record has none yet, and writing it behaves like `set_name`. In a localized plugin the grid shows a stored name as the read-only placeholder `<opaque/localized>`; `set_field` and `set_name` still replace it. `value` is always text. Paths that are not editable columns are refused (for example with `FieldWriteUnsupported`), and a value that does not fit leaves nothing changed. A form-link field checks that the target is loaded and of an allowed record type (`NULL` only where the field allows it), and adds the target's plugin to the edited plugin's masters when needed. |
| `save_plugin` | `plugin` | disk | Writes the plugin atomically to the path it was loaded from. No other path can be chosen over MCP. Refused for read-only plugins and for files changed on disk since loading. |
| `discard_plugin` | `plugin` | memory | Drops the plugin's pending edits by re-reading the plugin from disk. Refuses with `PluginChangedOnDisk` when the file changed since it was loaded, and with `PluginReadOnly` for a protected or game-Data plugin, leaving the edits pending. Writes nothing. |
| `backup_plugin` | `plugin` | disk | Copies the plugin's on-disk file to `<plugin>.<YYYYMMDD_HHMMSS>.bak` beside it, for example `MyMod.esp.20250623_142031.bak`. Call it before the first edit of an existing plugin. An existing backup is never overwritten: a second one in the same second gets `_1`, `_2` and so on. The copy holds the saved bytes even when edits are pending, and those edits stay in memory. Refuses `PluginNotLoaded`, `PluginProtected`, `PluginReadOnly` (a game-Data plugin, so no `.bak` lands in the game Data folder) and `PluginFileMissing`. |
| `list_masters` | `plugin` | no | The plugin's declared masters (`index`, `name`, `size`, `used`, `kind` of `light` or `full`) and whether the plugin itself is light. Returns `{}` if the plugin is not loaded. |
| `add_master` | `plugin`, `master_file` | memory | Declares one master on a plugin, fresh or existing, ahead of time. The record writers that take a FormKey (magic effects, PERK links and effects, conditions, list entries and element copies) declare a missing master for it themselves, in load order, so calling this first is optional. An owner that loads after the edited plugin is still refused with `MasterOrder`. `master_file` is a plugin file name, not a path. Masters are load-order significant: a master that loads before an already-declared master is refused with `MasterOrder`, and so is one that does not load before the plugin. Re-declaring an existing master is a no-op (`No new masters were needed`). `SelfMaster`, `MasterNotLoaded` for a full or light-unknown master that is not loaded, and `PluginReadOnly` for protected or game-Data plugins. A refusal leaves the plugin unchanged. Call `save_plugin` to persist. |
| `add_masters` | `plugin`, `master_file` | memory | Same as `add_master` for several masters in one call: `master_file` is a comma-separated list and the declaration order is the order given. |
| `validate_plugin` | `plugin` [`contract`, `severity`] | no | Checks a loaded plugin and returns findings instead of raising on the first problem. See "Validation" below for the codes, the contract shape and the severity filter. |

#### create_record payloads

`type` is the signature and `payload` is an object. These twenty-two types have a verified layout. A payload
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
| `QUST`, `SPEL`, `MGEF`, `FLST`, `MESG`, `ALCH` | `editorId` (non-empty; `editor_id` accepted) | `ALCH` and `SPEL` also take `effects`; `FLST` takes `entries`. Builds the schema's required subrecords from their registered defaults: `QUST` takes a zeroed `DNAM` plus an ANAM Next Alias ID of zero; its optional `name` (an inline title, refused in a localized plugin) becomes FULL, and optional `flags` (u16), `priority` and `type` (u8) fill DNAM. `MESG` an empty `DESC` plus `INAM` and `DNAM`, `MGEF` a zeroed `DATA`, `SPEL` `OBND`, `DESC` and `SPIT`, `ALCH` `OBND`, `DATA` and `ENIT`, and `FLST` carries only its `EDID`. Nothing else is set at creation. |
| `KYWD` | `editorId` (non-empty) | `type`, `color`. `type` is a schema enum name such as `Sound` or a number 0-18 and is written to `TNAM`; `color` is `"RRGGBB"`, `"RRGGBBAA"`, or `[r, g, b, a]` with bytes 0-255 and is written to `CNAM` (alpha defaults to 255). Both are optional, so a bare keyword carries only its `EDID`. `FO4RecordEditor` has no flags subrecord for a keyword, so `flags` is refused. |
| `SPEL`, `ALCH` (`effects`) | - | `effects` is a list of `{effect, magnitude, area, duration}`. `effect` is an `MGEF` FormKey or a loaded EditorID and must resolve in the loaded order; `magnitude` is a number and `area` and `duration` are integers, all optional and defaulting to 0. The entries are appended in list order, and a refused entry names its position (`MagicEffectEntryRefused`, `MagicEffectListUnsupported`). |
| `FLST` (`entries`) | - | `entries` is a list of FormKeys or loaded EditorIDs, inserted in the order given. An entry that does not resolve in the loaded order refuses the whole call with `FormListEntryUnresolved` naming the offending index (`Entry #n`), and nothing is created. |
| `GLOB` | `editorId` (non-empty), `type` (`short`, `long`, `float` or `boolean`) | none. The type is the required `FNAM` byte the schema has no default for; `FLTV` is created as a zero `f32`. |
| `PROJ`, `EXPL`, `HAZD` | none | `editorId`, `name`, `bounds` (six integers), and the type's own numbers and form-key links, for example `speed`, `range` and `explosionType` on `PROJ`; `force`, `damage` and `innerRadius` on `EXPL`; `radius`, `lifetime` and `spell` on `HAZD` |
| `PERK` | `editorId` (non-empty; `editor_id` accepted) | `full` and `desc` (inline text in a non-localized plugin; in a localized plugin they become new string IDs in the staged string tables, so call `strings_save` to persist them; `language` picks the table language, default `en`), `trait` (0 or 1, default 0), `level` (0-255, default 0), `ranks` (0-255, default 1), `playable` (0 or 1, default 1), `conditions` (the `set_conditions` array; the root condition list), `nextPerk` (a PERK FormKey), `sound` (an SNDR FormKey), `swf` (text, the FNAM), `effects` (an array in the `set_perk_effects` shape; `quest` must be a loaded QUST, `ability` a loaded SPEL, and the typed parameter form and actor value a loaded record of the type the effect expects) |
| `PACK` | `editorId` (non-empty; `editor_id` accepted), `location` (an object: `type` is `reference`, `cell`, `near_package_start`, `near_editor_location` or `near_self`; `form` is a FormKey and is required for `reference` and `cell` and refused for the others) | `location.radius` (signed 32-bit, default 0), `speed` (`walk`, `jog`, `run` or `fast_walk`, default `jog`), `preferPreferredPath`, `useSoftRadius` and `ignoreMarkerOrientation` (booleans, default false). Builds a Travel procedure with a Location input and three Bool inputs. |

Any other signature is refused (`RecordCreationRequiresSchema`) unless the payload contains
`"schema": true`. The record is then built from the schema's defaults, with an optional `editorId`, and
only for record types the schema layer can create on its own (`RecordNotAuthorable`, `RecordNoDefault`).

### Validation

`validate_plugin` reads a loaded plugin and answers with every problem it can name in one call, instead of
failing on the first one. It returns `plugin`, `severityFilter`, `errors`, `warnings`, `infos`, `clean` and a
`findings` array. Each finding has a stable `code`, a `severity` of `error`, `warning` or `info`, the record's
`formKey`, `editorId` and `recordType`, the `field` (subrecord signature or path) it is about, and a `message`.
A plugin-scoped finding names `TES4` and carries no `formKey`. `severity` filters the report to that level and
above; the default `info` returns everything. Errors are what makes `clean` false.

The field readings behind these codes are the ones already recorded for the reader and writer in the
existing plugin notes; source:Radicle/docs/code-notes/Radicle.md.

| Code | Severity | Meaning |
|---|---|---|
| `DUPLICATE_FORM_ID` | error | Two records share a FormID; only one can win. |
| `FORM_ID_MASTER_OUT_OF_RANGE` | error | A FormID's index byte names a plugin past the declared masters. |
| `HEDR_RECORD_COUNT_MISMATCH` | warning | HEDR `numRecords`, as a save would store it, differs from the number of records and groups the plugin holds. `repair_hedr_count` rewrites it, and so does a save that adds or removes a record. |
| `HEDR_NEXT_OBJECT_ID` | error | HEDR `nextObjectId` is not above the highest own local FormID, so a new record would collide. |
| `MASTER_MISSING` | error | A declared master is not in the active profile. |
| `MASTER_ORDER` | error | A declared master loads after the plugin that declares it. |
| `REQUIRED_SUBRECORD_MISSING` | error | A record type lacks a subrecord it requires (for example SPEL without `SPIT`, QUST without `DNAM`). |
| `EFFECT_LIST_EMPTY` | warning | SPEL, ALCH, ENCH or INGR has no magic effects, so using it does nothing. |
| `EFFECT_LIST_UNREADABLE` | warning | The effect list could not be decoded, so the check could not finish. |
| `QUEST_SCRIPT_NOT_START_ENABLED` | info | A QUST carries a VMAD script but Start Game Enabled (`DNAM` bit 0) is clear, so the quest does not start on game load. `SetStage` or `Start()` can still start it. |
| `QUEST_START_GAME_UNKNOWN` | warning | QUST `DNAM` is missing or too short to read the flag. |
| `SCRIPT_MISSING` | warning | A VMAD script has no compiled `.pex` in the scripts folders searched. The script may be packed in a BA2 archive, which is not searched. |
| `SCRIPT_UNREADABLE` | error | The `.pex` exists but could not be decoded. |
| `SCRIPT_OBJECT_MISSING` | error | The `.pex` does not define the script object the VMAD names. |
| `SCRIPT_PROPERTY_MISSING` | error | A VMAD property is not declared by the `.pex` or by any parent script it extends. |
| `SCRIPT_PARENT_MISSING` | warning | A VMAD property is not declared by the script, and a non-native parent script has no readable `.pex`, so the property may be inherited. Native Papyrus parents such as `Quest` end the chain and give `SCRIPT_PROPERTY_MISSING`. |
| `SCRIPT_PROPERTY_TYPE_MISMATCH` | error | The VMAD property and the `.pex` disagree about the type. |
| `SCRIPT_PROPERTY_UNBOUND` | warning | An object property binds neither a form nor an alias, or an object array has an element that is `None`. The script sees `None`, which is valid. |
| `VMAD_UNREADABLE` | warning | The VMAD could not be decoded past some point. |
| `CONTRACT_RECORD_MISSING` | error | A contract names a record the plugin does not define. |
| `CONTRACT_RECORD_TYPE_MISMATCH` | error | A contract names a record of a different type. |
| `CONTRACT_EDITOR_ID_MISMATCH` | error | A contract names a different editor ID. |
| `CONTRACT_GLOB_TYPE_MISMATCH` | error | A GLOB's `FNAM` type differs from the contract. |
| `CONTRACT_VMAD_BINDING_MISSING` | error | A contract's script, property or property binding is absent. |
| `CONTRACT_REQUIRED_FIELD_UNSET` | error | A contract-required subrecord is missing or empty. |
| `CONTRACT_FIELD_UNSUPPORTED` | error | A contract's `requiredFields` entry is not a 4-character subrecord signature, such as a `set_field` path. It was not checked. |
| `MASTER_CHECK_SKIPPED` | info | No profile was supplied, so master presence and order were not checked. |
| `SCRIPT_CHECK_SKIPPED` | info | No script directory was resolvable, so scripts were not compared with `.pex`. |
| `MASTER_ORDER_UNCHECKED` | info | The plugin itself is not in the active profile, so its position was not compared. |
| `PLUGIN_INPUT_MISSING` | error | No plugin was supplied to the validator. |

Scripts are searched in the game's `Data\Scripts`, then in a `Scripts` folder beside the plugin. When neither
folder exists the script checks report `SCRIPT_CHECK_SKIPPED`. A script found in neither is a warning, not an
error, because it may be packed in an archive that is not searched.

`contract` is a JSON file path or an inline object. It pins a project's own expectations so the same call
checks them every time. Every list is optional.

```json
{
  "records": [{"formKey": "00080D:RadSurvival.esp", "type": "SPEL"}],
  "globals": [{"editorId": "RadSurvival_Risk", "type": "Float"}],
  "vmad": [{
    "record": "000800:RadSurvival.esp",
    "script": "RadSurvivalQuest",
    "properties": [{"name": "RadSurvival_Hunger", "formKey": "000801:RadSurvival.esp"}]
  }],
  "requiredFields": [{"editorId": "RadSurvivalQuest", "field": "DNAM"}]
}
```

A record may be named by `formKey`, by `editorId` or by both, which must then agree. A `formKey` is the
record's own FormKey as every other tool writes it: the six-digit local ID, a colon and the owning plugin, with
no load-order byte in front. A `vmad` entry names its record by `record`, a FormKey, and a property's `formKey`
is the form the property must bind. `globals` types are
`short`, `long`, `float` and `boolean`. `requiredFields` takes a 4-character subrecord signature such as `DESC`
and requires it to be present and non-empty. A `set_field` path such as `EYES\Full` is refused with
`CONTRACT_FIELD_UNSUPPORTED` rather than checked.

#### The same check without an editor

The command line runs the identical validation for a single file, with no window, no server and no MO2
profile:

```
FO4RecordEditor.exe --check <plugin.esp> [--contract <json>] [--json] [--strict] [--scripts <dir>]
```

Exit status is **0** when clean, **1** when there are errors, and **2** when the check could not run at all
(missing file, unreadable plugin, a `--scripts` folder that does not exist, or a missing or malformed contract). `--strict` also fails on warnings. `--json`
prints the same report as the tool. Without a profile, master presence and load order are reported as skipped
rather than guessed, which makes the command safe in a build script for one plugin at a time.

### Typed records

These read the winning version of a leveled list, NPC_, FACT or WEAP record (PERK reads `plugin`'s version) and edit one plugin's version in memory. An edit needs `plugin` to already hold the record; call `save_plugin` to persist. `op` is `add`, `set` or `remove`. A refused edit leaves the plugin unchanged.

| Tool | Arguments | Writes | What it does |
|---|---|---|---|
| `get_leveled_list` | `form_key` [`plugin`] | no | An LVLI, LVLN or LVSP record's chance none, max count, flags, use global and entries, from the winning version or from `plugin`'s version when `plugin` is given. `edit_leveled_list_entry` indexes the named plugin's version. |
| `set_leveled_list_settings` | `plugin`, `form_key`, `chance_none`, `max_count`, `flags` | memory | Sets the chance none, max count and flags bytes (0-255). |
| `edit_leveled_list_entry` | `plugin`, `form_key`, `op`, `index`, [`value`] | memory | Adds (inserted at `index`), sets or removes one entry. `value` is `{level, item, count, chanceNone, extra}` and is required for `add` and `set`; `set` keeps the entry's reserved bytes. |
| `get_form_list_entries` | `plugin`, `form_key` | no | An FLST record's entries in list order, from that plugin's version: `formKey`, `count` and `entries` (FormKeys). Refused with `FormListRecordMismatch` when the record is not an FLST. |
| `add_form_list_entry` | `plugin`, `form_key`, `entry` [`index`] | memory | Appends one entry, or inserts it at `index`. `entry` is a FormKey or a loaded EditorID and must resolve in the loaded order (`FormListEntryUnresolved`); an index past the end is `FormListEntryIndexOutOfRange`. The target's plugin is added to the edited plugin's masters when needed. |
| `set_form_list_entry` | `plugin`, `form_key`, `index`, `entry` | memory | Replaces the entry at `index`, which must already exist (`FormListEntryIndexOutOfRange`). |
| `remove_form_list_entry` | `plugin`, `form_key`, `index` | memory | Removes the entry at `index`, which must already exist (`FormListEntryIndexOutOfRange`). |
| `get_npc` | `form_key` [`plugin`] | no | An NPC_ record's factions, perks and inventory, from the winning version or from `plugin`'s version when `plugin` is given. `edit_npc_entries` indexes the named plugin's version. |
| `edit_npc_entries` | `plugin`, `form_key`, `list`, `op`, [`index`], [`value`] | memory | Adds, sets or removes one entry; `list` is `factions`, `perks` or `inventory`. `index` is required for `set` and `remove`, `value` for `add` and `set`. |
| `get_faction` | `form_key` [`plugin`] | no | A FACT record's relations and ranks, from the winning version or from `plugin`'s version when `plugin` is given. `edit_faction_entries` indexes the named plugin's version. A localized rank title reads as `{"stringId": n}`. |
| `edit_faction_entries` | `plugin`, `form_key`, `list`, `op`, [`index`], [`value`] | memory | Adds, sets or removes one relation or rank; `list` is `relations` or `ranks`. |
| `add_sound_file` | `plugin`, `form_key`, `file` | memory | Adds one ANAM file name to an SNDR sound descriptor, after the last file and before the descriptors. The result names the new index. Call `save_plugin` to persist. |
| `add_package_procedure` | `plugin`, `form_key`, `parent_index`, `procedure` | memory | Adds one leaf procedure under a decoded PACK procedure-tree branch and recomputes the branch counts. A procedure with a verified data-input count gets that many data inputs (existing inputs are reused in block order, missing ones are created as Bool) and PKC2 indexes; an unknown procedure, a templated package that would need inputs created, and an unextendable input block are refused. The result names the new node index. Call `save_plugin` to persist. |
| `remove_package_procedure` | `plugin`, `form_key`, `node_index` | memory | Removes one leaf procedure node, its PKC2 indexes and PFO2/PFOR override, and every data input only that leaf referenced, then reindexes the remaining input ids and PKC2 references. A container, the root, an unattributable leaf payload, duplicate input ids and a data input still referenced elsewhere are refused. Call `save_plugin` to persist. |
| `remove_sound_file` | `plugin`, `form_key`, `file_index` | memory | Removes one SNDR file name by its ANAM index. Call `save_plugin` to persist. |
| `set_sound_base` | `plugin`, `form_key` [`target`] | memory | Sets or removes the BNAM base-descriptor link of an SNDR. Only an AutoWeapon descriptor carries that arm; an empty or omitted `target` removes the link. Call `save_plugin` to persist. |
| `create_sound` | `plugin`, `signature`, `editor_id` | memory | Creates a SOUN sound marker or an FSTS footstep set from proved defaults; any other signature is refused. Call `save_plugin` to persist. |
| `add_script` | `plugin`, `form_key`, `script` [`flags`, `index`] | memory | Adds one script to the record's VMAD, creating an empty VMAD when the record has none. `script` is the Papyrus script name, `flags` the optional script flags byte and `index` the optional script-list position; the result names the new index. Call `save_plugin` to persist. |
| `remove_script` | `plugin`, `form_key`, `script_index` | memory | Removes one script and its properties by script index. Call `save_plugin` to persist. |
| `remove_vmad_fragment` | `plugin`, `form_key`, `fragment_key` | memory | Removes one existing VMAD fragment binding from a PACK, PERK, TERM or SCEN record. `fragment_key` names the binding: for PACK 0 is OnBegin, 1 OnEnd and 2 OnChange; for SCEN 0 is OnBegin and 1 OnEnd (scene phase fragments use `remove_scene_phase_fragment`); for PERK and TERM it is the fragment index stored in the entry, not its list position. Attached scripts, the other bindings and the rest of the record are kept. INFO and QUST records are refused with `UnsupportedFragmentRecord` (use `remove_info_fragment` or `remove_quest_fragment`), a key with no binding with `FragmentMissing`, and a record type that stores no fragments with `FragmentNotAuthorable`. Call `save_plugin` to persist. |
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
| `get_magic_effects` | `plugin`, `form_key` | no | A SPEL, ALCH, ENCH or INGR record's effect list: count plus each effect's index, `effect` FormKey, `magnitude`, `area` and `duration`. |
| `add_magic_effect` | `plugin`, `form_key`, `effect`, `magnitude`, `area`, `duration` | memory | Appends one effect (EFID/EFIT) to a SPEL, ALCH, ENCH or INGR record and returns its index. `effect` must be an MGEF FormKey, `magnitude` a number, `area` and `duration` integers. Call `save_plugin` to persist. |
| `set_furniture_markers` | `plugin`, `form_key`, `markers` (optional) | memory | Replaces the whole marker list of a FURN record with `markers` (at most 22 objects with `enabled`, `offsetX`, `offsetY`, `offsetZ`, `rotationZ`, `entryTypes`; omitted means one default marker). `enabled` is stored in the record's interaction bits. Call `save_plugin` to persist. |
| `set_magic_effect` | `plugin`, `form_key`, `index`, `effect`, `magnitude`, `area`, `duration` | memory | Replaces one effect of the list by index; the record must be SPEL, ALCH, ENCH or INGR and `effect` an MGEF FormKey. Call `save_plugin` to persist. |
| `remove_magic_effect` | `plugin`, `form_key`, `index` | memory | Removes one effect of a SPEL, ALCH, ENCH or INGR record by index; the whole block is dropped when the last effect goes. Call `save_plugin` to persist. |
| `set_message_buttons` | `plugin`, `form_key`, `buttons` (optional) | memory | Replaces the whole button list of a MESG record with `buttons` (strings or `{text}` objects in zero-based `Show()` index order, each non-empty without NUL, at most 65535) and sets the MessageBox bit in the flags, keeping the other flag bits. The old buttons and any conditions on them are dropped; omitted `buttons` clears the list. Text is stored inline as Windows-1252 in a non-localized plugin, or as new strings in the staged `en` STRINGS table in a localized one. Call `save_plugin` to persist. |

PERK root conditions are written with `set_conditions` and read with `get_conditions`. `set_field` with `PERK\Sound` or `PERK\SWF` still edits only an existing SNAM or FNAM; use `set_perk_sound` and `set_perk_swf` to insert one.

### Layout tools

Room layout over the plugin backend (`McpCatalogLayout.cpp`, target `layout`). Coordinates are world units; rotations are radians. `Writes` follows the rule of the editor parity tools.

| Tool | Arguments | Writes | What it does |
|---|---|---|---|
| `get_model_bounds` | [`model_paths`], [`base_form_keys`] | no | Local bounds (`min`, `max`, `size`) of model meshes with per-shape transforms applied. Give exactly one of the two lists; bounds are cached per model. |
| `describe_kit` | [`name_prefix`], [`plugin`], [`model_dir`] | no | Describes a modular kit: pieces with category, bounds and the detected grid. Give at least one filter; `plugin` is the owning plugin file name. |
| `snap_transform` | `base_form_key`, `near`, [`grid`], [`neighbour_keys`] | no | Suggests a transform for a base object near `near` (`{x, y, z}`), snapped to `grid` and butted against neighbour references. Returns the transform and the neighbours used or skipped. |
| `build_room` | `patch_plugin`, `cell_id`, `kit`, `size`, `origin`, [`grid`], [`dry_run`] | memory | Builds a room in a cell of a patch plugin from `kit` (`floor` and `wall` required; `corner`, `ceiling`, `door` optional; each `{baseFormKey, min, max}`), `size` (`cells_x`, `cells_y`, `height_cells`) and `origin` (`{x, y, z}`, snapped to `grid` in x and y). `dry_run` returns the placements and writes nothing. At most 200 placements per call. Recorded for undo and redo. |
| `check_overlaps` | `cell_id`, [`tolerance`] | no | Lists pairs of placed references whose bounds overlap by more than `tolerance` (default 1). Markers and references whose model cannot be read are listed as skipped. |
| `align_references` | `patch_plugin`, `keys`, `mode`, [`grid`] | memory | Aligns placed references (FormKeys in `keys`, at most 200) in a patch plugin. `mode` is `floor` (lowest base to the same height), `grid` (min corners to `grid`, which must be > 0) or `distribute_x` / `distribute_y` (even spacing; at least three keys; end references stay put). Recorded for undo and redo. |

### Editor parity tools

These map onto the same native RPC operations the desktop editor uses, so the MCP surface now matches the visual one. `Writes` follows the rest of this document: `memory` edits must be persisted with `save_plugin` (or the operation's own save call), `files` tools write the file they name.

| Tool | Arguments | Writes | What it does |
|---|---|---|---|
| `get_conflicts` | [`conflict_categories`] | no | Editor query: get conflicts. Each row carries its record `Category`. `conflict_categories` is an optional comma-separated filter of `unknown`, `only_one`, `no_conflict`, `benign`, `override`, `conflict`, `critical`; for example, list everything except `no_conflict` to hide unchanged overrides. Omit it for all rows. |
| `get_referenced_by` | `form_key` | no | Editor query: get referenced by. Same RPC the desktop editor uses. |
| `resolve_editorid` | `id` | no | Editor query: resolve an EditorID (exact, ignoring case) or a FormKey to its FormKey across the loaded order, with the winning plugin and record type. A duplicate EditorID resolves to the last-loaded plugin; an unknown EditorID is EditorIdNotFound and a FormKey no loaded plugin holds is RecordNotFound. |
| `get_scripts` | `id` | no | Editor query: read back the Papyrus scripts (VMAD) on a record and their property values, from the winning version. id is a FormKey or an EditorID. |
| `get_problems` | `form_key` | no | Editor query: get problems. Same RPC the desktop editor uses. |
| `compare_plugin_to` | `plugin`, `other_path`, `form_key` (optional) | no | Editor query: compare plugin to. Same RPC the desktop editor uses. |
| `describe_element` | `plugin`, `form_key`, `path` | no | Editor query: describe element. Same RPC the desktop editor uses. |
| `get_conditions` | `plugin`, `form_key` | no | Editor query: get conditions. Same RPC the desktop editor uses. |
| `get_conditions_at` | `plugin`, `form_key`, `path` | no | Read scoped conditions. Scene paths include `Phases[0].StartConditions`, `Phases[0].CompletionConditions`, and `Actions[0].StartScenes[0].Conditions`. |
| `get_quest_objectives` | `plugin`, `form_key` [`context`] | no | Editor query: get quest objectives with their alias targets and flags. `context` is an optional object `{language, stringsDirectory}` (`language` one of `en`, `de`, `it`, `es`, `esmx`, `fr`, `pl`, `ptbr`, `cn`, `ru`, `ja`; `stringsDirectory` optional) that resolves localized display text; an invalid one is refused with `InvalidLocalizedQuestContext`. Each objective also returns `flagNames` (named FNAM bits set: `ORedWithPrevious`, `NoStatsTracking`) and `unknownFlagBits` (remaining raw bits, unnamed). Same RPC the desktop editor uses. |
| `get_quest_stages` | `plugin`, `form_key` [`context`] | no | Editor query: get quest stages with log entries, log text and notes. `context` is the optional localization object described under `get_quest_objectives` and resolves localized log text. Each stage also returns raw `flags`, `startUp`, `flagNames` (named INDX bits set: `RunOnStart`, `RunOnStop`, `KeepInstanceDataFromHereOn`) and `unknownFlagBits` (remaining raw bits, unnamed). Conditions are not returned. Same RPC the desktop editor uses. |
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
| `get_reference_details` | `form_key` | no | Editor query: get reference details. Placed-reference or cell operation. |
| `get_placed_references` | `cell_id` | no | Editor query: get placed references. Placed-reference or cell operation. |
| `get_placed_references_at_grid` | `worldspace`, `grid_x`, `grid_y` | no | Editor query: get placed references at grid. Placed-reference or cell operation. |
| `list_base_objects` | [`query`] | no | Editor query: list placeable base objects (winners only) with type counts. `query` is an object: `query` text, `type`, `activePlugin`, `changedBy`, `scopes`, `match`, `sortBy`, `sortDir`, `offset` and `limit`; `limit` is capped at 200. |
| `get_navmesh_info` | `cell_id` | no | Read-only navmesh summary for a cell: each NAVM record with its parent, vertex, triangle, edge-link and door counts. Reads the cell's persistent and temporary children. |
| `get_navmesh_vertices` | `navmesh`, [`offset`, `limit`] | no | Read-only navmesh vertices (x, y, z) for one NAVM, paged. `navmesh` is a FormKey from `get_navmesh_info`. `limit` defaults to 200 and is capped at 2000. |
| `get_navmesh_triangles` | `navmesh`, [`offset`, `limit`] | no | Read-only navmesh triangles: vertex indices, edge values, extra-edge flags, clearance, traversal and the raw flags word, paged. An `edges` value is the neighbouring triangle's index, or -1 for none; where `extraEdge` is true it is an index into `edgeLinks` from `get_navmesh_links` instead. `limit` defaults to 200 and is capped at 2000. |
| `get_navmesh_links` | `navmesh` | no | Read-only links for one NAVM: `edgeLinks` (cross-navmesh edge links) and `doors` (each door's triangle index and door FormKey). |
| `resolve_worldspace` | `cell_id` | no | Editor query: resolve the worldspace that owns a cell. Placed-reference or cell operation. |
| `resolve_worldspace_id` | `worldspace` | no | Editor query: resolve a worldspace EditorID or FormKey to its FormKey. Placed-reference or cell operation. |
| `get_cell_properties` | `cell_id` | no | Editor query: get cell properties (lighting, water, owner, regions and so on) with the descriptors that `set_cell_properties` accepts. Placed-reference or cell operation. |
| `get_cell_land` | `form_key` | no | Editor query: read an exterior cell's LAND terrain heightmap. Returns `width`, `height`, `offset` and 1089 row-major world heights from the winning cell version. Refused with `CellNotFound`, `CellLandNotACell`, `CellDeleted`, `CellLandMissing` or `CellLandMalformed`. |
| `get_world_properties` | `form_key` | no | Editor query: read a WRLD's own properties. Returns the values named by the descriptors (parent, climate, water, lighting template, default land and water heights, bounds, flags, LOD water) plus the editable descriptors. Refused with `WorldNotFound`, `WorldDeleted` or `WorldNotAWorld`. |
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
| `set_name` | `plugin`, `form_key`, `text` | memory | Editor operation: set a record's display name (its FULL subrecord) on every record type that has a name: items (WEAP, ARMO, MISC, ALCH, BOOK, KEYM, AMMO, NOTE, CMPO, INGR, OMOD), actors and world objects (NPC_, RACE, FACT, CONT, ACTI, FURN, DOOR, LIGH, STAT, MSTT, TERM, FLOR, TREE), magic and perks (SPEL, ENCH, MGEF, PERK, AVIF), quests and places (QUST, DIAL, MESG, LCTN, CELL, WRLD) and the other types whose `list_records_summary` grid shows a `<SIG>\Full` column. It also works on an override copied with `copy_as_override`; when the master is localized and the patch is not, the string ID the copy carried over is replaced by the text. On an NPC_, WEAP, ARMO or FURN with object templates it writes the record's own name and leaves the template combination names alone. `text` is non-empty without NUL. Stored inline as Windows-1252 in a non-localized plugin; in a localized plugin it becomes a new string ID in the staged `en` STRINGS table. `save_plugin` persists the plugin and its string tables through the recoverable journal; `discard_plugin` drops a pending edit. Refused with a specific error for a record type without a name (`NameFieldUnsupported`), a record that cannot take a new name (`NameFieldNotInsertable`: a placed reference that is not already a named map marker, or an LCTN that holds unlisted location rows), or an existing FULL that is not a readable string (`NameFieldOpaque`). |
| `copy_as_override` | `source_plugin`, `form_key`, `patch_plugin`, `overwrite`, `allow_losing` (optional) | memory | Editor operation: copy as override. Same RPC the desktop editor uses; call save_plugin to persist in-memory edits. |
| `copy_as_override_many` | `items_json`, `patch_plugin`, `overwrite`, `allow_losing` (optional) | memory | Editor operation: copy as override many. Same RPC the desktop editor uses; call save_plugin to persist in-memory edits. |
| `revert_overrides` | `bad_plugin`, `patch_plugin`, [`signature`], [`contains_component`], [`apply`], [`limit`] | memory | Editor operation: revert overrides. Same RPC the desktop editor uses; call save_plugin to persist in-memory edits. |
| `set_localized_flag` | `plugin`, [`localized`] | memory | Editor operation: set or clear the plugin header Localized flag (default true). Only the flag changes; no text is moved into Strings files. Refused (`InlineTextPresent`) when a translated field still stores inline text, and on clear (`StringIdPresent`) when one stores a string ID. Reports a warning when no Strings folder sits next to the plugin. Call save_plugin to persist. |
| `strip_masters_clean` | `source_path`, `masters`, [`output_path`], [`dry_run`] | files | Editor operation: read a plugin file, drop every COBJ condition whose parameter or reference points at one of the named masters, remove those masters from the header and write the result to `output_path` (required when `dry_run` is false; a .esp/.esm/.esl inside a directory hosting a loaded plugin; an existing file there is overwritten unless it is a loaded plugin, which is refused). `masters` is a JSON array of file names. A named master that records still use stays and is reported; unnamed masters keep their order. Defaults to a dry run. No loaded plugin is touched. |
| `batch_patch_records` | `patch_plugin`, `source_plugin`, `type`, [`operations`], [`filter_field`], [`filter_value`], [`dry_run`], [`limit`] | memory | Editor operation: apply the same set/add/remove field operations to every record of one four-letter type in `source_plugin`, as overrides in `patch_plugin` (created when missing). Optional filter, `limit` default 5000. Defaults to a dry run that lists the matches. All-or-nothing: one failure stages nothing. ARMO/ARMA, EYES, LVSP and EDID fields are refused (use set_field). Call save_plugin to persist. |
| `compact_to_esl` | `plugin` | memory | Editor operation: compact to esl. Same RPC the desktop editor uses; call save_plugin to persist in-memory edits. |
| `clean_unused_masters` | `plugin`, `apply` | memory | Editor operation: clean unused masters. Same RPC the desktop editor uses; call save_plugin to persist in-memory edits. |
| `check_for_errors` | `plugin`, [`limit`] | no | Editor query: check for errors. Same RPC the desktop editor uses. |
| `check_quest` | `plugin`, `form_key`, [`limit`] | no | Editor query: check one quest. Returns the `check_for_errors` rows restricted to the quest record, its dialogue branches, topics and INFO records, and its scene records. Skips the load-order passes (leveled-list cycles, injected records, cross-plugin dialogue cycles), so it runs in time proportional to the quest. Same RPC the desktop editor uses. |
| `find_unreachable` | `plugin`, [`limit`] | no | Editor query: find unreachable. Same RPC the desktop editor uses. |
| `loot_cleaning_report` | `plugin` | no | Editor query: loot cleaning report. Same RPC the desktop editor uses. |
| `update_onam` | `plugin` | memory | Editor operation: update onam. Same RPC the desktop editor uses; call save_plugin to persist in-memory edits. |
| `repair_hedr_count` | `plugin` | memory | Recalculates the HEDR record count. Call save_plugin to persist the change. |
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
| `remove_identical_to_master` | `plugin`, [`apply`], [`limit`] | memory | Editor operation: remove identical to master. Only records with every subrecord identical to the master are removed. Same RPC the desktop editor uses; call save_plugin to persist in-memory edits. |
| `find_identical_to_previous_override` | `plugin`, [`limit`] | memory | Editor query: find identical to previous override. Lists the overrides that match the previous override in the load order, where the nearest earlier plugin holding the record must be one of the plugin's masters, otherwise the record is not reported; read-only. |
| `remove_identical_to_previous_override` | `plugin`, [`apply`] | memory | Editor operation: remove identical to previous override. Dry run unless apply is true; removes the overrides identical to the previous override in the load order, only when that previous override is held by a master of the plugin and every subrecord is the same. Call save_plugin to persist in-memory edits. |
| `create_merged_patch` | `plugins`, `patch_plugin`, [`apply`] | memory | Editor operation: create merged patch. Same RPC the desktop editor uses; call save_plugin to persist in-memory edits. |
| `renumber_plugin_form_ids` | `plugin`, `start`, [`apply`] | memory | Editor operation: renumber plugin form ids. Same RPC the desktop editor uses; call save_plugin to persist in-memory edits. |
| `inject_forms_into_master` | `plugin`, `master`, `start`, [`apply`] | memory | Editor operation: inject forms into master. Same RPC the desktop editor uses; call save_plugin to persist in-memory edits. |
| `create_seq_file` | `plugin`, `output_dir` | files | Editor operation: create seq file. Same RPC the desktop editor uses; writes the seq file to disk at once, not through save_plugin. |
| `check_circular_leveled_lists` | [`plugin`, `limit`] | no | Editor query: check circular leveled lists. Same RPC the desktop editor uses. |
| `deep_copy_as_override` | `source_plugin`, `form_key`, `patch_plugin`, [`apply`], [`overwrite`], `max_records` (optional), `allow_losing` (optional) | memory | Editor operation: deep copy as override. Same RPC the desktop editor uses; call save_plugin to persist in-memory edits. |
| `change_referencing_records` | `from`, `to`, `patch_plugin`, [`apply`] | memory | Editor operation: change referencing records. Same RPC the desktop editor uses; call save_plugin to persist in-memory edits. |
| `resolve_conflict` | `form_key`, `winner`, `patch`, `allow_losing` (optional), `overwrite` (optional) | memory | Editor operation: resolve conflict. Same RPC the desktop editor uses; call save_plugin to persist in-memory edits. |
| `scan_conflicts` | none | no | Editor query: scan conflicts. Same RPC the desktop editor uses. |
| `scan_broken_refs` | none | no | Editor query: scan broken refs. Same RPC the desktop editor uses. |
| `fix` | `nif_path`, `out_nif`, [`check`], [`overwrite`] | files | Editor operation: fix. Same RPC the desktop editor uses; writes out_nif to disk at once, not through save_plugin. An existing out_nif is refused unless `overwrite` is true. Omit `check` for the default repair. |
| `shape_op` | `nif_path`, `out_nif`, `op`, [`blocks_json`], [`crease_degrees`], [`overwrite`], [`ratio`], [`max_influences`], [`prune_threshold`], [`source_block`], [`tris_json`], [`distance`], [`loop_a_json`], [`loop_b_json`], [`normal_json`], [`offset`], [`edges_json`], [`width`] | files | Editor operation: shape op (`recalc_normals`, `weld_vertices`, `simplify`, `normalize_weights`, and others). Same RPC the desktop editor uses; writes out_nif to disk at once, not through save_plugin. An existing out_nif is refused unless `overwrite` is true. The five model ops `extrude_faces` (`tris_json`, `distance`), `bridge_loops` (`loop_a_json`, `loop_b_json`), `knife_plane` (`normal_json`, `offset`), `cut_loop` (`edges_json`) and `bevel_edges` (`edges_json`, `width`) take exactly one id in `blocks_json`, edit that plain `BSTriShape` in place and keep its block index, references, transform and vertex layout. |
| `reorder` | `plugin`, `order_json` | memory | Editor operation: reorder. Same RPC the desktop editor uses; call save_plugin to persist in-memory edits. |
| `set_next_object_id` | `plugin`, `next_id` | memory | Editor operation: set next object ID. Sets the HEDR next object ID of a loaded plugin; refused above 0xFFFFFF, above 0x1000 for a light plugin, and at or below the highest owned object ID (NextObjectIdTooLow). Call save_plugin to persist in-memory edits. |
| `set_light` | `plugin`, `light` | memory | Editor operation: set light. Same RPC the desktop editor uses; call save_plugin to persist in-memory edits. |
| `set_record_flags` | `plugin`, `form_key`, [`persistent`], [`visible_when_distant`], [`initially_disabled`], [`deleted`] | memory | Editor operation: set record flags. Sets the persistent, visible-when-distant and initially-disabled flags of one record; omitted flags keep their value. The deleted flag cannot change here and is refused unless it already matches. Call save_plugin to persist in-memory edits. |
| `clean_unused` | `plugin`, `apply` | memory | Editor operation: clean unused. Same RPC the desktop editor uses; call save_plugin to persist in-memory edits. |
| `set_placed_reference_transform` | `form_key`, `patch_plugin`, `x`, `y`, `z`, `rx`, `ry`, `rz` | memory | Editor operation: set placed reference transform. Placed-reference or cell operation. |
| `edit_placed_reference` | `form_key`, `patch_plugin`, `changes` | memory | Editor operation: edit a placed reference in a patch plugin. `changes` may hold `transform`, `scale` (number, or null to clear), `editorId` (string, or null or empty to clear) and `enableParent` (`{parent, flags}`, or null to clear), `flags` (uint32 record flags; only the named bits that `get_reference_details` lists in `flags.options` with `editable` true can change, and the persistent and deleted bits are refused) and `lock` (null removes the lock, or `{level, key, flags}` with `level` a named lock level 0, 1, 25, 50, 75, 100, 251-255, `key` a KEYM FormKey or null, and `flags` 0-255), `ownership` (null clears the owner, or `{owner, noCrime, rank}` with `owner` an NPC_ or FACT FormKey, `noCrime` defaulting to false, and `rank` an integer only with a FACT owner), `primitive` (`{bounds: {x, y, z}, color: [r, g, b, a]}`, half-extents and colour; the shape type is read-only), `openByDefault` and `ignoredBySandbox` (booleans), `linkedReferences` (an array of `{keyword, reference}` that replaces the whole list, with `keyword` a KYWD FormKey or null and `reference` a placed-reference FormKey), `teleport` (`{door, position, rotation}` with `door` a DOOR FormKey and `position` and `rotation` as `{x, y, z}` objects; it cannot be cleared) and `comments` (a string, or null to clear). Recorded for undo and redo. |
| `undo_cell_edit` | `patch_plugin` | memory | Editor operation: undo the last placed-reference edit in a patch plugin. |
| `redo_cell_edit` | `patch_plugin` | memory | Editor operation: redo the last undone placed-reference edit in a patch plugin. |
| `get_cell_edit_history` | `patch_plugin` | no | Editor query: undo and redo step counts for a patch plugin's placed-reference edits. |
| `place_reference` | `patch_plugin`, `cell_id`, `base_form_key`, `transform`, [`options`] | memory | Editor operation: place one reference of a base object in a cell of a patch plugin. `transform` takes `x`, `y`, `z`, `rx`, `ry` and `rz`; `options` may set `persistent`, `initiallyDisabled`, `scale` and `editorId`. The cell is copied into the patch plugin as an override when needed. Recorded for undo and redo. |
| `place_references` | `patch_plugin`, `cell_id`, `placements_json`, [`options`] | memory | Editor operation: place a batch of references in one cell as one undo step. `placements_json` is a JSON array (at most 200 items) of `{baseFormKey, transform, scale?, editorId?}`. `options` as in `place_reference`. Recorded for undo and redo. |
| `duplicate_placed_reference` | `form_key`, `patch_plugin`, [`offset`] | memory | Editor operation: duplicate a placed reference into a patch plugin. `offset` adds `x`, `y`, `z`, `rx`, `ry`, `rz` to the copy's transform. Recorded for undo and redo. |
| `delete_placed_reference` | `form_key`, `patch_plugin` | memory | Editor operation: delete a placed reference in a patch plugin. Returns status `deleted` or `markedDeleted`. Recorded for undo and redo. |
| `set_cell_properties` | `form_key`, `patch_plugin`, `changes` | memory | Editor operation: edit cell properties in a patch plugin. `changes` keys are the names from `get_cell_properties`; a null link clears it. Recorded for undo and redo. |
| `move_cell_to_grid` | `form_key`, `patch_plugin`, `grid_x`, `grid_y` | memory | Editor operation: move an exterior cell to another grid coordinate in a patch plugin, carrying the CELL record and its child records into the new block and sub-block groups and translating the `XTEL` destination of every teleport door that points at a door in the cell (copied into the patch; `teleportsUpdated` counts them). Refused when the coordinate is outside -110..110, already occupied by another cell (in the patch or any loaded plugin), or the cell is interior or the persistent exterior cell. Returns `changed`, false when the cell was already there. Recorded for undo and redo. |
| `set_cell_land` | `form_key`, `patch_plugin`, `heights_json` | memory | Editor operation: write an exterior cell's LAND terrain heights in a patch plugin. `heights_json` is a JSON array of 1089 row-major world heights. The winning LAND is copied into the patch on the first edit and its VHGT rewritten in place; the copy source is reported as `copiedFrom`. Refused with `CellNotFound`, `CellLandNotACell`, `CellDeleted`, `CellLandMissing`, `CellLandMalformed` or `CellLandInvalid`. Recorded for undo and redo. |
| `set_world_properties` | `form_key`, `patch_plugin`, `changes` | memory | Editor operation: edit a worldspace's own properties in a patch plugin. `changes` keys are the names from `get_world_properties`; a null link clears it and `BoundsMin`/`BoundsMax` take an `[x, y]` array. The winning WRLD is copied into the patch on the first edit and the copy source is reported as `copiedFrom`. Refused with `WorldNotFound`, `WorldDeleted`, `WorldNotAWorld`, `WorldTargetMissing`, `WorldTargetTypeNotAllowed`, `WorldTargetNotMaster`, `WorldPropertyInvalid` or `WorldMalformed`. Recorded for undo and redo. |
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
| `strings_localize` | `plugin`, [`language`] | memory/files | Editor operation: localize plugin. Converts the inline localized text into string IDs and stages the string tables; persist the tables with strings_save and the plugin with save. Refused while a localized quest edit is pending. |
| `strings_delocalize` | `plugin`, [`language`] | memory/files | Editor operation: delocalize plugin. Writes each string ID back as inline text from the staged or on-disk string tables and clears the localized flag; the plugin is saved with save. Refused while a localized quest edit is pending. |
| `archive_extract_file` | `archive_path`, `inner_path`, `out_path`, [`overwrite`] | files | Editor operation: extract file. Archive operation; paths are validated by the editor backend. An existing destination is refused unless `overwrite` is true. |
| `archive_extract_selected` | `archive_path`, `inner_paths_json`, `out_dir`, [`overwrite`] | files | Editor operation: extract selected. Archive operation; paths are validated by the editor backend. An existing destination is refused unless `overwrite` is true. |
| `archive_extract_all` | `archive_path`, `out_dir`, [`filter`], [`limit`], [`filter_mode`], [`overwrite`] | files | Editor operation: extract all. Archive operation; paths are validated by the editor backend. An existing destination is refused unless `overwrite` is true. |
| `archive_compare` | `archive_path_a`, `archive_path_b` | no | Editor query: compare. Archive operation; paths are validated by the editor backend. |
| `archive_list` | `archive_path`, [`filter`], [`limit`], [`filter_mode`] | no | Editor query: list the entries of a BA2 archive, optionally narrowed by filter and filter_mode, up to limit (default 2000). |
| `resolve_asset` | `path`, [`extract`], [`limit`] | cache | Editor query: find which loose file or BA2 serves a game-relative asset path across the loaded data roots, highest priority first, with every lower-priority provider. extract:true also writes the winning copy to the asset cache. |
| `audit_asset_usage` | `plugin`, [`limit`] | no | Read-only asset audit of a loaded plugin: asset paths held in its own records (by file extension) are checked against the loaded data roots, BA2 archives and the plugin folder. Returns referenced, resolved and missing counts and up to `limit` (default 200, at most 5000) missing paths with the records that name them. Only paths in modelled record fields are seen; unreferenced shipped files are not reported. |
| `graph_core_palette` | none | no | Editor query: get core palette. Blueprint behaviour-graph operation. |
| `graph_search_palette` | [`kind`], `query`, [`script_filter`], [`limit`] | no | Editor query: search palette. Blueprint behaviour-graph operation. |
| `graph_node_signature` | `node_type` | no | Editor query: get node signature. Blueprint behaviour-graph operation. |
| `graph_validate` | `document_json` | no | Editor query: validate graph. Blueprint behaviour-graph operation. |
| `graph_compile_to_source` | `document_json` | no | Editor operation: compile to source. Blueprint behaviour-graph operation. |
| `graph_compile_to_pex` | `document_json`, `output_directory` | files | Editor operation: compile to pex. Blueprint behaviour-graph operation. |
| `graph_load` | `path` | no | Editor operation: load graph. Blueprint behaviour-graph operation. |
| `graph_save` | `path`, `document_json` | files | Editor operation: save graph. Blueprint behaviour-graph operation. |

### Asset browser and placement

These read the placeable base objects of the winning records (`McpCatalogAssets.cpp`) and place them as references. `asset` takes a form key (`000800:Fallout4.esm`) or an editor ID; an editor ID that matches more than one base object fails with `AssetAmbiguous` and lists the candidates as form keys. `place_asset` takes either `position` or `near_reference_key` with `offset`, and places `count` copies stepped by `spacing` (an `{x,y,z}` object), optionally snapped to `snap_grid`. It is one undo step, at most 200 copies. `dry_run` returns the plan and writes nothing.

| Tool | Arguments | Writes | What it does |
|---|---|---|---|
| `browse_assets_categories` | [`plugin`] | no | Asset query: browse categories. Lists placeable base-object groups with counts and the plugins that define them. |
| `browse_assets_folder` | `path`, [`group`, `depth`] | no | Asset query: browse folder. Walks the model folder tree of placeable assets under `path`, with counts and sample rows per folder. |
| `search_assets` | [`text`, `group`, `folder`, `plugin`, `match`, `field`, `has_model`, `sort`, `limit`, `offset`] | no | Asset query: search assets. Filters placeable base objects by text, group, model folder, plugin and model presence; paged. |
| `get_asset_details` | [`base_form_key`, `editor_id`] | no | Asset query: get asset details. One placeable base object by form key or unique editor ID, with model path and bounds. |
| `place_asset` | `patch_plugin`, `cell_id`, `asset`, [`position`, `near_reference_key`, `offset`, `rotation`, `scale`, `count`, `spacing`, `snap_grid`, `persistent`, `dry_run`] | memory | Editor operation: place asset. Places an asset at `position` (`{x,y,z}`) or near an existing reference with `offset`. Recorded for `undo_cell_edit` and `redo_cell_edit`; in memory until `save_plugin`. |

### Profile and host state

| Tool | Arguments | Writes | What it does |
|---|---|---|---|
| `native_status` | none | no | Host status: `pluginBackend`, `pluginRead`, `pluginWrite`, `archiveReadBackend` and `embeddedWindow`. `materialCodec` and `audioTools` are fixed at `true` and do not check for helper programs. |
| `get_mo2_status` | none | no | `loaded`; for a loaded profile also `instancePath`, `profile`, `dataFolder`, `overwriteFolder`, `newPluginFolder`, `pluginCount`, `missingPluginCount` and `loadedAtUnixMs`. Always `pendingPlugins` (plugins with unsaved edits) and staleness flags: `staleConfig` (`ModOrganizer.ini` changed), `staleLoadOrder` (`plugins.txt`, `modlist.txt` or `loadorder.txt` changed) and `stale` (either). |
| `reload_mo2_profile` | [`discard_pending`] | memory | Reloads the enabled plugins from the configured profile, replacing everything that is loaded (plugins added with `open_plugin` included), and returns the `get_mo2_status` fields. Refused with `PluginHasPendingChanges` while any plugin has unsaved edits, unless `discard_pending` is `true`, which throws those edits away. Also saves the instance path to `settings.json`. |
| `release_memory` | [`drop_conflict_graph`] | memory | Pages the process working set out to the OS after a large load, save or reload. Plugin data stays loaded and pages back in on the next access, so use it when the editor is idle and memory use matters. The result reports the working set before and after the trim. `drop_conflict_graph: true` also drops the cached conflict/reference graph (the largest optional allocation); it rebuilds on the next conflict or reference query. The reference half of that graph is also released automatically after an idle period. |
| `get_settings` | none | no | The current settings: folders, tool paths and flags, including values that came from environment variables. Secrets are redacted. |

### Preview

Software-rasterised images and numeric summaries of loaded cells and NIF models. Nothing is written to disk.

| Tool | Arguments | Writes | What it does |
|---|---|---|---|
| `render_cell_preview` | `cell_id` [`camera`, `layers`, `width`, `height`, `highlight_keys`, `max_references`] | no | Renders a loaded cell's placed references to a PNG (Z-up; `front`, `right`, `top` or `iso` camera, framed on the drawn content). Returns the image plus JSON: drawn and skipped counts (`missingGeometry`, `overBudget`), markers, bounds, camera, legend and each `highlight_keys` FormKey's pixel position, `volumes` drawn and `unresolvedMarkers`. Markers draw as wire boxes; primitive, trigger and room volumes draw by primitive type (Box as a wire box, Sphere and Ellipsoid as three axis rings, Plane as a rectangle, Line as one segment; size from the primitive bounds, else the model bounds as a box, else a 64 box for triggers and a 256 box for rooms); references past `max_references` (default 4000) are skipped. `layers` is an object of booleans `room`, `trigger`, `marker` and `effect`, all on by default, that hides that layer's volumes, markers and models as the cell viewer's layer toggles do; hidden volumes and markers are also left out of `bounds` and the camera framing (triangles of any hidden layer still count toward the framing). |
| `render_model_preview` | `model_path` [`camera`, `size`] | no | Renders one NIF model's geometry to a PNG (Z-up; default `iso` camera, square `size` default 512). `model_path` is relative to Data, for example `Architecture/Foo.nif`. Returns the image plus JSON: triangles, bounds, camera and legend. |
| `get_cell_layout_summary` | `cell_id` | no | Numeric layout of a loaded cell with no image: reference counts by record type, bounds, a 16x16 occupancy grid (counts and dominant model per cell), the largest structures by bounding box with positions, and floor and ceiling height histograms. `bounds` covers model geometry and marker positions only; the default 64 and 256 unit trigger and room boxes are not included. |

### Previs

| Tool | Arguments | Writes | What it does |
|---|---|---|---|
| `previs_scan` | [`data_dir`] | no | Read-only previs report for the saved load order: the summary counts, and the broken, outdated and skipped cells, each list capped at 5,000 rows (`truncated` says so). `data_dir` defaults to the configured Data folder. The scan reads the saved files on disk, so unsaved edits are not part of it; `pendingPlugins` names the loaded plugins that have unsaved edits. A second scan while one runs is refused with `PrevisScanBusy`, and a full scan can take a minute or more. |
| `previs_resolve_oc_template` | `cell`, [`source_plugin`], [`patch_plugin`] | no | Finds the `Meshes/PreCombined` `_OC.NIF` paths for a cell local id, highest-priority Data root first. Returns `suggestedSourceNif`, and `suggestedOutputOc` when `patch_plugin` is set. |
| `stamp_cell_previs` | `patch_plugin`, `cell`, [`source_plugin`], `pcmb`, `visi`, [`xcri_json`], [`apply`] | memory | CK-free previs patch: copies the CELL override if needed, then writes PCMB, VISI and XCRI (and RVIS for an exterior cell). `xcri_json` is `{"mesh_ids":[],"refs":[[refr_form_id,oc]]}`, each REFR a file Form-ID in the patch's master list. Call `save_plugin` to persist. |
| `disable_cell_previs` | `cell`, `patch_plugin`, [`source_plugin`], [`apply`] | memory | Exterior cells only: copies the override, sets NoPreVis and clears PCMB, VISI, XCRI, RVIS and XPRI. Call `save_plugin` to persist. |
| `precombine_plan` | `cell`, `plugin`, [`min_instances`], [`lod_targets`], [`group_limit`], [`include_instances`] | no | Lists the STAT precombine groups of an interior or exterior cell, over the temporary references whose winning version is in `plugin`; `min_instances` defaults to 2. `lod_targets` pins object ratios by model path, as in `generate_cell_previs`. `group_limit` caps the groups returned (0 or absent is unlimited); `groupsShown` and `groupsOmitted` report the cut and `eligibleReferences` counts every eligible group's references. `skipped` counts the references refused for a script (`scripted`) or an enable parent (`enableParent`). `include_instances` set to false returns the counts without per-instance entries (default true). |
| `bake_oc_nif` | `source_nif`, `output_oc_nif`, [`placements_or_plan`], [`plan_group_index`], [`oc_shell`] | files | Copies `source_nif` to `output_oc_nif` when no placements are given. With a placements or plan JSON value (object, array or string) it patches the measured `_OC` Shared extras, or authors one from the STAT mesh using the `oc_shell` `_OC` template when `oc_shell` is set. |
| `generate_cell_previs` | `patch_plugin`, `cell`, [`source_plugin`], [`oc_shell`], [`min_instances`], [`apply`], [`copy_vanilla_tome`], [`lod_targets`], [`patch_into_data`] | files, memory | CK-free previs generation in one call for an interior or exterior cell: plans the STAT groups, bakes one `_OC.NIF` per group, pairs XCRI, stamps PCMB and VISI, writes the CELL override, writes the cell's `.uvd` tome from the baked geometry and merges the patch's `.cdx` row. `copy_vanilla_tome` copies a shipped tome instead, with a warning. A run that cannot finish writes and stages nothing and returns `complete: false` with `limitations` naming why; it is not an error, so check `complete`. `apply: false` reports the plan without writing. Call `save_plugin` to persist. |
| `write_previs_container` | `kind`, `source_path`, `output_path` | files | Parses a CDX, CSG or UVD file (`kind` is `cdx`, `csg` or `uvd`) and rewrites it with the native writers. |

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
| `nif_segments` | `path`, `block` | no | One BSSubIndexTriShape's armor segment table, sub-segment counts and SSF sidecar. Game acceptance of a rewritten segment table is unproven. |
| `nif_ssf_generate` | `path`, `block`, `out` [`overwrite`] | disk | Writes the `.ssf` sidecar of one BSSubIndexTriShape from its armor segment table. Bone names come from the NIF string table; unresolved and ambiguous bone ids are listed in the reply, left out of the file, and the first line reads `ssf PARTIAL` with a bone count instead of `ssf OK`. `out` must end in `.ssf` and an existing file is refused unless `overwrite` is true. Game acceptance is unproven. |
| `nif_import` | `obj_path`, `out_nif` [`material`, `tex_diffuse`, `tex_normal`, `collision`, `from_blender`, `mass_kg`, `overwrite`] | disk | Builds a NIF from an OBJ in process. `collision` is `none`, `box`, `hull` or `mesh` (a boolean works too: `true` is box). `mass_kg` gives a box or hull its mass in kilograms; 0 or omitted keeps it static. Mesh collision is static only. Authored collision has not been tested in the game until the owner confirms it. |
| `nif_collision_info` | `path` | no | Reads each collision body's node, motion, mass, layer and layer name, material CRCs, shape kind, vertex and triangle counts, constraints, unsupported shapes and lint findings. Each system's `owned` flag marks donor bodies for `nif_collision_set`. |
| `nif_collision_set` | `path`, `node`, `out_nif`, `source` [`donor_path`, `kind`, `mass_kg`, `donor_body`, `overwrite`] | disk | Adds or replaces collision on one node and writes a new NIF. `source` is `donor_nif` or `kind` (`box`, `hull`, `mesh`). `out_nif` must differ from `path`; an existing `out_nif` is refused with an `EXISTS:` error unless `overwrite` is true, and is then copied to `out_nif.bak` once before it is replaced. `donor_body` (default 0) picks the donor body by its position among the donor's `owned` entries in `nif_collision_info`. See tool schema for donor index and Save As rules. |
| `nif_collision_remove` | `path`, `node`, `out_nif` [`overwrite`] | disk | Removes collision on one node and writes a new NIF under the same Save As and backup rules as `nif_collision_set`. |
| `nif_connect_points_get` | `path` | no | Lists connect point blocks (CPA parents and CPT children) in process through Radicle. |
| `nif_connect_points_set` | `path`, `ops`, `out_nif` [`overwrite`] | disk | Applies connect point `add`, `remove` and `set` ops from JSON array text and writes `out_nif`. An existing `out_nif` is refused unless `overwrite` is true. |
| `nif_uv_get` | `path` | no | UV summary per shape: island counts and UV-area summary for UV0 and UV2. |
| `nif_uv_transform` | `path`, `block`, `out_nif` [`channel`, `du`, `dv`, `sx`, `sy`, `rotation_degrees`, `overwrite`] | disk | Transforms one shape's UV channel and writes the NIF to `out_nif`, which is required; an omitted `out_nif` is refused. An existing `out_nif` is refused unless `overwrite` is true. |
| `nif_vertex_color_get` | `path` | no | Vertex colour summary per shape: counts and per-channel min, max and mean. |
| `nif_vertex_color_set` | `path`, `block`, `kind` [`channel`, `axis`, `value`, `color`, `end_color`, `vertices`, `out_nif`, `overwrite`] | disk | Paints one shape's vertex colours (`fill`, `channel`, `gradient`, `enable`) and writes the NIF. `vertices` lists the target vertex indices; omitted means all vertices and an empty list is refused. An existing `out_nif` is refused unless `overwrite` is true. |
| `nif_create_texture_set` | `path`, `block`, `textures`, `out_nif` [`overwrite`] | disk | Gives a shape whose lighting shader has no texture set a new texture set holding the slot paths in `textures`, and writes the NIF to `out_nif`. Refused when the shader already has a texture set or is not a lighting shader. An existing `out_nif` is refused unless `overwrite` is true. |
| `nif_attach_alpha` | `path`, `block`, `out_nif` [`flags`, `threshold`, `overwrite`] | disk | Gives a BSTriShape or BSMeshLODTriShape with no alpha property a new NiAlphaProperty and writes the NIF to `out_nif`. `flags` is the alpha flags word (default 236, the engine default) and `threshold` the alpha test threshold (default 128). Refused when the shape already has an alpha property or is not a supported tri-shape. An existing `out_nif` is refused unless `overwrite` is true. |
| `nif_remove_block` | `path`, `block`, `out_nif` [`overwrite`] | disk | Removes a childless block that nothing outside a child list references and writes the NIF to `out_nif`, renumbering every reference past it. Blocks it alone owned go with it, and a header string only those blocks used is emptied in place so the string table keeps its indices. Refused when the block has children or is referenced from outside a child list. An existing `out_nif` is refused unless `overwrite` is true. |
| `nif_block_op` | `path`, `op`, `out_nif` [`block`, `child`, `donor_path`, `blocks`, `groups`, `pose_path`, `pose`, `channel`, `interpolation`, `keys`, `overwrite`] | disk | One block-structure op on a NIF, written to `out_nif`. `duplicate_block` copies a childless node or a shape (`block`) as a new last block with no parent, sharing a shape's skin, shader and texture references; attach it with `append_child`. `append_child` makes the unparented `child` the last child of the node `block` and refuses a child that has a parent or is an ancestor. `reparent_block` moves `child`, which keeps its index, to the end of the child list of the node `block` (`block` is the new parent, `child` the block that moves); `child` keeps its local transform, so its world pose changes under a parent with a non-identity transform; `child` must have exactly one parent (use `append_child` for an unparented block) and not be a root, and `block` must not lie inside `child`. `sort_children_by_name` orders the child list of the node `block` by block name and keeps its length. `merge_nif_into` appends every block of the NIF at `donor_path` and parents the donor roots under the node `block`; Havok collision in the donor is refused. `bake_shape_transforms` bakes each shape's own transform in `blocks` into its vertices (the vertices then sit in the parent's space), reports skipped shapes (skinned, half precision, mirrored) and is refused when nothing was baked. `join_shapes` merges the BSTriShape blocks in `blocks` into `blocks[0]`, which keeps its node, material, shader and skin, and removes the others; it needs byte-identical vertex layouts, bitwise-identical global transforms (bake first), the same material and textures, and all skinned or all unskinned, and refuses a `BSDismemberSkinInstance` skin. `separate_shape` splits the BSTriShape `block` into one shape per triangle list in `groups` (every triangle in exactly one group, at least two groups), appending the copies under the same plain parent so they share its skin, shader and texture blocks. `separate_by_bone` does the same for a skinned shape, one shape per dominant bone, and refuses a triangle with no positive bone weight. The reply lists the join target, the removed blocks and the counts, or the surviving and appended shape blocks. `apply_pose` writes the bone-local transforms of the pose called `pose` in the pose library file `pose_path` into the bone data of the skin instance `block` (`BSSkin::Instance` or `BSDismemberSkinInstance`), lists the pose bones the skin lacks and the skin bones the pose lacks, and is refused when no bone was posed or the pose is not in the library (the refusal names the poses it holds). `set_keys` replaces one key channel (`translation`, `scale`, `rotation_quat`, `rotation_x`, `rotation_y`, `rotation_z` on `NiTransformData` and `NiKeyframeData`, `data` on `NiPosData` and `NiFloatData`) of the key block `block` with `keys`, each `{time, value}` with `value` the numbers of one key: one per component for linear (1) and constant (5) keys, three per component for quadratic (2) keys, three more than the components for TBC (3) keys; components are 1 for scale, XYZ rotations and float data, 3 for translation and position data, 4 (w, x, y, z) for `rotation_quat`, whose quadratic keys have no tangents. `interpolation` is optional when the channel already has keys. Times must not decrease; `NiBoolData` and text keys are refused. An existing `out_nif` is refused unless `overwrite` is true. |
| `nif_keys_get` | `path`, `block` | in memory | Reads the key channels of one key data block (`NiTransformData`, `NiKeyframeData`, `NiPosData` or `NiFloatData`): for each channel that holds keys, its name, interpolation, components and keys as `{time, value}`, with the channel names and numbers per key that `nif_block_op` `set_keys` takes, so a channel can be edited and written back. `NiBoolData` and text keys are refused. |
| `nif_pose_list` | `pose_path` [`path`, `block`] | in memory | Lists the poses of a pose library file (JSON, a `poses` list of named bone-local transforms) with their bone counts. With `path` and `block` (a skin instance) every pose is also dry-run against that skin: the bones it would write, the pose bones the skin lacks and the skin bones the pose lacks, or why it would be refused. Nothing is written; `nif_block_op` `apply_pose` applies one. |
| `nif_shape_op` | `nif_path`, `out_nif`, `op` [`blocks`, `crease_degrees`, `ratio`, `max_influences`, `prune_threshold`, `source_block`, `overwrite`, `tris`, `distance`, `loop_a`, `loop_b`, `normal`, `offset`, `edges`, `width`] | disk | Mesh or skin-weight ops on one or more shapes (`recalc_normals`, `weld_vertices`, `normalize_weights`, and others); writes `out_nif`. `extrude_faces` (`tris`, `distance`), `bridge_loops` (`loop_a`, `loop_b`), `knife_plane` (`normal` as `[x, y, z]`, `offset`), `cut_loop` (`edges` as `[vertex, vertex]` pairs forming the ring) and `bevel_edges` (`edges`, `width`) need exactly one id in `blocks`, keep the shape's block index, name, references, transform and vertex layout, and are refused for a `BSDismemberSkinInstance` skin, a second UV set, other tri-shape types and any shape that would change on rewrite; a `knife_plane` that crosses no triangle is refused. |
| `nif_skin_influences` | `nif_path`, `block`, `vertex` | in memory | The bones that move one vertex, heaviest first, with the shape's bone names. |
| `nif_skin_influenced_by` | `nif_path`, `block`, `bone` [`minimum`] | in memory | The vertices one bone moves, with their count, weight sum and local bounding box; `minimum` defaults to 0.01. |
| `nif_skin_validate` | `nif_path`, `block` | in memory | BGS SkinValidator's findings for one shape: bind transforms against bones, finite and non-negative weights, a sum of one, a repeated bone, a vertex that moves nothing and a bone no vertex uses; errors are separated from warnings. |
| `nif_skin_binding` | `nif_path`, `skeleton_path` [`block`, `skeleton_nif_path`] | in memory | Which skin bones an HKX skeleton moves, as BGS bound a skinned mesh: exact, helper node chain, `_skin` parent guess or unmatched, with the fully bound vertex count and each bone's weighted vertices. Read only; a shape without a readable skin is refused. |
| `nif_skin_edit` | `nif_path`, `out_nif`, `kind`, `block` [`vertex`, `influences`, `from_bone`, `to_bone`, `keep_source`, `lane`, `from_positive_side`, `overwrite`, `expected_source`] | disk | Per-vertex influence replacement (`set_influences`), bone-to-bone weight copy (`copy_bone`) or a mirrored weight copy across one axis of one shape (`mirror_weights`); writes `out_nif`. `lane` is 0 for X, 1 for Y, 2 for Z (default 0). `expected_source` is the sha256 of the NIF as loaded; a mismatch refuses the save. The NIF at `nif_path` is not changed, and an existing `out_nif` is refused unless `overwrite` is true. |
| `hkx_skeleton` | `skeleton_path` | no | Reads a Havok skeleton HKX: the bone hierarchy with each bone's translation lock and reference pose, the reference floats, the float slots, the local frames and the named partitions. |
| `hkx_pose` | `skeleton_path`, `animation_path`, `time` [`space`] | no | Samples an animation HKX at `time` seconds. `space` is `local` (default) or `model`. |
| `hkx_summary` | `path` | no | Reads an HKX overview: object count, the first skeleton's name and bone count, every decoded clip's class, kind, duration, frame, track and annotation counts, and the skeleton names the bindings name. Every animation object that did not decode is named in `undecoded` with its class and reason (for example `bad-section`, or `more than one animation object: 2`), so an empty `clips` list is never silent; `detail` names the refused case when the reader knows it (for example a predictive clip: `float/bone boundary sampling`), else it is empty. |
| `hkx_animation_inspect` | `animation_path` [`track`, `limit`] | no | Inspects one animation HKX: clip class and kind, duration, frame, track, annotation and object counts, each track's animation flags and sample count, and every annotation track's entries. With `track`, that track's translations, rotations (x, y, z, w) and scales are included for at most `limit` frames (default 2000, clamped 1 to 100000), and `truncated` says whether the clip has more. A clip class this build does not decode reports kind `view` with no tracks. A clip the reader refuses (for example some predictive layouts) fails with the class and the refused case in the error text. |
| `hkx_gltf_save` | `template_path`, `skeleton_path`, `gltf_path`, `out_path` [`animation`, `skin`, `replace_annotations`, `replace_motion`, `overwrite`] | disk | Writes a glTF 2.0 animation (`.glb` or `.gltf`) into a copy of a template animation HKX in process. The template keeps its binding and track-to-bone map and only its clip is replaced; the template is never modified and `out_path` must be a new file unless `overwrite` is true. Refuses, with the reason and the fix, a bone the glTF animates that the template has no track for, float tracks, a clip shared with another object, a root-motion frame that is not a Z-up default frame, and a result whose read-back drifts past the error budget. The template's existing annotations and non-zero root motion are replaced only with `replace_annotations` and `replace_motion`. Returns the validation report. |
| `hkx_subgraph` | `plugin_path`, `form_id` [`data_folder`] | no | Resolves a RACE record's AnimTextData subgraph: the behaviour graphs (`SGNM`) and animation path prefixes (`SAPT`), the subgraph id those hash to, and the `AnimationFileData\<id>.txt` manifest read from `data_folder` (loose `Meshes` first, then its `.ba2` archives). Every animation path the manifest lists is counted `present` or listed in `unresolved`. A record naming several behaviour graphs reports the lists with no id, because the record does not group prefixes per graph. |
| `hkx_objects` | `path` [`class_prefix`, `known_layout_only`, `limit`] | no | Lists the typed objects of a `.hkx` packfile with their fields and resolved references. `class_prefix` keeps objects whose class name starts with it (default: every class); `known_layout_only` skips classes with no generated layout (default false); `limit` is the most objects returned (default 64). |
| `hkx_scene` | `path` | no | Reads a `.hkx` packfile's asset structure: the root container's named variants with the object and class each holds, the scene-level classes present (`hkxScene`, `hkxEnvironment`, `hkAlignSceneToNodeOptions`), the classes with no verified layout and the diagnostics. Units, up axis and environment variables are reported as not decoded. The file is never changed. |
| `hkx_root_motion` | `skeleton_path`, `animation_path`, `bone` [`limit`] | no | One bone's travelled distance, heading change and loop closure for a clip. `bone` is the bone index the clip moves; `limit` is the most samples returned (default 64). |
| `hkx_skeleton_document` | `skeleton_path` | no | Reads a skeleton HKX as a bone document with depths, children and left/right partners. Each bone also carries `lock`, `translation`, `rotation` and `scale`, and the reply has `source_hash`. |
| `hkx_behavior_graph` | `path` [`limit`] | no | Lists a `.hkx` file's `hkb` behavior objects and the links between them; `limit` is the most behavior objects returned (default 64). |
| `hkx_behavior_model` | `path` [`limit`] | no | Reads a behavior HKX through the typed reader: graph name, root node, events, variables, typed nodes with child edges and state generators. `limit` is the most rows returned from each list (default 2000). |
| `hkx_behavior_authoring` | `path` | no | Reads a behavior HKX for editing: every event, state machine, state and transition (with its event, target state, priority, flags, intervals, effect and condition) plus the structural validation of the graph. Nothing is written. |
| `hkx_behavior_validate` | `path` | no | Validates a behavior HKX structurally: dangling event, variable, state and node references, a missing start state, unreachable states, transitions and nodes, and duplicate or missing names. Each issue has a severity, a stable code, the table and the row. Nothing is written. |
| `hkx_behavior_edit` | `path`, `edits` [`out_path`, `overwrite`] | no, or disk | Applies transition, event, variable, clip, state and binding edits to a behavior HKX transactionally. `edits` run in order on a private copy; any refusal, or an edit that adds a validation error, aborts the whole call and writes nothing. Kinds: `set_transition` (`state_machine`, `state`, `index`, `transition` with every field), `add_transition` (`state_machine`, `state`, `index` to insert before, `transition` with `event_id` and `to_state_id`), `remove_transition` (`state_machine`, `state`, `index`), `add_event` (`name`, `flags`), `rename_event` (`index`, `name`), `set_event_flags` (`index`, `flags`), `add_variable` (`name`, `flags` = type 0 bool, 1 int8, 2 int16, 3 int32, 4 real; initial value 0), `rename_variable` (`index`, `name`; refused while an expression string mentions the old name), `set_clip_animation` (`index` = clips table index, `name`), `set_start_state` (`state_machine`, `value` = state id), `add_state` (`state_machine`, `name`, `value` = new state id, `node` = generator node index), `remove_state` (`state_machine`, `state` = states table index), `set_binding` (`index` = bindings table row, `value` = variable index), `add_binding` (`flags` = owner kind 1 state machine, 2 state, 3 blender, 5 clip; `index` = owner position in its table, `name` = member path, `value` = variable index), `remove_binding` (`index` = bindings table row) and `set_generator_field` (`flags` = owner kind 3 blender, 4 blend child, 5 clip; `index` = position in that owner's table; `name` = member; `real` for real members, `value` for integers): clip `playbackSpeed`, `startTime`, `cropStartAmountLocalTime`, `cropEndAmountLocalTime`, `enforcedDuration` (real), `mode` (0 to 3), `flags` (0 to 63); blender `blendParameter`, `minCyclicBlendParameter`, `maxCyclicBlendParameter` (real); blend child `weight`, `worldFromModelWeight` (real), refused for a member a binding drives; a binding edit is refused for a blend child or for an owner with no binding set. `add_blend_child` (`index` = blenders table index, `node` = generator node, `real` = weight; appended last; a non-parametric blender takes 0 to 1), `remove_blend_child` (`index` = blend_children table row), `add_selector_child` (`node` = selector node, `value` = generator node; appended last), `remove_selector_child` (`index` = edges table row of a selector child) and `remove_subtree` (`node` = an unreferenced generator node): a removal before the sync master or selected child renumbers that index, and removing the sync master or selected child, the last child, a child of a `subtractLastChild` blender, or a child of a blender or selector with null slots or non-empty runtime arrays or counters is refused with its reason (the engine behaviour of those cases is not verified). A removed blend child leaves its generator, which `remove_subtree` removes once nothing points at it; the file does not shrink. `state` is a states table index, or -1 for the machine's wildcard run. Removing events or variables is refused; a removed state is refused while a transition or the start state still names its id. Without `out_path` the edits are validated and nothing is written; an existing `out_path` is refused unless `overwrite` is true. |
| `hkx_subgraph_id` | [`id`, `behavior_path`, `path_prefixes`, `data_folder`, `mo2_instance_dir`, `mo2_profile`] | no | Names a subgraph and reads its manifest without a RACE record. No argument is schema-required, but give `id` or `behavior_path`: `id` (a decimal unsigned 64-bit number, as a crash log prints it) or a `behavior_path` such as `Actors/Character/Behaviors/WeaponBehavior.hkx` with its `path_prefixes` (required with `behavior_path`, in the order the record lists them), which are hashed to the id the game uses. With `data_folder` the `AnimationFileData` manifest for that id is read (loose `Meshes` first, then the `.ba2` archives) and each animation it lists is reported present or in `unresolved`. With `mo2_instance_dir` the manifest and animations are read through that MO2 profile's enabled mods, highest priority first, as the animation index reads them; a `data_folder` given with it replaces the instance's game Data folder, `mo2_profile` (default: the instance's selected profile) needs `mo2_instance_dir`, and an unusable instance, profile or Data folder is an error (`InvalidSources`). Archives that could not be read are listed in `notes`. |
| `hkx_project_chain` | `hkx_path` [`data_folder`, `mo2_instance_dir`, `mo2_profile`] | no | Follows a Havok project's link chain in process: the project `.hkx`, its character, the character's behaviour and skeleton, and every animation the character declares, each resolved loose or inside a `.ba2` archive and reported with its source. `hkx_path` is any Data-relative `.hkx` below `meshes` in the project's folder. Give `data_folder` or `mo2_instance_dir`; with an instance the files are read through the profile's enabled mods, highest priority first (a `data_folder` replaces the instance's game Data folder; `mo2_profile` needs `mo2_instance_dir`). A declared path that leaves `meshes` is blocked and never opened. `problems` names every missing or blocked link, including an animation borrowed from another project, and `ok` is false when there are any; `warnings` names non-fatal facts such as a character that declares no animations or a project that names several characters; `notes` names archives that could not be read. This is the chain of Havok project files, not a plugin or master chain. |
| `hkx_project_check` | `hkx_path` [`data_folder`, `mo2_instance_dir`, `mo2_profile`, `limit`, `format`] | no | Checks a Havok project's behaviour files in process: the behaviour its character names, then every `.hkx` under the project's `behaviors` folder. The sources are chosen as for `hkx_project_chain`. A file that is not a readable packfile is an error; a file with no behaviour graph is a warning; every graph runs the behaviour validator, and each finding carries `file`, `level`, `code` and `message`. Each clip animation is resolved against the project: an absent one is an error, one the character does not declare is a warning. `limit` is the most behaviour files read (default 2000, clamped to 1 to 100000); `truncated` is true when the scan stopped at `limit`, and then `complete` and `ok` are false. `chain_problems` and `chain_warnings` repeat `hkx_project_chain`, and `notes` names archives that could not be read. Each readable file is also checked for round-trip losses, read-only: a class the class table does not define or signs differently (kind `UnsupportedClass`, as BGS) and a file the Radicle writers lay out wrongly (`RewriteFailed`, `RereadFailed`, `RepackDiffers`, from `repack_check`). A file the writers refuse to lay out again (an exports or imports tail) is not a loss: it is a warning finding `repack.refused`, because the refused rate on vanilla files is not yet measured. A loss blocks a native save, is kept apart from `findings` and makes `ok` false; `round_trip_losses`, `files_with_round_trip_losses`, `unreadable` and `summary` (the BGS one-line sentence) are in the reply. `report` is the per-file report of the BGS project report: `project`, `summary`, `truncated`, `totals` (`files`, `readable`, `unreadable`, `errors`, `warnings`, `round_trip_losses`, `files_with_round_trip_losses`; `errors` and `warnings` are per-file and exclude losses), `files` (each with `path`, `name`, `unreadable`, `errors`, `warnings`, `round_trip_losses` and `findings`) and `project_findings` (findings that belong to no file). A finding in the report has `severity`, `code` and `message`: the BGS `where`, `object_id` and `blocks_save` have no Radicle counterpart. `format` `csv` or `html` also returns the BGS CSV or HTML report as text in `rendered`; nothing is written to disk. |
| `hkx_ragdoll` | `path` [`limit`] | no | Reads a standalone ragdoll HKX: bodies, shapes, `capsules` (`a`, `b`, `radius`, `editable` and the `reason` when a capsule is not editable), `materials` (`dynamic_friction`, `static_friction`, `restitution`, `shared`) with `capsule_total` and `material_total` counting every row, constraints with their limits (twist, cone, plane, hinge), angular friction and the cone and plane angle measurement modes, bone bindings, the physics and animation skeletons and the validator findings. A file with no usable ragdoll replies with `loaded` false and the classifier verdict (skeleton only, legacy physics, unsupported shapes, wrong container) instead of an error. `limit` is the most rows returned from each list (default 2000). |
| `hkx_ragdoll_save` | `path`, `out_path`, `edits` [`overwrite`, `expected_source`] | disk | Applies validated ragdoll edits to a standalone ragdoll HKX and writes the result to `out_path`. `edits` is an object of `bodies` (`position`, `rotation`, `center_of_mass`, `mass`), `constraints` (`body_a`, `body_b`, `twist_min`, `twist_max`, `cone_max`, `plane_min`, `plane_max`, `friction`, `hinge_min`, `hinge_max`, `frame_a`, `frame_b`; a cone or plane edit is refused when that atom stores another angle measurement mode than the one the edit is defined for, and `friction` is the angular friction maximum torque, finite and not negative) and `bone_map` (bone to body), `shapes` (capsule `a`, `b`, `radius`; the capsule direction must not change, and a capsule whose stored polytope or mass data does not match the verified capsule model, a shared capsule or a shape that is not a capsule is refused) and `materials` (`material` index with `dynamic_friction`, `static_friction`, `restitution`, `allow_shared`; values are stored as the upper 16 bits of a float, so they read back truncated) rows; edits are in-place writes of existing fields only, and a capsule resize re-derives its polytope, volume and mass block and rescales the owner's motion inertia. The edited file is read back and compared with the request before anything is written, so a refusal writes nothing and names its reason. `out_path` must differ from `path`; an existing `out_path` is refused unless `overwrite` is true and is then copied to `out_path.bak` once. `expected_source`, the sha256 of `path` from the last read, refuses a file changed since. |
| `hkx_ragdoll_preview` | `path` [`options`] | no | A bounded physics preview of a ragdoll HKX: sampled body poses, the final body state and the measured joint angles against their limits. Capsule bodies only, a fixed 1/120 s step, gravity along -Z and an optional ground plane. `options` is an object: `gravity`, `ground`, `ground_z`, `body_collision`, `steps` (0-2400), `frames` (1-240), `limit_overrides`, `impulses` and `pose`; they apply to the preview only and nothing is written. A ragdoll the preview cannot simulate is refused with its reason. |
| `hkx_mapper` | `path` [`recompute`, `limit`] | no | Reads the skeleton mappers of an HKX: both skeletons, mapping type, unmapped bones, the extracted motion, and every simple row and chain row with its bone names. With `recompute` true each simple row also reports the transform the two reference poses produce and its gap to the stored one (default false). `limit` is the most rows returned from each list (default 2000). |
| `hkx_mapper_edit` | `path`, `edits` [`mapper`, `out_path`, `overwrite`, `expected_source`, `limit`] | no, or disk | Edits one skeleton mapper of an HKX and, with `out_path`, writes the result. `mapper` is the position in the `hkx_mapper` list (default 0). Each edit has a `kind`: `set_row_bones`, `set_row_transform`, `set_row_pose` (translation and rotation only), `insert_row`, `insert_row_with_transform`, `remove_row`, `recompute_row`, `build_rows`, `set_chain_bones`, `set_chain_transforms`, `set_mapping_type` or `set_keep_unmapped_local`. Chain rows are edited in place only. The writer always runs in memory, so a resize it cannot place is refused with its reason. Without `out_path` nothing is written and the reply is a preview; the file at `path` is changed only when `out_path` names it and `overwrite` is true. `expected_source` is `source_sha256` from `hkx_mapper`; a mismatch refuses the edit. |
| `hkx_project_index` | `root` [`query`, `limit`] | no | Indexes the `.hkx` files under the directory `root`, searches them and groups identical behavior graphs. `query` matches file names or class names ignoring case (default: every file); `limit` is the most files scanned (default 400). |
| `hkx_animation_index` | [`data_folder`, `mo2_folder`, `profile`, `kind`, `project`, `race`, `character`, `query`, `resolve_path`, `offset`, `limit`, `refresh`] | no | Indexes every `.hkx` and `.hkt` animation, behavior, character and skeleton file in a game Data folder, an MO2 instance profile or both, across loose files and `.ba2` archives. Each path lists its winning provider and the providers it shadows. `data_folder` is optional when `mo2_folder` names the game; `profile` defaults to the instance's selected profile; `kind` is `animation`, `behaviour`, `project`, `character`, `skeleton` or `other`; `project`, `race`, `character` and `query` filter ignoring case; `offset` (default 0) and `limit` (default 200) page the result; `resolve_path` matches the skeleton and body model for one indexed path (several candidates are listed and none is chosen). A folder that is not an MO2 instance, a profile with no `modlist.txt` or a missing Data folder is an error. Vortex mod lists are not read. The index is kept per Data folder, MO2 folder and profile; `refresh` rescans. |
| `hkx_object` | `path`, `object` [`limit`] | no | Inspects one object of a `.hkx` packfile by object id (its index in the file's object list): class, signature, the fields of classes with a generated layout (inherited members included), the pointers it holds and the objects that point at it. Pointers into the object's own arrays or strings are marked inner and never counted as references. Each field has its `type`, raw `hex` capped at 16 bytes and, where the layout states a width, a decoded `value` (strings quoted, pointers named by object, arrays as a count and their first values; structs, vectors, matrices and arrays of them are expanded, enum values read as `number (NAME)` and a half float as its raw bits); a field that is not decoded (members whose width the layout tables do not state such as ulong, variant and matrix4, members the game never stores, a value that fails the checks) has an empty `value` and a `reason`, a struct with one undecoded member keeps its value with `?` for that member plus a `reason`, and a class with no layout keeps raw hex only. `partial` is true when any field is not decoded. `limit` is the most fields and references per list (default 50, at most 200). |
| `hkx_object_tree` | `path` [`depth`, `limit`] | no | Walks a `.hkx` packfile's object graph depth first from the root container (or from every unreferenced object), listing each object with its class, offset, the member it hangs from and, for a behavior graph, its node or state name and clip animation, plus a `summary` of its decoded string members for any class with a generated layout. A repeated object is listed again marked repeated and not expanded; the reply counts the objects the walk never reached. Partial: objects are addressed by packfile index and a class with no layout has an empty summary (`partial` true). `depth` is the deepest level expanded (default 32, at most 256); `limit` is the most rows (default 200, at most 5000). |
| `hkx_packfile` | `path` | no | Diagnoses a `.hkx` packfile: header version and pointer size, per section size and fixup counts, each class with its signature checked against the generated layout table, objects shorter than their layout, pointer kinds and unreferenced objects. A header the reader refuses is reported with the reason instead of an error. |
| `hkx_repack_check` | `path` | no | Round-trip check of a `.hkx` packfile: reads it, lays it out again with the writer every hkx save uses, reads the result and compares the two packfile models. The reply has `verdict` (`equal`, `read_failed`, `refused`, `rewrite_failed`, `reread_failed`, `differs`), `model_equal`, `status`, both sizes and, for `differs`, `first_difference` naming the first differing part (user tag, version, section data or fixups, object offset, signature or class). The contract is model equality, not byte identity: `bytes_identical` is informational, because the rewrite re-lays the fixup tables. `refused` is a layout the writer declines (exports or imports), not corruption; `read_failed` adds the reader's `reason`. The file is never changed. |
| `hkx_search` | `path`, `query` [`limit`] | no | Searches one `.hkx` file or every `.hkx` under a directory for `query`, ignoring case: file names, class names, events, variables, node, state and bone names, clip animation names, class field names (a field hit means the class has that member) and field values (written strings, numbers and booleans, vectors, quaternions, matrices and transforms as their floats, and enum values by name or number, shown as `Class.path = value`; byte arrays and half floats are not searched). `#N` finds object N; an empty query finds nothing. `limit` is the most hits across all files (default 200, at most 5000); once it is reached no further file is read and the reply says `truncated`. A file that does not read is counted under `skipped`. |
| `hkx_channels` | `skeleton_path`, `animations_path` [`limit`] | no | Audits animation clips against a skeleton: for each clip the tracks whose translation, rotation or scale channel is not animated while the skeleton's rest pose is not the identity. `animations_path` is one clip or a directory of clips; a clip that cannot be posed on the skeleton is listed under `skipped_files` with the reason. `limit` is the most clip rows (default 200, at most 5000). |
| `hkx_symbol_usage` | `path` [`limit`] | no | Lists where each event, variable and state of a behavior `.hkx` is used: clip triggers, state machine events, transitions and their intervals, modifier and other object event slots the class table types as events (`object_member` sites), variable bindings, and per state incoming and outgoing transition counts. Partial: a class or slot the table does not type is not decoded, so a row with `no_decoded_site` is not proof of an unused event or variable; use `hkx_behavior_validate` for dangling reference errors. `limit` is the most rows per list (default 200, at most 5000). |
| `hkx_skeleton_edit` | `skeleton_path`, `kind`, `bone` [`name`, `parent`] | no | Applies one validated bone edit and returns the resulting skeleton, without writing a file. `kind` is `rename` (`name`), `reparent` (`parent`, a bone index or -1 for a root) or `remove`; `bone` is the bone index to edit. |
| `hkx_skeleton_save` | `skeleton_path`, `edits` [`out_path`, `overwrite`, `expected_source`] | no, or disk | Applies an ordered list of bone edits (`rename`, `reparent`, `remove`, `set_pose` with `translation`, `rotation` and `scale`, `set_lock` with `locked`) with the real writer run in memory and, with `out_path`, writes the result after reading it back. Without `out_path` it is a preview and writes nothing. An existing `out_path`, the source included, is refused unless `overwrite` is true. `expected_source` is the `source_hash` the caller last saw. |
| `hkx_clip_edit` | `path`, `ops` [`out_hkx`, `expected_source`, `overwrite`, `spline`, `max_position_error`, `max_rotation_error`, `max_scale_error`] | no, or disk | Edits one animation clip HKX with an ordered list of `ops`, each an object with an `op` name: `trim` (`first`, `last`), `retime` (`scale`, `keep_frame_rate`, `max_position`, `max_rotation`, `max_scale`), `loop` (`blend_frames`), `set_key` (`track`, `frame`, `translation`, `rotation`, `scale`), `copy_pose` (`from`, `to`), `insert_frames` (`at`, `count`), `delete_frames` (`first`, `count`), `move_frames` (`first`, `count`, `to`), `add_annotation` (`track_name`, `time`, `text`), `edit_annotation` (`track`, `index`, `time`, `text`) and `remove_annotation` (`track`, `index`). Without `out_hkx` nothing is written and the reply previews the result. With `out_hkx` the clip is saved only when every op and the verified save succeed; an existing file there is refused unless `overwrite` is true. `expected_source` is the sha256 of the clip as loaded. `spline` stores the clip spline compressed (true) or uncompressed (false); the default keeps the source's storage. `max_position_error` (default 0.02), `max_rotation_error` (radians, default 0.01) and `max_scale_error` (default 0.01) bound the saved clip's error against the edit. Clips with float tracks, several clips or unsupported root motion are refused. |
| `hkx_convert` | `path` [`target_version`, `out_hkx`, `expected_source`, `overwrite`] | no, or disk | Verifies a same-version hk_2014.1.0-r1 packfile round trip; no field-changing version pair exists yet. It reports exactly which fields changed and which classes were left opaque. Only conversions with verified evidence are supported; today that is the same-version pass, which changes nothing. Any other `target_version`, an unreadable or non-packfile file, a class this build cannot decode, a reference into an opaque class or a rule gap is refused with a named reason and no bytes. Without `out_hkx` nothing is written and the reply previews the result; with it the converted file is written only when the conversion ended ok, `out_hkx` must differ from `path`, and an existing file there is refused unless `overwrite` is true. |
| `nif_import_gltf` | `gltf_path`, `out_nif` [`material`, `tex_diffuse`, `tex_normal`, `collision`, `skeleton`, `overwrite`] | disk | Builds a NIF from a glTF 2.0 file (`.glb` or `.gltf`) in process. Each glTF node becomes a named NiNode and each primitive a BSTriShape, with positions, normals, UVs, tangents and vertex colours; Y-up metres become Z-up NIF units. A skinned mesh is bound to the bones of `skeleton`, a NIF whose bones its joints name, and a joint missing from it is refused by name. A material's base colour and normal textures become texture paths (absolute paths under a `textures` folder are cut to it, and others are refused); a material named `*.bgsm`, or with `extras.bgsm`, becomes the shader's BGSM. Each texture path comes from the exporter's record (`fo4Path` on the image, then the image name, then the material's `fo4DiffuseTexture` or `fo4NormalTexture`), so embedded or renamed images keep their DDS game path. Only when none of those names a path is a texture refused, and the refusal names `tex_diffuse` or `tex_normal` to pass. Morph targets, sparse accessors, a second UV set and non-triangle primitives are refused by name. An existing NIF at `out_nif` is refused unless `overwrite` is true. Not yet opened in the game. |
| `nif_export_gltf` | `path`, `out` (required); `overwrite`, `materials`, `textures`, `tangents`, `vertex_colours` (optional) | disk | Exports a NIF to a binary glTF 2.0 (`.glb`) file in process: the node tree, shapes with positions, normals, UVs, tangents, vertex colours (`COLOR_0`), skin, and one material per shape. Each material takes its textures from the shader texture set, or from the BGSM/BGEM the shape names when that file resolves under the app's data roots. Alpha is `BLEND` or `MASK` with a cutoff from the NiAlphaProperty threshold; `doubleSided` comes from the Double_Sided shader flag or the BGSM TwoSided flag. `textures` is `png` (default: DDS textures decoded to PNG files beside the `.glb` under `textures/`, and a DDS copy with a warning when a texture cannot be decoded), `copy` (the DDS files written beside the `.glb`), `embed` (DDS stored inside the `.glb`) or `uri` (relative paths only, no files written). A texture file whose name is already taken in the export gets `_2`, `_3` and so on before its extension. `materials`, `tangents` and `vertex_colours` default to true. Positions are in metres (0.0142875 per NIF unit) and Z-up becomes Y-up. The NIF is not changed; Havok animation clips are not exported. An existing `.glb` at `out`, or a texture file it would write beside it, is refused unless `overwrite` is true. Not yet opened in Blender: the DDS and PNG output is untested there. Blender 4.2 LTS is the assumed target, and its DDS support is assumed, not checked. |
| `material_inspect` | `path` | no | Reads a BGSM or BGEM material: `fileName`, `format`, `version` and its named `fields`. |
| `material_set_fields` | `path`, `fields` [`output`] | disk | Sets named material fields (`fields` maps a name to a text value) and writes the result atomically to `output`, or back to `path` when no output is given. A conditional field such as `EmittanceColor` can be set only when its switch (`EmitEnabled`) is already true in the saved file: set the switch in one call and the field in the next. |
| `audio_convert_to_xwm` | `source` [`output`, `bitrateBps`] | disk | Converts one audio or video file, or every supported file under a folder, to Fallout 4 XWM. Without `output` each `.xwm` goes next to its source; with a folder source, `output` is a folder that mirrors the layout. Needs xWMAEncode, plus ffmpeg for anything that is not PCM WAV. |
| `audio_convert_from_xwm` | `source` [`output`, `targetExt`] | disk | Decodes one XWM file to WAV (default) or another format ffmpeg supports. Without `output` the result goes next to the source. Needs xWMAEncode, plus ffmpeg for anything but WAV. |
| `audio_make_fuz` | `audioSource`, `fuzOutput` [`lipPath`, `noLip`] | disk | Packs audio into a FUZ together with a LIP file. Audio that is not already `.xwm` is converted first. Without `lipPath` it looks for a `.lip` next to the audio; `noLip` leaves the LIP out. Needs BmlFuzEncode (and the XWM tools for the conversion). |
| `audio_extract_fuz` | `fuzPath` [`xwmOutput`, `lipOutput`, `alsoWav`] | disk | Splits a FUZ into XWM and LIP files, next to the FUZ unless outputs are given; `alsoWav` also decodes a WAV. Needs BmlFuzDecode (and xWMAEncode for `alsoWav`). |
| `lod_worlds` | `plugin_paths`, `data_dir`, `archive_paths` | no | Lists the loaded worldspaces that carry LOD settings (`southwestX`, `southwestY`, `stride`, `levelMin`, `levelMax`). Paths are newline separated. |
| `lod_plan` | `plugin_paths`, `data_dir`, `archive_paths` [`ledger`] | no | Plans which LOD tiles need a rebake against a stored ledger and returns `plan`, `ledger` and `commit`. Tiles come from the world's `lodsettings` grid, so a missing tile is planned too. Planning never writes, and `ledger` may be omitted for a first run. |
| `lod_preview` | `plugin_paths`, `data_dir`, `archive_paths`, `world`, `level`, `x`, `y` [`object`] | temp | Writes one LOD tile to a temp file and returns its path, so it can be inspected with `nif_geometry`. The terrain `.btr` by default; `object:true` writes the `.bto` the generator bakes for that tile (inside `Objects\`), and a cell that holds no object tile is a `not found` error rather than an empty file. Previews are temporary: the host keeps the 8 newest and deletes the rest when it exits, so read a path before making many more. |
| `lod_candidates` | `plugin_paths`, `data_dir`, `archive_paths`, `world`, `level` | no | Lists the object LOD candidates for one world and level, with `counts` per skip reason so an empty result explains itself, plus `tiles` (the object tiles those candidates occupy). Read-only. |
| `lod_generate` | `plugin_paths`, `data_dir`, `archive_paths`, [`ledger`], `apply` | disk | Bakes the planned terrain and object tiles into `data_dir`. Write-gated: `apply:false` only reports `would-write` and needs no data folder; `apply:true` replaces each tile atomically and returns the merged ledger, and a second run writes nothing because a current tile is never rebuilt. Object tiles are written as `.bto` after the world's three atlas maps are packed, the two terrain atlases each `.btr` names are written beside it, and the report counts `empty`, `failed`, the texture rows and `unsupportedCandidates` (objects it could not place, never tiles). |
| `lod_verify` | `plugin_paths`, `data_dir`, `archive_paths` [`ledger`] | no | Reports each tile's real on-disk `state` (`ok`, `stale`, `partial`, `missing`) plus ledger currency and `inArchive`, so a ledger entry without a file never reads as present. Without `data_dir` it reports ledger currency only (`local: no-data-dir`). |
| `lod_generate_world` | `plugin_paths`, `data_dir`, `archive_paths`, `world`, [`ledger`], `apply` [`level_min`, `level_max`] | disk | `lod_generate` for one worldspace: bakes only that world's planned tiles into `data_dir`, with the same `apply` write gate and returned ledger. An unknown world is a `not found` error. `level_min` and `level_max` scope the bake to a level range (the report gains `scope`), and a level left out or `0` keeps the world's own range. |
| `lod_verify_world` | `plugin_paths`, `data_dir`, `archive_paths`, `world` [`ledger`] | no | `lod_verify` for one worldspace. Read-only. |
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
- **New plugins go to the new-plugin folder and nowhere else.** The folder defaults to
  `<MO2 mods>/FO4IDE New Plugins`, not the overwrite folder. `NewPluginFolder` replaces it. It is set
  in the editor under Settings (New Plugin Folder), in `settings.json` or through the `SaveSettings`
  call, which refuses a relative path. The folder is
  read and created when the MO2 profile loads, so an empty `FO4IDE New Plugins` mod folder can appear
  after an MO2 refresh even when no plugin is created. A `NewPluginFolder` that is relative, cannot be
  created, or lies inside the game Data folder makes the profile load fail with `NewPluginRootInvalid`
  or `NewPluginRootProtected`, and the previous profile and its new-plugin folder are restored. `create_plugin` takes a file name,
  never a path, and never overwrites a file that already exists there. The server does not edit
  `modlist.txt` or `ModOrganizer.ini`. For a folder in MO2's mods folder, the steps after
  `create_plugin` are F5 in MO2 and enabling the mod by hand (MO2 behaviour, not verified here).
- **Unproven structures fail closed.** `create_record` builds only record types it has a verified
  layout for, and `set_field` refuses paths it cannot validate, rather than guessing.
- **Reloading cannot silently lose work.** `reload_mo2_profile` fails while any plugin has unsaved
  edits unless `discard_pending` is `true`.
- **Paths are checked before a tool runs.** A tool that takes a file or folder path refuses an absolute
  path outside the allowed roots, a `..` segment, and a write to an executable, script, installer or
  `ModOrganizer.ini`. See "Allowed paths" below.

The MCP tools cannot copy a record into another plugin as an override; the desktop editor can. Edit
the plugin you own directly rather than stacking a fix plugin on top, unless you specifically want an
override: fewer plugins, fewer conflicts, fewer masters.

---

## Allowed paths

The tools that take a file or folder path check it before they run. The path is resolved first: a relative
path is taken from the server's working directory, and links are followed. The resolved path must lie inside
one of these roots:

- the parent of the configured `Data` folder (`DataFolder`, or `FO4_DATA_PATH`). In a normal install that is
  the Fallout 4 install folder. The rule is the parent of `Data`, not the install folder as such;
- the `Data` folder itself;
- the Fallout 4 install folder from the `Fallout4Path` setting or `FALLOUT4_PATH`, or the one the editor finds on
  its own (the registry or a Steam library). The first install found is kept for the rest of the session; while
  none is found, the search runs again at most once a minute;
- the MO2 instance folder when one is set, and, while a profile is loaded, its mods folder and overwrite folder;
- each folder listed in the `FO4IDE_MCP_ALLOWED_ROOTS` environment variable, separated by `;`.

A folder with no parent is never a root: a drive root (`C:\` or `/`) or a network share root. So a `Data` folder
kept at a drive root allows only that folder. A folder outside these roots, such as a mod folder kept somewhere
else, must be listed in `FO4IDE_MCP_ALLOWED_ROOTS` before the tools can open or write files there.

Network paths (`\\server\share\...`, `//server/share/...`) and device paths (`\\.\...`, `\\?\...`) are refused before
the path is resolved, unless they lexically match one of the roots. Drive-relative paths (`C:file.nif`) and
root-relative paths are refused.

Data-relative arguments are the exception to the working-directory rule. The `rel_*` arguments of `get_texture` and
`get_geometry_batch` and the `flags` argument of `compile_papyrus` name files in the game data, so they are not
checked against the roots. They still refuse `..` segments and absolute paths outside the roots.

A refused path returns `isError: true` with `Path refused for '<argument>': <reason>`.

The `lod_*` tools take `plugin_paths` and `archive_paths` as newline-separated lists, and the `imports` arguments are
semicolon-separated lists. The policy splits each list the way its tool does and checks every entry. Every other
argument is one path and is checked whole, so a `;` in it is part of a file or folder name. A path containing a control character (a NUL, a vertical tab and so on) is refused. `lod_generate` and `lod_generate_world` write tiles under `data_dir`, so they check `data_dir` as a write; the other `lod_*`
tools only read it. `ledger` is the ledger text itself, not a file. `resolve_conflict` creates its `patch` plugin when it
is not loaded, so `patch` follows the plugin-name rule below.

A segment made only of dots or spaces, other than `.`, is refused. A write is refused when its file name, or the
file name it resolves to through a link, ends in `.exe`, `.dll`, `.bat`, `.cmd`, `.ps1`, `.vbs`, `.js`, `.msi`,
`.lnk`, `.scr`, `.com`, `.py`, `.pyw`, `.hta`, `.vbe`, `.jse`, `.wsf`, `.wsh`, `.reg`, `.jar`, `.psm1`, `.cpl`,
`.url`, `.pif` or `.inf`, or is `ModOrganizer.ini`, or contains a colon (an alternate data stream). Trailing dots and
spaces are ignored in that check, as Windows ignores them, and an existing file named by its 8.3 short name is judged
by its long name.

**What the file-type denylist covers elsewhere.** It checks the named output file of a write, such as `output`,
`out_path`, `out_nif` or the `path` of `script_write_script`. Two tools create files inside an output folder under
names they build themselves. `create_seq_file` names the file after the loaded plugin with a `.seq` extension and
refuses an output folder outside the loaded plugin's own folder. `graph_compile_to_pex` names its `.pex` and `.psc`
after the script name in the graph document, so it refuses a script name that is not made only of ASCII letters,
digits and underscore (a namespaced name such as `Ns:Script` is refused too) and is not a Windows device name such as
`NUL`. The managed `graph_compile` applies the same rule to its output names, and the managed `decompile_papyrus` falls
back to the input's file name when the script name inside a `.pex` is not made of such parts. The bulk
extraction tools `archive_extract_all` and `archive_extract_selected` (`out_dir`) apply the same file-type rules to
archive entry names: if any selected entry has a protected name, the whole call is refused and nothing is written.

Links are followed before the root test, so a link inside a root that points outside it is refused. A local link
that points at a network share is still followed by the resolver. The check and the open are two steps, so a
link swapped in between them can still redirect the open. The checks stop a tool call that names a bad path; they
do not stop someone who already controls a folder inside a root. Closing the swap needs each tool to open the file
by handle and compare it with the checked path, which is not done.

A compiler path must be an absolute path to `PapyrusCompiler.exe` (any letter case) inside the roots; leaving it out
uses the configured compiler.

### Covered tools

124 tools take at least one checked path argument, 214 arguments in all, one cell each in the table below (`compiler_path` is in the Compiler column). Each argument is declared in the code, in a type tag of the
catalog tables (`McpCatalogRead.cpp`, `McpCatalogEdit.cpp`, `McpCatalogService.cpp`) or in the argument list of the
hand-written tool in `McpTools.cpp`, and the test `McpPathPolicyTests.cpp` calls every one of them with an outside path. On top
of these, every string argument named `plugin` or ending in `_plugin` is checked as a plugin name: a bare file name passes, and
a name with a directory part must satisfy the write rules above. `compile_papyrus` also checks `compiler_path` as a compiler path.

| Tool | Read | Write | Data-relative | Plugin name | Compiler |
|---|---|---|---|---|---|
| `open_plugin` | `path` |  |  |  |  |
| `nif_inspect` | `path` |  |  |  |  |
| `nif_verify` | `path` |  |  |  |  |
| `nif_tree` | `path` |  |  |  |  |
| `nif_geometry` | `path` |  |  |  |  |
| `nif_segments` | `path` |  |  |  |  |
| `nif_ssf_generate` | `path` | `out` |  |  |  |
| `nif_import` | `obj_path` | `out_nif` |  |  |  |
| `nif_create_texture_set` | `path` | `out_nif` |  |  |  |
| `nif_attach_alpha` | `path` | `out_nif` |  |  |  |
| `nif_remove_block` | `path` | `out_nif` |  |  |  |
| `nif_block_op` | `path`, `donor_path`, `pose_path` | `out_nif` |  |  |  |
| `nif_keys_get` | `path` |  |  |  |  |
| `nif_pose_list` | `pose_path`, `path` |  |  |  |  |
| `nif_export_gltf` | `path` | `out` |  |  |  |
| `nif_import_gltf` | `gltf_path`, `skeleton` | `out_nif` |  |  |  |
| `nif_collision_info` | `path` |  |  |  |  |
| `nif_collision_set` | `path`, `donor_path` | `out_nif` |  |  |  |
| `nif_collision_remove` | `path` | `out_nif` |  |  |  |
| `nif_connect_points_get` | `path` |  |  |  |  |
| `nif_connect_points_set` | `path` | `out_nif` |  |  |  |
| `nif_uv_get` | `path` |  |  |  |  |
| `nif_uv_transform` | `path` | `out_nif` |  |  |  |
| `nif_vertex_color_get` | `path` |  |  |  |  |
| `nif_vertex_color_set` | `path` | `out_nif` |  |  |  |
| `nif_shape_op` | `nif_path` | `out_nif` |  |  |  |
| `nif_skin_influences` | `nif_path` |  |  |  |  |
| `nif_skin_influenced_by` | `nif_path` |  |  |  |  |
| `nif_skin_validate` | `nif_path` |  |  |  |  |
| `nif_skin_binding` | `nif_path`, `skeleton_path`, `skeleton_nif_path` |  |  |  |  |
| `nif_skin_edit` | `nif_path` | `out_nif` |  |  |  |
| `hkx_skeleton` | `skeleton_path` |  |  |  |  |
| `hkx_pose` | `skeleton_path`, `animation_path` |  |  |  |  |
| `hkx_summary` | `path` |  |  |  |  |
| `hkx_animation_inspect` | `animation_path` |  |  |  |  |
| `hkx_gltf_save` | `template_path`, `skeleton_path`, `gltf_path` | `out_path` |  |  |  |
| `hkx_subgraph` | `plugin_path`, `data_folder` |  |  |  |  |
| `hkx_objects` | `path` |  |  |  |  |
| `hkx_scene` | `path` |  |  |  |  |
| `hkx_root_motion` | `skeleton_path`, `animation_path` |  |  |  |  |
| `hkx_skeleton_document` | `skeleton_path` |  |  |  |  |
| `hkx_behavior_graph` | `path` |  |  |  |  |
| `hkx_behavior_model` | `path` |  |  |  |  |
| `hkx_behavior_authoring` | `path` |  |  |  |  |
| `hkx_behavior_validate` | `path` |  |  |  |  |
| `hkx_behavior_edit` | `path` | `out_path` |  |  |  |
| `hkx_subgraph_id` | `data_folder`, `mo2_instance_dir` |  |  |  |  |
| `hkx_project_chain` | `data_folder`, `mo2_instance_dir` |  |  |  |  |
| `hkx_project_check` | `data_folder`, `mo2_instance_dir` |  |  |  |  |
| `hkx_ragdoll` | `path` |  |  |  |  |
| `hkx_ragdoll_save` | `path` | `out_path` |  |  |  |
| `hkx_ragdoll_preview` | `path` |  |  |  |  |
| `hkx_mapper` | `path` |  |  |  |  |
| `hkx_mapper_edit` | `path` | `out_path` |  |  |  |
| `hkx_project_index` | `root` |  |  |  |  |
| `hkx_animation_index` | `data_folder`, `mo2_folder` |  |  |  |  |
| `hkx_object` | `path` |  |  |  |  |
| `hkx_object_tree` | `path` |  |  |  |  |
| `hkx_packfile` | `path` |  |  |  |  |
| `hkx_repack_check` | `path` |  |  |  |  |
| `hkx_search` | `path` |  |  |  |  |
| `hkx_channels` | `skeleton_path`, `animations_path` |  |  |  |  |
| `hkx_symbol_usage` | `path` |  |  |  |  |
| `hkx_skeleton_edit` | `skeleton_path` |  |  |  |  |
| `hkx_skeleton_save` | `skeleton_path` | `out_path` |  |  |  |
| `hkx_clip_edit` | `path` | `out_hkx` |  |  |  |
| `hkx_convert` | `path` | `out_hkx` |  |  |  |
| `compile_papyrus` | `source`, `imports` | `output` | `flags` |  | `compiler_path` |
| `decompile_papyrus` | `source` | `output` |  |  |  |
| `papyrus_check` | `source`, `imports` |  |  |  |  |
| `audio_convert_to_xwm` | `source` | `output` |  |  |  |
| `audio_convert_from_xwm` | `source` | `output` |  |  |  |
| `audio_make_fuz` | `audioSource`, `lipPath` | `fuzOutput` |  |  |  |
| `audio_extract_fuz` | `fuzPath` | `xwmOutput`, `lipOutput` |  |  |  |
| `material_inspect` | `path` |  |  |  |  |
| `material_set_fields` |  | `path`, `output` |  |  |  |
| `archive_pack` | `sources`, `rootDir` | `output` |  |  |  |
| `compare_plugin_to` | `other_path` |  |  |  |  |
| `validate_plugin` | `contract` (a JSON object passed inline is not a path and is not checked) |  |  |  |  |
| `strip_masters_clean` | `source_path` | `output_path` |  |  |  |
| `get_texture` |  |  | `rel_model_path`, `rel_tex_path` |  |  |
| `get_geometry_batch` |  |  | `rel_model_paths_json` |  |  |
| `render_model_preview` |  |  | `model_path` |  |  |
| `get_model_bounds` |  |  | `model_paths` |  |  |
| `previs_scan` | `data_dir` |  |  |  |  |
| `bake_oc_nif` | `source_nif`, `oc_shell` | `output_oc_nif` |  |  |  |
| `generate_cell_previs` | `oc_shell` |  |  |  |  |
| `write_previs_container` | `source_path` | `output_path` |  |  |  |
| `create_seq_file` |  | `output_dir` |  |  |  |
| `fix` | `nif_path` | `out_nif` |  |  |  |
| `shape_op` | `nif_path` | `out_nif` |  |  |  |
| `script_analyze` | `path` |  |  |  |  |
| `script_symbol_at` | `path`, `imports` |  |  |  |  |
| `script_read_script` | `path` |  |  |  |  |
| `script_write_script` |  | `path` |  |  |  |
| `archive_extract_file` | `archive_path` | `out_path` |  |  |  |
| `archive_extract_selected` | `archive_path` | `out_dir` |  |  |  |
| `archive_extract_all` | `archive_path` | `out_dir` |  |  |  |
| `archive_compare` | `archive_path_a`, `archive_path_b` |  |  |  |  |
| `archive_list` | `archive_path` |  |  |  |  |
| `resolve_asset` |  |  | `path` |  |  |
| `graph_compile_to_pex` |  | `output_directory` |  |  |  |
| `graph_load` | `path` |  |  |  |  |
| `load_script` | `path` |  |  |  |  |
| `graph_save` |  | `path` |  |  |  |
| `get_quest_script` | `imports_directory` |  |  |  |  |
| `script_complete` | `path`, `imports` |  |  |  |  |
| `script_signature_help` | `path`, `imports` |  |  |  |  |
| `script_check_text` | `path`, `imports` |  |  |  |  |
| `script_references` | `path`, `imports` |  |  |  |  |
| `script_rename` | `path`, `imports` |  |  |  |  |
| `script_workspace_symbols` | `imports` |  |  |  |  |
| `set_quest_fragment` | `imports_directory` | `output_directory` |  |  |  |
| `remove_quest_fragment` | `imports_directory` | `output_directory` |  |  |  |
| `edit_quest_script` | `imports_directory` | `output_directory` |  |  |  |
| `resolve_conflict` |  |  |  | `patch` |  |
| `lod_worlds` | `plugin_paths`, `data_dir`, `archive_paths` |  |  |  |  |
| `lod_plan` | `plugin_paths`, `data_dir`, `archive_paths` |  |  |  |  |
| `lod_preview` | `plugin_paths`, `data_dir`, `archive_paths` |  |  |  |  |
| `lod_candidates` | `plugin_paths`, `data_dir`, `archive_paths` |  |  |  |  |
| `lod_generate` | `plugin_paths`, `archive_paths` | `data_dir` |  |  |  |
| `lod_verify` | `plugin_paths`, `data_dir`, `archive_paths` |  |  |  |  |
| `lod_generate_world` | `plugin_paths`, `archive_paths` | `data_dir` |  |  |  |
| `lod_verify_world` | `plugin_paths`, `data_dir`, `archive_paths` |  |  |  |  |

### The managed server

The older managed server (`FO4RecordEditor --mcp` over stdio, the loopback HTTP server of the desktop app, and the
in-app AI agents) runs the same checks in `PluginToolExecutor`, before any tool body runs. The refusal text is the same,
`Path refused for '<argument>': <reason>`. The rules for `..`, dot-only segments, control characters, drive- and
root-relative paths, network and device paths, alternate data streams, protected file names, compiler paths and plugin
names are the ones above. Differences:

- The roots are the Data folder (the `DataFolder` setting in the app, `--data` in the headless hosts) and its parent, the
  Fallout 4 install folder when it is found, the MO2 instance given by `--mo2` or the setting with its `mods` and
  overwrite folders (the `mods` folder is allowed as soon as an instance is set, not only while a profile is loaded), the
  game Data folder named in `ModOrganizer.ini`, and the `FO4IDE_MCP_ALLOWED_ROOTS` entries. With no Data folder set, only
  the other roots apply.
- Each tool's checked arguments are declared in `McpPathPolicy.cs`, and a test fails when a tool gains a string or array
  argument that is neither declared nor on the test's not-a-path list. Every string argument named `plugin` or
  ending in `_plugin` is checked as a plugin name.
- `flags` of `compile_papyrus` and `path` of `resolve_asset` are data-relative.
- `archive_extract_all` applies the protected file-type rule to archive entry names, as the native bulk extraction
  tools do: one protected entry name refuses the whole call and nothing is written. The desktop app's own extraction
  does not apply it.

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
shipped in the package. The package always includes ffmpeg. The Creation Kit Wiki mirror
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
- **NIF commands need no external tool.** Fix, import (including box collision), glTF export, inspect, geometry,
  verify, tree and saving NIF edits all run in process. Fix repairs tangents, adds a missing BSX flag,
  clears the BSX editor-marker bit and trims texture and material name whitespace; it does not change shader
  flags or any other BSX bit, and it leaves the
  tangents of shapes without normal, tangent and UV attributes alone.
- **`archive_pack` says `Archive2.exe is not configured`.** Install the Creation Kit or set
  `ARCHIVE2_PATH`.
- **An audio tool answers `Error: set ..._PATH or place ... in tools/audio`.** A helper program is
  missing. Set the environment variable the message names, or put the program in `tools\audio`.
- **Nothing starts, with no window and no output.** Run the exe with `--health` from a terminal. On a
  clean Windows install the usual cause is a missing Microsoft Visual C++ 2015-2022 Redistributable
  (x64), which the package needs.

