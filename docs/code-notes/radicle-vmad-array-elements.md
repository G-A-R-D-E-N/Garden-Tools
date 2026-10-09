# VMAD array element decoding

`Radicle/src/schema/vmad.cpp:read_property` now decodes the elements of the five scalar array types instead of skipping them: object array (11), string array (12), int array (13), float array (14) and bool array (15). Each array's `u32` count is consumed and every element is read into the matching optional vector on `Radicle/include/fo4/schema/vmad.hpp:VmadProperty` (`object_elements`, `text_elements`, `integer_elements`, `real_elements`, `boolean_elements`). Exactly one of these is engaged, and only for its own array type; an empty array engages its vector as empty.

The byte layout mirrors the existing `Radicle/src/schema/vmad.cpp:walk_property` walker, so the typed reader and the offset scanner consume identical bytes. Object elements go through the new `Radicle/src/schema/vmad.cpp:read_object_element` helper, which is also used by the scalar object case, so an unexposed nonzero unused word throws `UnsupportedVmadProperty` for array elements exactly as it does for scalars. Float elements go through `Radicle/src/schema/vmad.cpp:read_float`, which is shared with the scalar float case, so values are reconstructed from the raw bit pattern and `-0.0` keeps its sign bit.

Struct (7) and struct array (17) properties remain count-only: `element_count` is set and member properties are walked with `walk_properties` but not exposed. Variable (6) and variable array (16) still throw `UnsupportedVmadProperty`. `VmadProperty::element_count` is still set for every array and struct type, as before. The `*_elements` fields for struct and struct array stay disengaged.

No C ABI, `RADICLE_ABI_VERSION`, environment, placed-record, or binding code changed. The header additions are the `VmadObjectElement` struct and the five optional vector fields only.

Tests: `Radicle/tests/test_vmad.cpp` adds ten `VmadArrayElements.*` cases. Before the reader change, nine failed (optional array fields were disengaged) and the struct case passed, since struct behavior is unchanged. After the change, all ten pass. The full `fo4_tests` run reports 824 passed, 38 skipped (pre-existing optional-data skips) and 0 failed out of 862.

## Placed reference script elements

`Radicle/src/environment/placed.cpp:decode_scripts` resolves each object array element's `object` through `env_.resolve_link` into the new `Radicle/include/fo4/environment/placed.hpp:ScriptProperty::object_elements` (`std::optional<std::vector<FormKey>>`), the same resolution the scalar `object` uses. The string, int, float and bool elements are not copied into `ScriptProperty`; the C layer reads them from the raw `VmadProperty` vectors, as it already does for scalar text, integer, real and boolean.

`Radicle/src/c_api_placed.cpp:build_view` emits one `radicle_script_element` vector per script property (`PlacedHolder::elements`, one entry for every property). The outer vector is reserved to the total property count before the script loop, so an inner `data()` pointer stored in `radicle_script_property::elements` is never invalidated by a later `emplace_back`. Element text points into the raw property strings owned by `PlacedHolder::data`, the same storage the scalar `text` uses. String array elements go through the same windows-1252 to UTF-8 conversion as scalar text, in the first loop of `build_view`, so they match scalars.

`Radicle/include/fo4/c_api_env.h:radicle_script_element` has `object`, `alias`, `text`, `integer`, `real` and `boolean`; each array kind sets only its own member and the rest stay zero or null. `radicle_script_property` appends `has_elements`, `elements` and `element_value_count`. `has_elements` is 1 exactly when one of the five vectors is engaged, which is types 11 through 15. An empty array gives `has_elements` 1, `element_value_count` 0 and a possibly null `elements`. A struct (7) keeps `has_elements` 0 and `has_element_count` 1.

ABI: `Radicle/include/fo4/c_api.h:RADICLE_ABI_VERSION` is bumped from 11 to 12. The number is provisional: `radicle_script_property` grew three members, so the layout changed. `Radicle/tests/test_c_api_quest.cpp:AbiVersionIsTwelve` was the only test asserting the old number and now checks 12.

Bindings: `Radicle/bindings/csharp/tools/generate_interop.py` regenerated `Radicle/bindings/csharp/Radicle.Interop/RadicleNative.g.cs` and `Radicle/bindings/csharp/tests/abi_layout_probe.cpp`; nothing was edited by hand. The managed `Radicle/bindings/csharp/Radicle.Interop/PlacedReferences.cs:ScriptElement` record maps each element, and `Radicle/bindings/csharp/Radicle.Interop/PlacedReferences.cs:ScriptProperty` gets `Elements`, which is null whenever `HasElements` is 0. The managed `AbiGuardTests` load `radicle_abi_stub*` DLLs, so those targets must be rebuilt after a header bump; they are not built by default in the build directory.

Red-first counts. Before any population code, `Radicle/tests/test_environment_script_elements.cpp:CApiExposesDecodedScriptArrayElements` failed (`has_elements` 0 where 1 is expected, `element_value_count` 0 where 2 is expected). `Radicle/bindings/csharp/Radicle.Interop.Tests/PlacedScriptElementTests.cs:ExposesDecodedArrayElementsAndNullForScalars` failed on `Assert.NotNull` for `Elements`. After the implementation, the focused C++ test passes, the full `fo4_tests` run reports 863 tests, 825 passed, 38 skipped (pre-existing optional-data skips) and 0 failed, the managed suite passes 80 of 80, and `ctest -R "interop|abi"` passes 6 of 6 including `radicle_csharp_interop_up_to_date`.

## Unknown property types stop the typed reader

- `Radicle/src/schema/vmad.cpp:read_property`: an unknown property type (anything other than 0-5, 7, 11-15 and 17) throws `UnsupportedVmadProperty` instead of `parse_error`.
  - `read_vmad_scripts` therefore returns the properties already read with `unsupported = true`, the same as for types 6 and 16, instead of failing the whole record.
  - This matches an unmerged decompile of Fallout4.exe OG 1.10.163 (ReverseEng/Fallout4_Runtime_Research pull request #11, `vmad-variable-types-388`; GardenEngine issue #355). In it the per-property reader rejects types 8, 9, 10 and >= 18 before reading a value, and the failure ends that script's property loop while keeping the earlier properties. AE was not checked.
- `Radicle/src/schema/vmad.cpp:walk_property` (form-link scan and master remap) is unchanged. It still reports such a VMAD as failed, so `Radicle/tests/test_vmad_unsupported.cpp:UnknownPropertyTypeFailsClosedForMasterRemap` stays refused.
- An unknown type nested inside a struct still raises `parse_error` through `walk_property`.
- `Radicle/src/schema/vmad.cpp:read_quest_vmad_extras` still reports `unsupported`, but its reason is now the generic quest-extras text, not the parse message.
- Regression: `Radicle/tests/test_vmad_unsupported.cpp:UnknownPropertyTypesStopTheTypedReaderLikeVariables` covers types 8, 9, 10, 18 and 255.
  - It failed before the change with `plugin parse error at 0x31: unknown VMAD property type`.
  - After the change `fo4_tests` passed 827, skipped 38 (real-data and corpus tests) and failed 0 of 865.
