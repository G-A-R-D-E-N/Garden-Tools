# VMAD array element decoding

`Radicle/src/schema/vmad.cpp:read_property` now decodes the elements of the five scalar array types instead of skipping them: object array (11), string array (12), int array (13), float array (14) and bool array (15). Each array's `u32` count is consumed and every element is read into the matching optional vector on `Radicle/include/fo4/schema/vmad.hpp:VmadProperty` (`object_elements`, `text_elements`, `integer_elements`, `real_elements`, `boolean_elements`). Exactly one of these is engaged, and only for its own array type; an empty array engages its vector as empty.

The byte layout mirrors the existing `Radicle/src/schema/vmad.cpp:walk_property` walker, so the typed reader and the offset scanner consume identical bytes. Object elements go through the new `Radicle/src/schema/vmad.cpp:read_object_element` helper, which is also used by the scalar object case, so an unexposed nonzero unused word throws `UnsupportedVmadProperty` for array elements exactly as it does for scalars. Float elements go through `Radicle/src/schema/vmad.cpp:read_float`, which is shared with the scalar float case, so values are reconstructed from the raw bit pattern and `-0.0` keeps its sign bit.

Struct (7) and struct array (17) properties remain count-only: `element_count` is set and member properties are walked with `walk_properties` but not exposed. Variable (6) and variable array (16) still throw `UnsupportedVmadProperty`. `VmadProperty::element_count` is still set for every array and struct type, as before. The `*_elements` fields for struct and struct array stay disengaged.

No C ABI, `RADICLE_ABI_VERSION`, environment, placed-record, or binding code changed. The header additions are the `VmadObjectElement` struct and the five optional vector fields only.

Tests: `Radicle/tests/test_vmad.cpp` adds ten `VmadArrayElements.*` cases. Before the reader change, nine failed (optional array fields were disengaged) and the struct case passed, since struct behavior is unchanged. After the change, all ten pass. The full `fo4_tests` run reports 824 passed, 38 skipped (pre-existing optional-data skips) and 0 failed out of 862.
