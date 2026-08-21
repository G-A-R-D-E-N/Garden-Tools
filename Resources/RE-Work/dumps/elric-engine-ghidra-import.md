# Elric engine-layer Ghidra project

## Project

A persistent Ghidra 12.1 project was created at:

```text
/run/media/user/Games-Storage/Projects/ElricGhidra/ElricEngine
```

The original working path contains an ampersand (`Tools & Reverse Engineering`),
which Ghidra rejects as an invalid project-path character, so the project lives
beside that tree rather than inside it. The import logs are:

- `ElricGhidra/import-engine.log`
- `ElricGhidra/import-support-engine.log`

All imported images are PE32/i386 and use image base `0x10000000`.

## Imported programs

| program | role | PDB result |
|---|---|---|
| `ElricInterop.dll` | managed/native Elric interop and the PDB symbol motherlode | `ElricInterop.pdb` loaded by Ghidra's PDB Universal analyzer |
| `NiMain.dll` | Gamebryo/NiStream core | no matching PDB present; PE exports and RTTI analyzed |
| `BSCore.dll` | Bethesda core/container layer | no matching PDB present; PE exports and RTTI analyzed |
| `BSResource.dll` | resource/archive/stream layer | no matching PDB present; PE exports and RTTI analyzed |
| `BSGraphics.dll` | renderer/geometry graphics layer | no matching PDB present; PE exports and RTTI analyzed |
| `BSMain.dll` | Bethesda main engine layer | no matching PDB present; PE exports and RTTI analyzed |
| `BSSystem.dll` | Bethesda system layer | no matching PDB present; PE exports and RTTI analyzed |
| `BSShader.dll` | shader/material layer | no matching PDB present; PE exports and RTTI analyzed |
| `NiSystem.dll` | NiSystem I/O/platform layer | no matching PDB present; PE exports and RTTI analyzed |
| `NiAnimation.dll` | animation/Ni animation layer | no matching PDB present; PE exports and RTTI analyzed |
| `NiParticle.dll` | particle layer | no matching PDB present; PE exports and RTTI analyzed |
| `NiOptimization.dll` | geometry/mesh optimization layer | no matching PDB present; PE exports and RTTI analyzed |
| `NiCollision.dll` | collision/Havok bridge layer | no matching PDB present; PE exports and RTTI analyzed |

Ghidra also resolved the DLL dependencies against the imported programs instead
of leaving them only as cached external-library export stubs. Windows runtime
DLLs remain unresolved external libraries, as expected on this Linux host.

## PDB inventory

The Elric download contains only:

```text
ElricInterop.pdb
ElricUtilities.pdb
Elrich.pdb
```

There is no `NiMain.pdb`, `BSCore.pdb`, `BSResource.pdb`, `BSGraphics.pdb`, or
matching PDB for the additional native engine DLLs. `ElricUtilities.pdb` and
`Elrich.pdb` belong to the Elric utility/executable side and were not assigned
to these native engine images.

The PDB analyzer successfully parsed `ElricInterop.pdb`; its large C++ symbol
set includes the previously used `SettingT`, Havok, NiObject, NiNode,
`bhkNiTriStripsShape`, and NiStream method names. Ghidra reported some expected
C++ composite-layout alignment warnings, but the symbol import completed.

## Coverage verification

`ghidra-dump/EngineCoverage.java` reports the following after analysis:

| program | functions | named functions | symbols | SaveBinary matches | RegisterStreamables | LinkObject |
|---|---:|---:|---:|---:|---:|---:|
| `ElricInterop.dll` | 75,799 | 75,772 | 367,392 | 288 | 106 | 122 |
| `NiMain.dll` | 8,019 | 3,501 | 30,477 | 243 | 192 | 210 |
| `BSCore.dll` | 2,039 | 777 | 7,116 | 0 | 0 | 0 |
| `BSResource.dll` | 2,511 | 886 | 8,771 | 0 | 0 | 0 |
| `BSGraphics.dll` | 2,063 | 876 | 9,967 | 24 | 24 | 33 |
| `BSMain.dll` | 9,154 | 3,452 | 32,856 | 160 | 147 | 165 |
| `BSSystem.dll` | 3,112 | 1,160 | 11,314 | 0 | 0 | 0 |
| `BSShader.dll` | 11,212 | 4,229 | 44,497 | 147 | 111 | 129 |
| `NiSystem.dll` | 640 | 312 | 3,193 | 0 | 0 | 0 |
| `NiAnimation.dll` | 6,696 | 3,134 | 27,582 | 340 | 251 | 257 |
| `NiParticle.dll` | 3,671 | 1,865 | 14,860 | 227 | 220 | 241 |
| `NiOptimization.dll` | 1,300 | 467 | 5,859 | 27 | 27 | 36 |
| `NiCollision.dll` | 1,631 | 776 | 7,368 | 45 | 5 | 8 |

The `NiMain.dll` address-accurate export symbols include the full
`SaveBinary`/`RegisterStreamables`/`LinkObject` families used by the NIF
reverse-engineering notes. For example, Ghidra resolves the exported
`NiNode::SaveBinary`, `NiTriShape::SaveBinary`, `NiTriShapeData::SaveBinary`,
`NiNode::RegisterStreamables`, and `NiNode::LinkObject` methods at their PE
RVAs rather than leaving them as `FUN_` names.

## Reproduction

```bash
GH=".../ReverseEngineering/Ghidra/ghidra_12.1_PUBLIC/support/analyzeHeadless"
PROJECT="/run/media/user/Games-Storage/Projects/ElricGhidra"
SCRIPTS=".../CKRE/ghidra-dump"

"$GH" "$PROJECT" ElricEngine \
  -process NiMain.dll -noanalysis \
  -scriptPath "$SCRIPTS" \
  -postScript EngineCoverage.java SaveBinary RegisterStreamables LinkObject
```

The same command works for each imported program in the coverage table.
