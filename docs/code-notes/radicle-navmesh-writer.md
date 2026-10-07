# Stored navmesh authoring

`Radicle/src/plugin/navmesh_writer.cpp:set_navmesh_vertex`, `set_navmesh_triangle` and `set_navmesh_door` patch existing counted elements after `navmesh_mixed_formid_offsets` validates the complete payload. Source: pinned xEdit 93cc0bc5 `wbDefinitionsFO4.pas:wbNVNM` and the existing decoder. Geometry version 15 and form version 128 or newer are required. Counts, indices outside existing arrays, unknown triangle traversal bytes, opaque tails and sibling subrecords are preserved or mutation is refused. No topology reconstruction or geometry creation is inferred.

`Radicle/include/fo4/c_api_navmesh.h` and `Radicle/bindings/csharp/Radicle.Interop/PluginFile.cs` expose the same indexed edits. C ABI input validation precedes mutation; door keys use the plugin's existing master context and validate local REFR targets.

`Radicle/schema/fallout4/fields.json:NAVM/NAVI` registers only fixed NAVI prefix fields and link slots after complete mixed-payload validation and unique NVER 15. The xEdit order is represented by coupled entries without guessed defaults. `Radicle/src/plugin/subrecord_authoring.cpp:create_with_rules` refuses non-top-level creation even with explicit rules. `Radicle/src/schema/preflight.cpp:preflight_authoring_record` validates NAVM temporary-cell-child placement. Counts, parent union and derived topology remain coupled.

`Radicle/tests/test_navmesh_authoring.cpp` checks typed serialization, idempotence, malformed/duplicate/legacy refusal and the public C ABI. `Radicle/bindings/csharp/Radicle.Interop.Tests/NavmeshTests.cs:TypedEditsReparseAndRefuseUnknownChanges` covers the managed writer-to-reader path. `Radicle/tests/test_navmesh_writer_real.cpp` checks official order and capped mutation with untouched-record and payload identity.

`Radicle/src/plugin/navmesh_writer.cpp:set_navmesh_triangle` permits only named triangle flags 0–6 and 9–11 from pinned xEdit `wbDefinitionsCommon.pas:wbNavmeshTriangleFlags`; unnamed bits and the entire uncertain cover-flags word must remain identical. `Radicle/tests/test_navmesh_writer_real.cpp` excludes retained `XXXX` length markers from logical xEdit order, as `Radicle/src/plugin/reader.cpp:parse_subrecords` retains them and `writer.cpp:write_subrecords` regenerates them.

`Radicle/tests/test_navmesh_writer_real.cpp` checks every untouched body record byte-for-byte. TES4 is checked by subrecord excluding ONAM, because `Radicle/src/plugin/writer.cpp:write_plugin` rebuilds that master override index for every dirty master. No topology counts or other TES4 payloads may change.
