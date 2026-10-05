# Stored grass and vegetation fields

The compatibility layouts come from TES5Edit/TES5Edit commit
`93cc0bc5a1251936c3c7859eee3150eda12a62d7`, `Core/wbDefinitionsFO4.pas`:
GRAS 9612–9644, LTEX 9967–9977, TREE 12134–12165, FLOR 12167–12197.
GRAS DATA is 32 bytes: density/min/max slope bytes, one unused byte,
u16 distance from water, two unused bytes, u32 water-distance discriminator,
four f32 placement/color/wave values and one flag byte plus three unused bytes.
The [verified loader record](https://re.nomadicinteractive.dev/ReverseEng/Fallout4_Runtime_Research/src/commit/6916afbcbec8ab09ecc0dfd30661502d0064f4f7/RE-Work/subsystems/radicle-re-garden-tools/vegetation-403-342/README.md)
and local copy `../radicle-341-re-followup-20261005/build/RE-Work/6916afb/vegetation-403-342/README.md`
stamp `TESGrass::Load` at OG RVA 0x304DB0. Present DATA is copied into a
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
Coast includes one base-master grass link; Nuka includes three. The remaining
links target grass in the same master, corroborating the schema association.
Runtime consumer semantics remain tracked in RE issue #403.

The newer loader record confirms the disk grammar and LTEX association,
including the TREE 0x30-byte size test in OG, AE and NG decompile records.
Its sections 5 and 8 leave TREE CNAM, PFPC and FLOR FNAM runtime consumers
unresolved. Those stored fields remain numeric and unchanged.

`test_vegetation_real.cpp` visits all four record families in each official
master through native and C ABI getters and checks unchanged whole-file byte
identity. It skips without FO4_DATA_DIR. Local and CI validation are recorded
separately for each published head.

GardenEngine main `10ea81f1590b06dad962ff86721c0a55f00ef173` consumes LAND/LTEX
diffuse textures through `CellService` and `LandscapeTextureCatalog`, but has no
LTEX GNAM or GRAS/TREE/FLOR vegetation consumer. GardenEngine #148 must add that
consumer before a meaningful vegetation probe can run.
