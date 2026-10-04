# Water field evidence and remaining resolution work

`src/environment/base_records.cpp:decode_water` exposes WATR data through
`include/fo4/c_api_records.h:radicle_water_get_info` and the generated C#
`RadicleEnvironment.Water` getter. These landed in #381, ABI 4.

The field-layout evidence is TES5Edit/TES5Edit commit
`93cc0bc5a1251936c3c7859eee3150eda12a62d7`,
[`Core/wbDefinitionsFO4.pas`](https://github.com/TES5Edit/TES5Edit/blob/93cc0bc5a1251936c3c7859eee3150eda12a62d7/Core/wbDefinitionsFO4.pas#L12342).
This is a pinned compatibility oracle, not proof of runtime selection behavior.

| Subrecord | Layout evidence | Decoder surface |
|---|---|---|
| FNAM | lines 12346–12350, one byte | raw flags |
| DNAM | lines 12357–12422 | visual fields below |
| NAM0 / NAM1 | lines 12424–12425, three floats each | linear / angular velocity |
| NAM2 / NAM3 / NAM4 | lines 12426–12428, NUL-terminated paths | three noise textures |

DNAM stores Fog Depth Amount at byte 0; shallow/deep RGBA colors at 4/8;
six range/alpha floats at 12–32; underwater color at 36; underwater
amount/near/far at 40–48; normal, falloff, reflectivity, Fresnel, surface
effect and displacement floats at 52–92; reflection color at 96; sun
specular/sparkle and interior specular floats at 100–124. Colors expose RGB;
the original fourth byte remains in the lossless container.

The noise values are grouped by property, not stored as three structures:
three wind-direction floats at 128–136, speeds at 140–148, amplitude scales
at 152–160, UV scales at 164–172, and falloffs at 176–184. The optional tail
is silt amount at 188, light/dark RGBA colors at 192/196, and one reflection
byte at 200. The decoder requires 188 bytes and rejects data beyond 201;
unmodeled subrecords and untouched bytes stay in the container.

`tests/test_water_real.cpp:WaterRealData` decodes every WATR version in each
of the seven official masters, records counts and DNAM sizes, exercises the
C ABI getter, and compares the complete untouched serialized master with
its original bytes. It uses installed `FO4_DATA_DIR`; absent data or a missing
named master skips cleanly. No game bytes are checked in.

The 2026-10-04 installed corpus has 47 WATR versions: Fallout4.esm 42,
DLCCoast.esm 2, DLCNukaWorld.esm 3, and zero in the other four masters.
Two DNAMs are 188 bytes and 45 are 201 bytes. All 14 parameterized native
and C ABI checks passed, including untouched byte identity for all seven
masters. With `FO4_DATA_DIR` unset, all 14 skipped cleanly.

## Cell/worldspace resolution

The same pinned FO4 source defines CELL XCLW as a float (lines 5789–5790)
and XCWT as a WATR link (5809). WRLD PNAM bit 3 is named Use Water Data
(12675–12686), and NAM2 is its WATR link (12692–12694). The pinned
[`Core/wbDefinitionsCommon.pas`](https://github.com/TES5Edit/TES5Edit/blob/93cc0bc5a1251936c3c7859eee3150eda12a62d7/Core/wbDefinitionsCommon.pas#L9121)
defines NAM3/NAM4 LOD water and DNAM default land/water heights at 9121–9131.
Radicle exposes these raw links/heights and load-order winners already.

Runtime precedence, multi-parent inheritance, null/default fallback and the
requested finite XCLW sentinel `-2147483648.0` remain **unverified**. They are
tracked in [RE #395](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/395).
No guessed effective-water resolver or automatic sentinel conversion is added.

GardenEngine main `10ea81f1590b06dad962ff86721c0a55f00ef173` has the consumer
`opencw/Services/WaterBuild/WaterBuilder.cs:Build`, `ReadWorld`, and `Material`,
and runtime probe `opencw/tools/probes/WaterProbe.cs` with scene
`opencw/tools/water/water_probe.tscn`. It is pinned to Radicle
`2360e88138aa75c60db78ab579a3d2d7fb81d142`. Its
`WaterRules.IsDefaultHeight` recognizes missing/nonfinite or magnitude at
least `1e30`; it currently misses the finite sentinel requested in #341.
That consumer policy is not runtime evidence. Probe acceptance of a verified
effective-water resolver remains outstanding.
