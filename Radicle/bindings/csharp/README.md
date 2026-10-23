# Radicle C# bindings

`Radicle.Interop` is a .NET 8 wrapper for Radicle's C ABI (`fo4/c_api.h` and `fo4/c_api_env.h`). It is how GardenEngine and other C# hosts read Fallout 4 load orders, archives and string tables, and write reference overrides, without Mutagen.

## Layout

| Path | Contents |
| --- | --- |
| `Radicle.Interop/RadicleNative.g.cs` | Generated `DllImport` declarations, structs and enums. **Do not edit by hand.** |
| `Radicle.Interop/*.cs` | Safe wrappers with managed records, `IDisposable` handles and `RadicleException`. |
| `Radicle.Interop.Tests` | xunit tests. Fixture plugins, BA2 archives and string tables are built in C#. |
| `tools/generate_interop.py` | Regenerates `RadicleNative.g.cs` and `tests/abi_layout_probe.cpp` from the headers. |

The main wrapper types:

| Type | Covers |
| --- | --- |
| `RadicleEnvironment` | Load order, winners and versions, EditorID lookup, worlds and cells, cell children, placed references, terrain, base records. |
| `StringTables` | Localized strings, including tables inside BA2 archives. |
| `Ba2Archive` / `AssetResolver` | Archives and asset lookup with the game's precedence. |
| `PluginFile` | Override creation, reference field edits and saving. |

## ABI check

`RadicleNative.CompiledAbiVersion` is `RADICLE_ABI_VERSION` from `fo4/c_api.h`, written by the generator. The first call into any wrapper loads the native library through `RadicleLibrary`, which calls the library's `radicle_abi_version` and throws `RadicleAbiMismatchException` naming both numbers when they differ, or when the library does not export that function at all. The check covers a path set with `RadicleLibrary.SetPath`, `RADICLE_NATIVE_PATH`, and the default library search. It does not cover a library that the default search misses and an `AssemblyLoadContext` then supplies (`LoadUnmanagedDll` or `ResolvingUnmanagedDll`); hosts that load native libraries that way should call `RadicleLibrary.SetPath`.

## Regenerating

After changing a public C header, run:

```
python3 Radicle/bindings/csharp/tools/generate_interop.py Radicle/include \
  Radicle/bindings/csharp/Radicle.Interop/RadicleNative.g.cs \
  Radicle/bindings/csharp/tests/abi_layout_probe.cpp
```

## Tests

The CTest `radicle_csharp_interop_up_to_date` fails when the committed generated files no longer match the headers. It needs Python 3.

`AbiGuardTests` loads a second copy of the bindings in its own `AssemblyLoadContext`, so its first native call is a real first call: it accepts the built library, and rejects both a library that reports another ABI (`radicle_abi_stub`, built with the tests and passed as `RADICLE_ABI_STUB`) and one that is not Radicle.

`abi_layout_probe` prints the C `sizeof` and `offsetof` of every public struct. `AbiLayoutTests` compares that output with `Marshal.SizeOf` / `OffsetOf` for every generated struct, so a mapping mistake fails the test and never reaches the caller as a corrupted read.

To run the C# suite:
- **Through CTest:** configure with `-DRADICLE_CSHARP_TESTS=ON` (needs the .NET 8 SDK and NuGet access).
- **Directly:**

  ```
  RADICLE_NATIVE_PATH=<build>/libradicle.so RADICLE_ABI_PROBE=<build>/radicle_abi_layout_probe \
    dotnet test Radicle/bindings/csharp/Radicle.Interop.Tests
  ```

## Loading the native library

Call `RadicleLibrary.SetPath(fullPath)` before any other call, or set `RADICLE_NATIVE_PATH`. Otherwise .NET's default probing looks for `radicle` (`radicle.dll` / `libradicle.so`).

## Quest runtime data

`QuestRuntime` reads one QUST record's VMAD and DNAM. `QuestRuntime.Rows` returns the C ABI's structural rows (`quest`, `script`, `fragment-script`, `fragment`, `alias`, `alias-script`, `unsupported`), `QuestRuntime.Properties` returns the structured `radicle_quest_property` entries with every decoded value, and `Decode` merges both.

Properties are not rows because a VMAD value does not fit the row grammar: a string value can hold a tab or a newline, and an object property carries a FormID and an alias at once. `DecodeRows(rows, properties, quest)` takes both halves, so the row-to-model mapping is testable without a native plugin. Every row kind the C ABI emits is represented, including the attachment rows: a `script` row is a script the quest owns even when it declares no properties, and an `alias-script` row is a script an alias owns. Alias-owned scripts and their properties keep the alias id, so two aliases carrying the same script name stay distinct.

A property's string value crosses the ABI as raw bytes with a length, and `QuestRuntimeValue` exposes both readings. `Text` is the byte-preserving projection of those bytes (byte N becomes U+00NN, which is what `Encoding.Latin1` does), so it round-trips every byte and the 12-byte value Garden, NUL, Patch arrives as a 12-character string rather than the 6 characters a NUL-terminated read would stop at. `TextBytes` is a derived get-only property, `Encoding.Latin1.GetBytes(Text)`, so it is exactly `text_size` bytes, embedded NULs included, and no second copy of the bytes is stored: `Text` is the only string member, so it is the one the record's synthesised value equality compares, and two structurally identical decodes stay equal. The character set of the bytes is not claimed and this layer never transcodes or decodes as UTF-8; naming an encoding stays the consumer's business, as in the `.pex` and `.seq` notes below. A property holding no string has `Text` and `TextBytes` both null; the empty string has `Text` `string.Empty` and an empty `TextBytes` array. Script names and property names are still read with `Utf8.Read` (`Marshal.PtrToStringUTF8`), so they are still decoded as UTF-8 and still stop at a NUL; whether a shipped plugin can carry an embedded NUL there is UNPROVEN, and this pass did not change them.

A fragment property belongs to the fragment script named by the `fragment-script` row, so its own `Script` is empty and the script name is available exactly once, as `QuestRuntimeData.FragmentScript`.

## Runtime notes

- **Memory.** `RadicleEnvironment` parses every plugin in the load order into memory up front; nothing is read lazily. Measure a real load order with `radicle_env_bench "<Data dir>" [plugin ...]` (built with the Radicle tools), which reports load time, resident and peak memory, archive and string-table open times, and record counts.
- **Snapshots.** `PexFile` and `SeqFile` copy everything they need while they parse, so their properties stay readable after `Dispose`. A `.pex` name or string-table entry stays raw bytes: the container stores one byte per character and the encoding above `0x7F` is UNPROVEN, so this layer never transcodes, and Latin-1 is a reading a consumer may apply to those bytes rather than something the model assumes. A `.seq` entry stays a raw FormID and is never resolved to a `FormKey`; the evidence is in `Radicle/docs/SEQ_FORMAT.md`.
- **Object code (ABI 9).** `PexFile.ReadCode(index)` reads one object's states, functions, parameters, locals, instructions, variables, properties and structs through `radicle_pex_object_code_get`. Unlike the snapshot it needs the live handle, so it throws `ObjectDisposedException` after `Dispose`. Every name and string operand is a string-table index into `StringTable` (or -1 for an empty name the table does not hold), so the bytes stay untranscoded. An instruction's `Arguments` are its decoded operands in file order with the variable-argument count removed; this layer names no operand roles, and what an operand means is the consumer's business.
- **Default values.** A parsed file always stores a value for every variable and struct member, so `PexVariable.DefaultValue` (C `has_default`) is never null for a file read from disk. A variable with no initializer has a value whose `Type` is `PexValueType.None`; test the type, not null.
- **Type names.** The model types in `Radicle.Interop` (`PexValue`, `PexFunction`, `PexState`, `PexObjectCode` and the rest, like the existing `PexHeader` and `PexObjectCounts`) share their names with the raw structs in `Radicle.Interop.Native`. A file outside `Radicle.Interop` that imports both namespaces gets CS0104; qualify the raw ones as `Native.PexValue` or alias them.
- **Function counts.** `PexObjectInfo.Counts.FunctionCount` and C `radicle_pex_object_counts.function_count` retain their pre-ABI-9 meaning: state functions only. `ReadCode(index).Functions.Count` and `radicle_pex_object_code.function_count` count all flattened functions: state functions in state order, followed by each property's getter then setter. `States[*].Functions` contains only state functions; property handler indexes identify their entries in the flattened list. Both-handler regressions pin the distinction without changing the existing snapshot contract or C layout.
- **Duplicate strings.** Code-view names, types and string/identifier operands guarantee the exact semantic byte value, not the original encoded table index. Equal table entries canonicalize to the first matching index, including empty strings; -1 means an empty name absent from the table. `StringTable` itself preserves duplicate entries and their order. Duplicate-entry fixtures deliberately reference a later duplicate and assert that the returned index selects the first entry with identical bytes. `ToBytes()` is a separate serialization operation; index-preserving rewrites are not promised by this code-view contract.
- **Load off the main thread.** Loading the official masters takes long enough to stall a frame. In Godot, build the `RadicleEnvironment` (and `StringTables` / `AssetResolver`) on a background thread or task, never on the main thread, and hand the finished objects over.
- **Shipping the native library.** Export `radicle.dll` (Windows) or `libradicle.so` (Linux) with the Godot build and call `RadicleLibrary.SetPath(fullPath)` at startup, before any other Radicle call.
- **Record handles are borrowed.** A `Record` belongs to the `RadicleEnvironment` that returned it. Do not use it after that environment is disposed, and never pass it to a different environment.
- **Thread safety.** Reads on `RadicleEnvironment`, `StringTables`, `Ba2Archive` and `AssetResolver` are safe from several threads at once. `PluginFile` is not: use each plugin from one thread at a time. `NifFile` and `DdsImage` hold no native handle at all - `FromBytes` copies every field into managed objects and destroys the handle before it returns - so their instances are immutable and safe to share, and there is nothing to dispose. The underlying C ABI has the same contract: a `radicle_nif` handle's read-only accessors are safe from several threads, while `radicle_nif_destroy` must not race another call on that handle (`docs/NIF.md`, `docs/TEXTURES.md`).
