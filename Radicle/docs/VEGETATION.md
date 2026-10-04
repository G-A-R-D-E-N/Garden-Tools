# Stored grass and vegetation fields

The compatibility layouts come from TES5Edit/TES5Edit commit
`93cc0bc5a1251936c3c7859eee3150eda12a62d7`, `Core/wbDefinitionsFO4.pas`:
GRAS 9612–9644, LTEX 9967–9977, TREE 12134–12165, FLOR 12167–12197.
GRAS DATA is 32 bytes: density/min/max slope bytes, one unused byte,
u16 distance from water, two unused bytes, u32 water-distance discriminator,
four f32 placement/color/wave values and one flag byte plus three unused bytes.
These are stored values, with no defaults, runtime ranges or placement algorithm
inferred. Flags and the water discriminator remain numeric.

LTEX GNAM is a repeated individual four-byte GRAS link, not one packed array.
TREE CNAM stores twelve f32 wind values in schema order. TREE and FLOR share
PFIG ingredient and SNAM harvest-sound links, plus four PFPC seasonal bytes.
FLOR FNAM is a raw u16 flag word. Model and object bounds reuse the existing
`Environment::metadata`, C `radicle_record_get_info` and C# `Info` surface;
the new getters validate present MODL/OBND fields before reading other values.
Unknown fields are preserved by the lossless container and never interpreted.

The independent installed-data size survey on 2026-10-04 found 107 GRAS
(82 base/10 Coast/15 Nuka), one TREE in the base master, 64 FLOR
(53 base/8 Coast/1 workshop03/2 Nuka) and 128 LTEX (105 base/8 Coast/15 Nuka).
Every GRAS DATA was 32 bytes, TREE CNAM was 48, all PFPC were four bytes,
FLOR FNAM two bytes, and all OBND twelve bytes. LTEX had 153 GNAM occurrences
(122 base/9 Coast/22 Nuka), each four bytes. The other three masters contain
none of these record families. These counts establish stored shapes, not
successful acceptance of the new decoder.

All 153 GNAM links resolve through their owning plugins' master tables to
winning GRAS records (zero null, unresolved or wrong-signature targets).
Coast includes one base-master grass link; Nuka includes three. The remaining
links target grass in the same master, corroborating the schema association.
Runtime consumer semantics remain tracked in RE issue #403.

The private runtime evidence index at commit
`f18bee7912b793b3cd3ed8d47c6f9a45f4d01b3a`, section a-342, verifies CK tag
inventories and a matching grass in-memory field family but explicitly lacks
the GRAS disk and TREE/FLOR loader byte grammar. Its LTEX grass-list association
is marked inferred. The pinned schema and disk survey supply compatibility
evidence; runtime placement/mixing/seasonal semantics still need confirmation.

`test_vegetation_real.cpp` visits all four record families in each official
master through native and C ABI getters and checks unchanged whole-file byte
identity. It skips without FO4_DATA_DIR. Builds and test execution use CI.

GardenEngine main `10ea81f1590b06dad962ff86721c0a55f00ef173` consumes LAND/LTEX
diffuse textures through `CellService` and `LandscapeTextureCatalog`, but has no
LTEX GNAM or GRAS/TREE/FLOR vegetation consumer. GardenEngine #148 must add that
consumer before a meaningful vegetation probe can run.
