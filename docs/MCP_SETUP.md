# FO4RecordEditor as an MCP server

## What this actually is

MCP (Model Context Protocol) is a standard way to hand an AI assistant a set of tools it can call.
FO4RecordEditor implements it: launched with `--mcp`, it does not open a window. It sits on stdin and
stdout speaking JSON-RPC, and it advertises **57 tools** for reading and writing Fallout 4 plugins.

The practical effect is that your AI stops guessing about your load order and starts *querying* it. It
can open a plugin, resolve an EditorID, read the winning override of a record, edit a field, save, and
run a validity check — without you opening xEdit once, and without the AI ever hand-writing a binary
record. Every write goes through Mutagen, so the file it produces is structurally valid or the call
fails loudly.

It is not a chatbot and it has no model inside it. It is the hands; your AI client is the brain.

## Wiring it up

Create `.mcp.json` in the folder you run your AI from (Claude Code reads it from the project root). A
ready-made copy sits next to the exe as `mcp.sample.json`.

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

Use **backslashes, doubled** — it is JSON, so `C:\Tools` must be written `C:\\Tools`. Use the absolute
path to the exe.

### Choosing the load-order source

The server needs to know what your game sees. Pass exactly one of:

| Argument | Use when | What it does |
|---|---|---|
| `--mo2 <instance>` | You use Mod Organizer 2 | Reads the active profile's `plugins.txt` and rebuilds that exact load order from `mods\`. This is the accurate one — conflicts and overrides resolve the way they do in game. |
| `--data <folder>` | Vanilla / no mod manager | Uses a plain `Data` folder. |

The MO2 instance path is the folder containing `mods\` and `profiles\` — not the MO2 program folder,
and not a single mod.

### Confirming it connected

Restart the client, or in Claude Code run `/mcp reconnect`. Then ask it to call `list_plugins` — if
you get your load order back, it works. (`ping` is a JSON-RPC method the client uses for its own
health checks, not a tool; asking the AI to "call ping" will not work.)

> **After you rebuild the exe, the running server is the old code.** Stop every `FO4RecordEditor.exe`
> process, rebuild, then `/mcp reconnect`. Skipping the reconnect is the single most common way to
> spend twenty minutes debugging a fix that already worked.

---

## The tool surface

### Plugin lifecycle
`open_plugin` · `create_plugin` · `save_plugin` · `reload_plugin` · `backup_plugin` · `list_plugins`
· `check_plugin` · `clean_plugin` · `strip_masters_clean`

### Reading and analysis
`get_record` · `get_winning_record` · `list_records` · `list_records_summary` · `list_record_types` ·
`search_records` · `search_all` · `resolve_editorid` · `get_scripts` · `diff_records` ·
`get_referenced_by` · `get_problems` · `search_robco_configs`

### Conflict detection
`get_conflicts` · `scan_conflicts` · `scan_all_plugins` · `scan_broken_refs`

### Writing records
`create_record` · `set_field` · `set_conditions` · `set_components` · `add_list_item` ·
`remove_list_item` · `attach_script` · `set_script_property` · `copy_as_override` · `delete_record` ·
`renumber_formid` · `compact_to_esl` · `batch_patch_records` · `revert_overrides`

### Cells and placed objects
`create_cell` · `create_placed_object`

`create_record` only adds **top-level** records, so it cannot make either of these — a placed object
lives inside a cell's persistent/temporary list, and cells live in a block tree. This is the pair you
need for map markers and for parking references you relocate at runtime.

### Struct lists that `add_list_item` cannot express
`add_leveled_entry` · `set_perk_effects` · `set_magic_effects` · `set_quest_aliases` ·
`set_quest_stages` · `set_quest_objectives` · `set_furniture_markers` · `set_message_buttons`

### Papyrus (built in — no Champollion needed)
`compile_papyrus` (`.psc` → `.pex`) · `decompile_papyrus` (`.pex` → `.psc`, recursive)

### NIF
`nif_import` · `nif_inspect` · `nif_fix` · `nif_verify`

### Escape hatch
`run_script` — executes C# against the loaded mod through Mutagen, for anything the tools above do not
cover. `host.New(sig, editorId)` creates records from inside a script.

---

## Rules that keep it from corrupting your plugins

These are not style preferences. Each one is a real failure that has happened.

**1. `create_record` takes signatures; `list_records` takes full names.** `create_record` wants
`COBJ`, `WEAP`, `GLOB`. `list_records` wants `ConstructibleObject`, `Weapon`, `GlobalVariable`. Passing
the wrong form to either returns "no records of that type", which reads like a bug and is not one.

**2. Check the master order after every save on a multi-master plugin.** A plugin whose masters are
written out of load order makes the game **hang on load with no crash log** — the worst possible
failure mode, because there is nothing to read. After any `save_plugin`, verify the master list.

**3. FormID encoding is `(masterIndex << 24) | objectId` for all masters.** Not just for ESLs.

**4. A new `GLOB`'s value is set via `set_field` on `Data`** — not by a constructor argument.

**5. `.pas` is assembly, not source.** `compile_papyrus` compiles `.psc`. Feeding it the decompiler's
assembly listing is a common and confusing mistake; the tool now detects it and tells you.

**6. Edit the source plugin directly rather than stacking a fix plugin on top,** unless you have a
specific reason to want an override. Fewer plugins, fewer conflicts, fewer masters.

**7. Deleted records lose their EditorID.** Mutagen strips it. Do not try to filter a deleted-record
pool by EditorID; there is nothing to match on.

---

## When something is wrong

- **The AI reports "not found" for a tool that should exist** → the server is an old build, or it
  never started. Reconnect.
- **`compile_papyrus` says the compiler is missing** → install the Creation Kit, or set
  `PAPYRUS_COMPILER_PATH`. The compiler is Bethesda's and cannot be shipped here.
- **The `nif_*` tools fail** → `tools\niftool\niftool.exe` is missing from the package, or
  `NIFTOOL_PATH` points somewhere stale.
- **Nothing starts at all, no window, no log** → almost always the .NET 9 **Desktop** Runtime is
  missing (the ASP.NET or plain runtime is not enough), or you are launching a self-contained build
  under MO2. Startup traces land next to the exe as `FO4RecordEditor.startup.log`, falling back to
  `%APPDATA%\FO4RecordEditor\startup.log` when the exe folder is read-only.
