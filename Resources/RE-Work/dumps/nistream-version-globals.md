# CK NiStream version globals: mutation census

This note answers whether Creation Kit 1.11.137.0 changes the NIF version
selected by the three CK-side globals used by the NiStream header reader/writer.
The image base is `0x140000000`.

## 1. Global identities and image values

| VA | value in `CreationKit.exe` | role |
|---:|---:|---|
| `0x143c56a7c` | `0x04020100` (`4.2.1.0`) | minimum accepted NIF file version |
| `0x143c56a80` | `0x14020007` (`20.2.0.7`) | maximum accepted version **and emitted file-version u32** |
| `0x143c56a84` | `0x0000000c` (`12`) | maximum accepted/emitted user-defined version |

All three are in the writable initialized `.data` section. Their writable section
placement does not, by itself, imply a CK feature writes them: the complete CK
`.text` reference census below finds only loads, comparisons, or address-taking.

## 2. Every direct `.text` reference

The complete disassembly was searched for the resolved addresses. There are eight
direct references to the three exact globals.

### Stream constructor/default-copy path

At `0x142496ef3` and `0x142496eff`, the stream setup code performs:

```asm
mov eax, DWORD PTR [rip+...]        ; 0x143c56a80
mov [rsi+0x200], eax                ; per-stream file version
mov eax, DWORD PTR [rip+...]        ; 0x143c56a84
mov [rsi+0x204], eax                ; per-stream user version
```

These are **reads from** the globals followed by writes to the NiStream instance,
not writes to the globals. The globals initialize per-stream state; a caller can
therefore alter a stream object's copy only after construction, without changing
the global defaults.

### Load/version-validation path (`FUN_142498a30`)

The vftable slot-12 body reads the file version into the stream's `+0x200` field
and then compares it as follows:

| instruction | operation |
|---:|---|
| `0x142498acd` | reject if file version `< [0x143c56a7c]` |
| `0x142498ae8` | reject if file version `> [0x143c56a80]` |
| `0x142498bcf` | reject if the loaded user-defined version `> [0x143c56a84]` |

This path only consumes the globals as validation limits. Other branches normalize
legacy BS-version values in the stream object's fields (`+0x204`); none writes a
version-global.

### Save-header path (`FUN_142498e10`)

The vftable slot-13 body writes the header through the stream output object's
vftable slot `+0x30`:

| instruction | operation |
|---:|---|
| `0x142498e39` | passes the address of `0x143c56a80` with length 4; this emits the file-version u32 |
| `0x142498e7f` | passes the address of `0x143c56a84` with length 4; this emits user version 12 |
| `0x142498ebc` | reads `0x143c56a84` to decide whether the BS-version/header extension is emitted |

The textual line is not variable: the same function writes the literal
`Gamebryo File Format, Version 20.2.0.7\n` from `0x14341a510`. Consequently,
externally changing the max-version global would produce a deliberately
inconsistent file: the text would still say `20.2.0.7`, while the binary u32
would use the patched value.

## 3. Mutation result

There is **no direct mutation path** for `0x143c56a7c`, `0x143c56a80`, or
`0x143c56a84` in the Creation Kit's executable code**:

- no `mov`/`stos`/atomic write targets any of the three addresses;
- no pointer to any of the three addresses is embedded elsewhere in the image;
- the only address-taking instructions are the two save-header `lea`s listed
  above;
- the static image values are the only initialization observed.

The `.data` permissions mean a debugger, DLL injection, binary patch, or memory
corruption could change them. That is an external intervention, not a normal CK
settings or feature path.

## 4. Does a CK feature select another emitted NIF version?

No, not in this build. FaceGen, PreCombined/SCOL, `_OC`, `_Physics`, and other
NIF-producing paths all converge on the common NiStream save wrapper:

```text
NiStream vftable 0x142fa4780
  slot 6 stub 0x140097e92 -> body 0x1425fe750
  slot 13 stub 0x14000da62 -> FUN_142498e10 (SaveHeader)
```

No feature-specific writer supplies a version argument to `SaveHeader`, and no
CreationKit.ini setting is read by the version-header path. Per-file values that
can legitimately vary are `numRoots`, block-type/RTTI tables, object sizes, fixed
strings, links, and the stream's BS-version field. They do **not** change the NIF
file-version field, which remains `0x14020007` / `20.2.0.7` under normal execution.

The minimum-version global affects only loading. The max-version global is used
both as the load upper bound and as the emitted u32. The max-user-version global
controls the user-version field and whether the later BS-version/header portion
is written, but is also not feature-selected.

## Evidence

- `ghidra-dump/nistream-savecore-0x142498a30.asm`
- complete `CreationKit.exe` `.text` disassembly reference census
- `ghidra-dump/elric-pdb-symbols.txt` (local) §4b–§4e
- `ghidra-dump/decompile-SCOL-combined-mesh-writer.md §4`
- `ghidra-dump/nifwriter-facegen-0x140ab8a60.asm`
