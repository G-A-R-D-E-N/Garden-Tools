# PreCombined / PreVis Export Pipeline (`-GeneratePreCombined`, `-GeneratePreVisData`)

Full decompile walk of the CK's headless precombined/previs export. All addresses are RVAs in
`CreationKit.exe` 1.11.137.0 (image base `0x140000000`). The dispatcher evidence is in
[`ghidra-dump/CLI-DISPATCHER-FUN_1404870f0.txt`](ghidra-dump/CLI-DISPATCHER-FUN_1404870f0.txt).

## Command-line contract

Both switches use the same dispatcher block and differ only in the mode byte passed to the
process (`0 = PreCombined`, `1 = PreVis`):

```text
-GeneratePreCombined:<ESMFilename> [clean|filtered] [all|main|ints|other] [currentset totalsetcount]
-GeneratePreVisData:<ESMFilename> [clean|filtered] [all|main|ints|other] [currentset totalsetcount]
```

The square-bracket groups are optional and are space-separated after the colon. The usage
string's `currentset(1-totalsetcount)` wording describes the intended job values; the parser's
actual acceptance rules are documented below and are intentionally a little looser.

## Dispatcher parsing (`FUN_1404870f0`)

The relevant block starts at RVA `0x4870f0` and selects the argument slot for the set bit:

- `0x40000` = `-GeneratePreCombined` (`bVar3 = 1`)
- `0x80000` = `-GeneratePreVisData` (`bVar3 = 0`)
- `pcVar8 = strtok_s(slot, " ", ...)` is the required ESM token.
- The next two tokens are read as `mode` and `scope`; the delimiter is one literal space, so
  tabs are not separators and quotes have no special meaning here.
- Comparisons use `_stricmp`, so mode and scope tokens are case-insensitive.
- The branch reports the usage string only when the ESM token is absent or a supplied mode or
  scope is unrecognized. It does not reject extra trailing tokens.

### Exact token table and defaults

| position | accepted token(s) | native result |
|---|---|---|
| 1 | `<ESMFilename>` | required; passed to the file lookup and runner |
| 2 | `clean` | sets the clean flag byte to `0` |
| 2 | `filtered` | leaves the default filtered flag byte at `1` |
| 2 | omitted | accepted; default flag byte remains `1` |
| 3 | `all` | scope `0`; enters the common chunk-token parser |
| 3 | `main` | scope `1`; enters the common chunk-token parser |
| 3 | `ints` | scope `3`; enters the common chunk-token parser |
| 3 | `other` | scope `2`; **does not** enter the chunk-token parser |
| 3 | omitted | accepted; default scope is `0` (`all`) |

A scope is examined only when a valid mode token is present. Thus ESM-only, ESM+mode, and
ESM+mode+scope forms are accepted. The initial pair defaults are `current=1,total=1`; when
`all`, `main`, or `ints` is explicitly supplied, entering the common parser changes the
missing-pair fallback to `current=0,total=1`.

For `all`, `main`, and `ints`, the dispatcher calls `strtok_s(NULL, " ", ...)` twice more:

1. If both tokens exist, it converts both with `atoi`, stores the raw converted values, and
   rejects the pair when `currentset == 0`, `totalsetcount == 0`, or
   `totalsetcount < currentset` (the locals are unsigned, so this comparison is unsigned).
2. If either token is absent, the common parser falls back to its internal defaults
   `current = 0`, `total = 1`; this is accepted rather than reported as a syntax error.
   If scope is omitted, or is `other`, that common-parser fallback is not reached and the
   pre-parser defaults remain `current = 1`, `total = 1`.
3. Words after `totalsetcount` are not inspected. For `other`, all words after the scope are
   ignored because that branch jumps around the common parser.

`atoi` is not a strict decimal validator: a numeric prefix such as `2junk` is converted to 2,
while a token with no numeric prefix converts to 0 and then fails the nonzero check. The intended
and documented job range remains `1 <= currentset <= totalsetcount`; the quirks above are the
compatibility behavior of the binary, not recommended inputs.

The process object is populated from the dispatcher stack object as follows:

| object offset | value at construction |
|---:|---|
| `+0x08` | parser scratch pointer: mode token when no common parser runs, otherwise the fourth token (or null after an empty common parse) |
| `+0x10` | resolved ESM file pointer |
| `+0x18` | packed pair: low dword = native `current` value, high dword = `total` |
| `+0x20` | scope: `all=0`, `main=1`, `other=2`, `ints=3` |
| `+0x24` | mode: `0 = PreCombined`, `1 = PreVis` |
| `+0x25` | process clean/filtered state derived from the clean flag and file state |

Important correction: the dispatcher does **not** subtract one from an explicitly supplied
`currentset`. Only the omitted-pair fallback initializes the low dword to zero. The usage text
calls the value 1-based, but the stored value is the raw `atoi` result; do not model the parser as
an unconditional `currentset - 1` conversion.

The validator oracle mirrors these rules:

```bash
python3 tools/validate_ckini.py --command='-GeneratePreVisData:Fallout4.esm clean all 1 2'
```

## Scope meaning in the process

The parser values are later consumed by `PreVisPreCombineDataProcess::Execute` (`0x48f920`):

| token | value | observed work selection |
|---|---:|---|
| `all` | 0 | interior CELL work and worldspace work |
| `main` | 1 | interior CELL work |
| `other` | 2 | interior CELL work; no dispatcher chunk parse |
| `ints` | 3 | worldspace work |

The `0x4a` passed by the interior enumeration helper (`FUN_140489590`) is the TES4 CELL record
id. The process also contains the message `Running filtered for partial options is not supported`;
that is a downstream process limitation, not a rejection performed by the CLI token parser.

The process collects candidate worldspaces from the data handler's worldspace array
(`+0xf60/+0xf6a`) and applies its per-worldspace predicates. The selected work is then sent to
`0x48b460` for each worldspace. The exact slice arithmetic belongs to `Execute`; the dispatcher
facts above should not be replaced with a presumed `currentset-1` formula.

## Process runner (`0x4906a0`)

The shared runner is used by both modes:

1. Optionally sends the progress-window command and hides the UI.
2. Resolves the ESM with `DataHandler->GetFileByName`.
3. Calls process vtable slot 1 (`Execute`; slot 0 is the destructor).
4. With `bThrowProcessCompleteExceptions:General`, maps failure/success to
   `RaiseException(0x6b650001)` / `RaiseException(0x6b650000)`.

Adjacent settings keys are `bGeneratePreCombinedGeometryPreVis:General` (pipeline enable) and
`bThrowProcessCompleteExceptions:General` (batch exception behavior).

## PreVis/PreCombined process outputs

### PreCombined world generation (`0xd20d90`)

- Ensures `Data\\Meshes` and `Data\\Meshes\\PreCombined` exist, with an optional plugin
  subdirectory `Data\\Meshes\\PreCombined\\<Plugin>\\`.
- Iterates cells and gathers combined-reference-eligible objects.
- Builds combined geometry through the `BSMeshCombiner` family and writes Havok physics data
  (`hknpPhysicsSystemData` family).
- Geometry is named `<FormID%08X>.NIF`; the physics companion is
  `<FormID%08X>_Physics.NIF` under `Data\\Meshes\\PreCombined`.
- `ExtraCombinedRefs` records the cell-to-combined-form mapping persisted in
  `Data\\CombinedObjects.esp`.

### PreVis per-cell generation (`0x1181210`)

- Logs `GeneratingVisibility for cell ...` / `GeneratingVisibility for Interior cell ...`.
- Builds the cell scene and reports failed/no-data scene builds.
- Applies view-distance culling (`DAT_143bd5a90`) to occluder geometry.
- Writes exterior scenes under
  `Data\\Vis\\Scenes\\<Worldspace>_<Plugin>_%+02i_%+02i.scene` and interior scenes under
  `Data\\Vis\\Scenes\\Interior_<CellName>.scene`.
- Records scene paths in the PreVis plugin's visibility records so the runtime can stream them.

The per-world dispatcher is `0x48b460`: PreCombined calls `0xd20d90`; PreVis validates the
worldspace/cell alignment and calls `0x1181210` followed by deferred cleanup `0xd279b0`.

## Output summary

| artifact | path |
|---|---|
| combined geometry | `Data\\Meshes\\PreCombined\\<FormID%08X>.NIF` |
| combined physics | `Data\\Meshes\\PreCombined\\<FormID%08X>_Physics.NIF` |
| combined plugin | `Data\\CombinedObjects.esp` |
| PreVis scenes | `Data\\Vis\\Scenes\\<Worldspace>_<Plugin>_<x>_<y>.scene` or `Interior_<Cell>.scene` |
| PreVis plugin | `Data\\PreVis.esp` |

## Remaining reverse-engineering targets

- Exact `Execute` worldspace slice arithmetic for the raw stored pair, including the last-chunk
  remainder behavior.
- The `.scene` binary format (see [14-SCENE-FORMAT.md](14-SCENE-FORMAT.md)).
- Worldspace filtering predicates at `0x48ac50`, `0x48a890`, `0x48bc60`, and `0x48bb60`.
