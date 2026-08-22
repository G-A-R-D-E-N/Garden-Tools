# Papyrus source ZIP group-array population

**Binary:** `CreationKit.exe` 1.11.137.0<br>
**Subsystem:** `EditorPapyrus` initialization<br>
**Relevant object:** `editor` / `param_1` in `FUN_141123e20`

## Summary

The CK does **not** contain a hard-coded `Base/CreationClub/DLC01...` ZIP
key table and does not scan `Data\\Scripts\\Source` to discover groups. It parses
the Papyrus setting `sAdditionalImports:Papyrus` into the source array at
`editor+0x410`, copies and normalizes that list into `editor+0x428`, then derives
the INI stamp key from each entry's final path component:

```text
PapyrusSource_<PathFindFileNameA(import-entry)>
```

The installed `CreationKit.ini` has:

```ini
[Papyrus]
sScriptSourceFolder = ".\\Data\\Scripts\\Source\\User"
sAdditionalImports = "$(source);.\\Data\\Scripts\\Source\\Base"
```

Therefore the **stock default for this installation** generates exactly:

```text
PapyrusSource_User
PapyrusSource_Base
```

If a user adds more semicolon-separated import paths, the same formatter
creates corresponding keys. For example, adding
`.\\Data\\Scripts\\Source\\DLC01` creates `PapyrusSource_DLC01`; the names
`CreationClub` and `DLC01`–`DLC06` seen as directories on disk are not
automatically expected unless they occur in `sAdditionalImports`.

## Population and copy chain

### 1. `sAdditionalImports` storage feeds `editor+0x410`

The Papyrus descriptor/key cluster is:

| Address | Role |
|---|---|
| `0x14310b738` | literal `sAdditionalImports:Papyrus` |
| `0x143bfb9f0` | descriptor/global pointer associated with that key |
| `0x143bfb9e8` | current string-value storage passed to the parser (empty in the on-disk image before INI loading) |
| `0x14310b870` | literal `$(source)` |

`FUN_141123e20` initializes the array and passes the loaded
`sAdditionalImports` value to `FUN_14112ab00`:

```asm
141124070  lea  rcx,[r14+410h]
141124077  call  FUN_14003f319       ; initialize source BSTArray
14112412a  mov  rcx,[143bfb9e8]      ; sAdditionalImports value
141124131  lea  rdx,[r14+410h]
141124138  call  FUN_14112ab00       ; split/assign import entries
```

The settings reader loads the value before this Papyrus initialization path.
The embedded image's zero/empty pointer is only the pre-INI default state; it
must not be mistaken for a literal empty import list at runtime.

### 2. `FUN_14112ab00` — import-list assignment

`FUN_14112ab00(source, destination)` clears the destination and parses the
loaded import-list string into `BSTArray<BSFixedString>` entries. The input is
the semicolon-separated `sAdditionalImports` value; each resulting entry is a
path string. There are no `PapyrusSource_` literals in this helper.

The default value observed in the installed INI produces two source entries:

```text
$(source)
.\\Data\\Scripts\\Source\\Base
```

### 3. `FUN_141126280` — copy to working array and `$(source)` expansion

`FUN_141126280(editor)` reads the source descriptor and writes the working
array:

```c
source.data  = *(void**)(editor + 0x410)
source.count = *(uint32_t*)(editor + 0x420)
working      = editor + 0x428
```

For each source `BSFixedString`:

- normal entries are copied into `editor+0x428`;
- the special string handle `DAT_143e08bc8` (initialized from `$(source)` at
  `0x14310b870`) is replaced by the effective script-source-folder field
  `editor+0x3f0`;
- if no `$(source)` entry is present, the fallback appends `editor+0x3f0`
  through `FUN_140359830` (`BSTArray::PushBack`-style helper).

The exact field operations are in `decompile-ziparraycopy-0x1126280.log` and
at `0x141126280`.

### 4. `FUN_141129aa0` — basename extraction and ZIP/key construction

The extraction loop receives `editor+0x428`:

```asm
14112433f  lea  rcx,[r14+428h]
141124346  call FUN_141129aa0
```

Its first pass converts each import path into a pair:

```text
pair[0] = PathFindFileNameA(entry)   // group, e.g. User or Base
pair[1] = entry                       // full import path
```

The second pass consumes that pair and performs:

```text
zipPath = PathAppendA(entry, group) + ".zip"
key     = sprintf_s("PapyrusSource_%s", group)
```

For the installed default this yields, in import-list order:

```text
.\\Data\\Scripts\\Source\\User\\User.zip
    [ZipInstall] PapyrusSource_User
.\\Data\\Scripts\\Source\\Base\\Base.zip
    [ZipInstall] PapyrusSource_Base
```

The relevant literals are:

| Address | Literal |
|---|---|
| `0x14310c038` | `.zip` |
| `0x14310c040` | `PapyrusSource_%s` |
| `0x14310c058` | `PapyrusTemp\\` |

The generated key is passed as `param_3` to `FUN_14063ab40`, which reads and
writes the 8-byte stamp in `[ZipInstall]` in `CreationKitPrefs.ini`.

## Configurable key set

The CK accepts any basename produced by the configured import path. The stock
Fallout 4 directory tree contains these directories:

```text
Base, CreationClub, DLC01, DLC02, DLC03, DLC04, DLC05, DLC06, User
```

The shipped install has ZIPs for `Base`, `CreationClub`, and `DLC01`–`DLC06`;
`User` has only `Readme.txt`. Nevertheless, directory presence alone does not
make a `PapyrusSource_<group>` key active. A group becomes active when its
path is present in `sAdditionalImports` (or when the `$(source)` token expands
to that group directory).

Examples of generated names when configured:

| Import basename | Generated key |
|---|---|
| `Base` | `PapyrusSource_Base` |
| `CreationClub` | `PapyrusSource_CreationClub` |
| `DLC01` | `PapyrusSource_DLC01` |
| `DLC02` | `PapyrusSource_DLC02` |
| `DLC03` | `PapyrusSource_DLC03` |
| `DLC04` | `PapyrusSource_DLC04` |
| `DLC05` | `PapyrusSource_DLC05` |
| `DLC06` | `PapyrusSource_DLC06` |
| `User` | `PapyrusSource_User` |

This is a **derivable namespace**, not a fixed nine-entry expectation.

## Confidence and scope

- **Confirmed from code:** `sAdditionalImports` storage is passed into the
  `editor+0x410` parser; `$(source)` substitution; copy to `+0x428`; basename
  extraction; `.zip` suffix; and `PapyrusSource_%s` formatting.
- **Confirmed from the installed configuration:** the stock default produces
  `User` and `Base` in that order.
- **Confirmed from the installed data tree:** the additional stock group
  directories and ZIP basenames listed above.
- **Not claimed:** that every arbitrary mod-created directory is enumerated.
  The decompiled path is configuration-driven and does not perform a directory
  census in `FUN_141126280` or `FUN_141129aa0`.

## Sources

- `ghidra-dump/decompile-papyrusinit-0x1123e20.log`
- `ghidra-dump/decompile-ziparraycopy-0x1126280.log`
- direct disassembly of `0x141124070`–`0x141124138`, `0x141126280`, and
  `0x141129aa0`
- `ghidra-dump/decompile-zipinstall-0x63ab40.log`
- installed `CreationKit.ini`, `[Papyrus]` section
- installed tree: `Data\\Scripts\\Source\\{Base,CreationClub,DLC01-DLC06,User}`
