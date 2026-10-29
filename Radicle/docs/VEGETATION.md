# Stored grass and vegetation fields

The compatibility layouts come from TES5Edit/TES5Edit commit
`93cc0bc5a1251936c3c7859eee3150eda12a62d7`, `Core/wbDefinitionsFO4.pas`:
GRAS 9612–9644, LTEX 9967–9977, TREE 12134–12165, FLOR 12167–12197.
GRAS DATA is 32 bytes: density/min/max slope bytes, one unused byte,
u16 distance from water, two unused bytes, u32 water-distance discriminator,
four f32 placement/color/wave values and one flag byte plus three unused bytes.
The [verified loader record](https://re.nomadicinteractive.dev/ReverseEng/Fallout4_Runtime_Research/src/commit/6916afbcbec8ab09ecc0dfd30661502d0064f4f7/RE-Work/subsystems/radicle-re-garden-tools/vegetation-403-342/README.md)
stamps `TESGrass::Load` at OG address 0x31AA00. Present DATA is copied into a
zero-filled 32-byte prefix, including short or oversized fields. The read
surface keeps absent DATA distinct from present zero-length DATA. Untouched
original bytes, including surplus bytes and padding, remain in the container.
Flags and the water discriminator remain numeric; placement is not simulated.

LTEX GNAM is a repeated individual four-byte GRAS link, not one packed array.
TREE CNAM exposes twelve f32 values only at exactly 48 bytes; other sizes
stay opaque, matching the verified loader size check. Their runtime consumer
meaning remains unresolved. TREE and FLOR share
PFIG ingredient and SNAM harvest-sound links, plus four PFPC seasonal bytes.
FLOR FNAM is a raw u16 flag word. Model and object bounds reuse the existing
`Environment::metadata`, C `radicle_record_get_info` and C# `Info` surface;
the new getters validate present MODL/OBND fields before reading other values.
Unknown fields are preserved by the lossless container and never interpreted.

The historical installed-data inventory recorded on 2026-10-04 lists 107 GRAS
(82 base/10 Coast/15 Nuka), one TREE in the base master, 64 FLOR
(53 base/8 Coast/1 workshop03/2 Nuka) and 128 LTEX (105 base/8 Coast/15 Nuka).
Every GRAS DATA was 32 bytes, TREE CNAM was 48, all PFPC were four bytes,
FLOR FNAM two bytes, and all OBND twelve bytes. LTEX had 153 GNAM occurrences
(122 base/9 Coast/22 Nuka), each four bytes. The other three masters contain
none of these record families. These counts establish stored shapes, not
successful acceptance of the new decoder. This inventory predates the linked
RE record; that record cites issue counts rather than repeating the census.

All 153 GNAM links resolve through their owning plugins' master tables to
winning GRAS records (zero null, unresolved or wrong-signature targets).
The decoders resolve these links but do not check the target's signature. xEdit flags a
wrong-signature target as a warning (`wbFormIDCk` with `[GRAS]` at `Core/wbDefinitionsFO4.pas:9976`,
`93cc0bc5`); the official data above has none, so the two agree there.
Coast includes one base-master grass link; Nuka includes three. The remaining
links target grass in the same master, corroborating the schema association.
Runtime consumer semantics remain tracked as RE-013 on #520.

The newer loader record confirms the disk grammar and LTEX association,
including the TREE 0x30-byte size test in OG, AE and NG decompile records.
Its sections 5 and 8 leave TREE CNAM, PFPC and FLOR FNAM runtime consumers
unresolved. Those stored fields remain numeric and unchanged.

Stored-read rules on the 1.10.163 executable, with AE and NG compared where the
record notes it:
- LTEX GNAM is the landscape-texture to grass link. The game loader confirms it
  and uses it at runtime. The loader keeps an unbounded list of form IDs and skips
  zero entries. `landscape_grasses_runtime_view` drops null links from the stored
  list and keeps the rest in file order.
- GRAS DATA is read as up to 32 raw bytes into a zeroed block, with no clamping.
- Water mode above 5 means no water filter. `GrassData::water_filter_active` is
  true for modes 0 through 5 only.
- TREE CNAM is read only at exactly 48 bytes.

The runtime view applies no slope, density or placement logic. Placement and
the runtime effect of these fields remain RE-013 on #520.

`test_vegetation_real.cpp` visits all four record families in each official
master through native and C ABI getters and checks unchanged whole-file byte
identity. It skips without FO4_DATA_DIR. Local and CI validation are recorded
separately for each published head.

GardenEngine main `5cb5e18131a53e3c7c6fa049ce8fde6a9ad9f742` pins Radicle
`34d16d50de68e7da079745240fd247310ea730f6`, before these getters, and has no
LTEX GNAM or GRAS/TREE/FLOR getter consumer. The probe source for GARDEN-TOOLS
#342 is in `Radicle/probes/gardenengine/`; it has not been run. It cannot run
until GardenEngine #148 adds that consumer and moves its Radicle pin. Existing foliage HLOD support does not establish that grass-reader proof.
The audit and ABI 26 integration are recorded in
`docs/code-notes/radicle-vegetation.md` at the repository root.

## GardenEngine consumer probe

Copy `Radicle/probes/gardenengine/VegetationProbe.cs` and
`Radicle/probes/gardenengine/vegetation_probe.tscn` into `opencw/tools/` of a
GardenEngine checkout. Build `opencw/OpenCommonwealth.csproj` with
`RadicleSourceDir` set to this Radicle checkout. Set `FO4_DATA_DIR` to the
installed data folder and `RADICLE_NATIVE_PATH` to the built library of the
matching ABI, then run `--headless --path opencw tools/vegetation_probe.tscn`.

Exit codes: 0 means `vegetation_probe PASS` with a one-line JSON summary;
77 means `vegetation_probe SKIP` because `FO4_DATA_DIR` is unset; 1 means
`vegetation_probe FAIL` on stderr with the exception text.

The probe loads only `Fallout4.esm` and asserts:
- the first GRAS winner with DATA decodes through `RadicleEnvironment.Grass`;
- the base-master TREE decodes through `Tree` with exactly twelve CNAM wind
  values, matching the 48-byte CNAM inventory above;
- the first FLOR winner with FNAM decodes through `Flora` with non-null flags;
- the first LTEX winner with GNAM links decodes through `LandscapeGrasses`,
  and every link is non-null, resolves through `Winner`, and is a GRAS.

No vanilla EDIDs for these signatures are cited in this repository, so the probe
walks `WinnersOf` and prints the EDIDs it finds in the PASS line instead of
hard-coding names.

Not yet run; needs a GardenEngine checkout whose Radicle pin is this PR head (GardenEngine pins ABI 9 at 34d16d50).

## Editing

| Family | READ | WRITE | ADD | REMOVE | CREATE | REMAP |
| --- | --- | --- | --- | --- | --- | --- |
| GRAS DATA scalars (10) | `decode_grass`, `radicle_grass_get_info`, `Grass` | `SetScalar` (`GRAS\Density` .. `GRAS\Flags`, prefix semantics) | DATA with the 32-byte xEdit default | refused (required) | GRAS | none (no links) |
| TREE CNAM wind (12) | `decode_tree` | `SetScalar` (`TREE\TrunkFlexibility` .. `LeafFrequency`, 48-byte CNAM only) | CNAM default | refused (required) | TREE | none |
| TREE/FLOR PFPC seasons (4 each) | harvest seasons | `SetScalar` (`TREE\Spring` .. `FLOR\Winter`, 4-byte PFPC only) | zero default | TREE yes, FLOR refused (required) | FLOR (required), TREE optional | none |
| FLOR FNAM flags | `decode_flora` | `SetScalar` `FLOR\Flags` (2-byte FNAM only) | zero default | yes | optional, not created by default | none |
| TREE PFIG, SNAM | `HarvestData` ingredient, sound | `SetFormLink` `TREE\Ingredient` (base objects plus ENCH, nullable), `TREE\HarvestSound` (SNDR, nullable) | null default | yes | optional, not created by default | PFIG and SNAM payload remap |
| FLOR PFIG, SNAM | same | `FLOR\Ingredient` (ALCH, AMMO, ARMO, BOOK, INGR, KEYM, LIGH, LVLI, MISC, NOTE, WEAP, nullable), `FLOR\HarvestSound` (SNDR, not nullable) | PFIG null default, SNAM no default | yes | optional, not created by default | same |
| LTEX GNAM | `decode_landscape_grasses`, `LandscapeGrasses` | `FormLinkListSet` `LTEX\Grasses` index n (GRAS, not nullable), `FormLinkListCount` | `FormLinkListInsert` `LTEX\Grasses` with a GRAS target (`AddSubrecord GNAM` is refused: no default) | `RemoveSubrecord GNAM n` or `FormLinkListRemove` | optional | GNAM payload remap |
| LTEX required MNAM, HNAM, SNAM | n/a | n/a | defaults null, 30/30, 30 | refused (required) | LTEX | none |
| Records | n/a | n/a | n/a | n/a | GRAS, TREE, LTEX, FLOR (non-localized plugins) via `radicle_plugin_create_record` / `PluginFile.CreateRecord` | n/a |

Stays opaque: MODL/MODT/MODC/MODS/MODF (MODL cannot be removed: it would orphan its siblings), VMAD,
DEST and its stage subrecords, DAMC, ATTX, RNAM, FULL, KSIZ/KWDA (use `FLOR\Keywords`),
and every byte
of an official record that no edit touched. Four FLOR records in Fallout4.esm (FloraFern01/02,
FloraCornStalk01/02, two destructible stages) cannot take `AddSubrecord` because the flat order check does
not model repeating stage groups; their field edits and removals still work. LTEX TNAM, MNAM, HNAM and SNAM have schema rows (`LTEX\TextureSet`, `LTEX\MaterialType`, `LTEX\Friction`, `LTEX\Restitution`, `LTEX\SpecularExponent`) for existing subrecords only; there is no typed field. FLOR `PTRN` (`FLOR\PreviewTransform`, TRNS, not nullable) and `PNAM` (`FLOR\MarkerColorRed`, `MarkerColorGreen`, `MarkerColorBlue`, unused) have schema rows for existing subrecords only.

Field list, size guards, defaults provenance and refusal rules are in
`docs/code-notes/radicle-vegetation-writer.md`. Subrecord add, remove and create semantics are in
`docs/code-notes/radicle-subrecord-authoring.md`.
