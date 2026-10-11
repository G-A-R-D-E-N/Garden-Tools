# Vegetation reader integration

## Radicle/include/fo4/c_api.h:RADICLE_ABI_VERSION

The #404 integration merges main `c50ad4d5405e1571113019654b10e3d4a05ca0f1` into
published head `890158cc21dd2626eeda36740ca9fa68274ba913` without rewriting it.
Main is ABI 11; the five vegetation exports are additive at ABI 12. Preserve all
main declarations, append `fo4/c_api_vegetation.h` to the generator's header list,
and regenerate the managed declarations and native layout probe. The CMake
source union retains both `c_api_fields.cpp` and `c_api_vegetation.cpp`.
`Radicle/tests/test_c_api_quest.cpp:AbiVersionIsTwelve` pins the native value and
header together; the generated managed constant is 12 too.

## Radicle/src/environment/vegetation.cpp:decode_grass / decode_tree / decode_flora / decode_landscape_grasses

The audit uses TES5Edit pin `93cc0bc5a1251936c3c7859eee3150eda12a62d7`,
`Core/wbDefinitionsFO4.pas`: GRAS 9612-9644, LTEX 9967-9977, TREE 12134-12165,
FLOR 12167-12197. The existing scalar offsets, signedness, twelve stored TREE
floats, four seasonal bytes, raw FLOR u16 and individual repeated GNAM links
agree with those declarations. Private RE pin
`6916afbcbec8ab09ecc0dfd30661502d0064f4f7`,
`build/RE-Work/subsystems/radicle-re-garden-tools/vegetation-403-342/README.md`,
sections 3.1, 3.2, 4 and 5, verifies the loader grammar and the bounded zero-filled
GRAS prefix / exact-48-byte TREE CNAM behavior. It carries separate OG, AE and NG
provenance and address stamps. Retain those behaviors and preserve original
padding and surplus bytes; missing DATA remains distinct from a present empty
payload on this read surface. TREE wind effects, seasonal effects and FLOR flag
effects remain unresolved in that record; this integration implements no runtime
placement or rendering behavior.

## Radicle/src/c_api_vegetation.cpp:radicle_landscape_grasses_decode / Radicle/bindings/csharp/Radicle.Interop/Vegetation.cs:RadicleEnvironment

GardenEngine #148 depends on the vegetation reads; GARDEN-TOOLS #342 explicitly
requires a C ABI getter consumed by a GardenEngine probe. At GardenEngine main
`5cb5e18131a53e3c7c6fa049ce8fde6a9ad9f742`,
`opencw/native/radicle/CMakeLists.txt:RADICLE_COMMIT` still pins
`34d16d50de68e7da079745240fd247310ea730f6`, which predates these exports.
`opencw/OpenCommonwealth.csproj` references that pinned source's managed binding
project. No call to these vegetation getters or managed wrappers is present in
the tracked application sources; existing foliage HLOD work is a separate path.
The required GardenEngine vegetation probe and its Radicle pin update are missing.
Radicle native/C ABI and managed tests do not substitute for that consumer proof.
Report the missing integration under #148/#342 rather than expanding #404 into
GardenEngine rendering work or closing either issue.

## Radicle/tests/test_vegetation_real.cpp:EveryOfficialVegetationRecordDecodesAndMasterIsByteIdentical

The official-master regression compares each native decoded value with its C ABI
view, including optional presence and resolved harvest keys. Raw CNAM floats,
FNAM flags and PFPC bytes independently check the payload values. Float agreement
uses exact IEEE bit patterns so signed zero and NaN payloads are compared too.
Each LTEX GNAM retains its physical order and must be non-null and resolve to GRAS
in both environments. Load the selected file's declared masters, in declaration
order, before that file so cross-master links can resolve; enumerate and serialize only the
selected master. Keep the family totals and byte-identity assertions, and include
record keys and raw subrecord bytes in failure traces. No decoder behavior changes.
Brace conditionals containing GoogleTest assertion macros so GCC's
`-Werror=dangling-else` check accepts the same assertions as MSVC.
The GRAS DATA raw offsets (density 0, min slope 1, max slope 2, units from water u16 at 4, water type u32 at 8, floats at 12/16/20/24, flags 28; bytes past a short payload read as zero) match `source:xEdit Core/wbDefinitionsFO4.pas:9616-9642 @93cc0bc5` (`wbStruct(DATA` for GRAS).

## Radicle/tests/test_c_api_quest.cpp:findProperty

Restore the blank line after `hasRow` removed by merge conflict resolution; the
diff against main then contains only the ABI 12 test update in this file.

## Radicle/src/environment/vegetation.cpp:decode_landscape_grasses

A NULL LTEX GNAM (raw form ID 0) is returned as an empty key, not as an error. `Radicle/src/formid.cpp:resolve_plugin_form_id` maps a null raw ID to an empty `FormKey`, and `Reader::link_at` in `Radicle/src/environment/record_reader.hpp` takes that path, the same as the other Radicle decoders. xEdit flags it: `source:xEdit Core/wbDefinitionsFO4.pas:9976 @93cc0bc5` declares GNAM as `wbFormIDCk(GNAM, 'Grass', [GRAS])` with no NULL allowed, and `source:xEdit Core/wbInterface.pas:TwbFormIDChecked.Check @93cc0bc5` (line 19359, IsNull branch) reports "Found a NULL reference". The official-master test asserts non-null links, so this case is not covered for other files.

## Radicle/src/environment/vegetation.cpp:decode_tree

A TREE CNAM whose length is not 48 bytes is treated the same as an absent CNAM: `wind` stays unset and no error is raised, so the rest of the record still decodes. The only difference is in the record container: a wrong-size CNAM is still a subrecord and its bytes stay untouched there, while an absent CNAM has no subrecord at all.
