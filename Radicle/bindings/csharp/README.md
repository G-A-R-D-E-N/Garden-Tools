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

## Regenerating

After changing a public C header, run:

```
python3 Radicle/bindings/csharp/tools/generate_interop.py Radicle/include \
  Radicle/bindings/csharp/Radicle.Interop/RadicleNative.g.cs \
  Radicle/bindings/csharp/tests/abi_layout_probe.cpp
```

## Tests

The CTest `radicle_csharp_interop_up_to_date` fails when the committed generated files no longer match the headers. It needs Python 3.

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
