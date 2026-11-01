cmake_minimum_required(VERSION 3.25)

project(fo4sdk
  VERSION 0.1.0
  DESCRIPTION "Radicle: native Fallout 4 plugin SDK"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT TARGET ZLIB::ZLIB)
  find_package(ZLIB REQUIRED)
endif()

if(NOT TARGET nlohmann_json::nlohmann_json)
  include(FetchContent)
  FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
    INACTIVITY_TIMEOUT 60
  )
  FetchContent_MakeAvailable(nlohmann_json)
endif()

option(FO4_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(FO4_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(RADICLE_BUILD_TOOLS "Build Radicle command-line tools" ${PROJECT_IS_TOP_LEVEL})
option(RADICLE_BUILD_TESTS "Build Radicle tests" ${PROJECT_IS_TOP_LEVEL})
option(RADICLE_INSTALL "Generate Radicle install/export rules" ${PROJECT_IS_TOP_LEVEL})
option(RADICLE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ${PROJECT_IS_TOP_LEVEL})
option(RADICLE_SHARED "Build Radicle as one shared library that also carries the C ABI" OFF)

find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_custom_target(radicle_schema_check ALL
  COMMAND
    "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/schema_codegen.py"
    --schema "${CMAKE_CURRENT_SOURCE_DIR}/schema/fallout4/fields.json"
    --output "${CMAKE_CURRENT_SOURCE_DIR}/include/fo4/schema/generated_scalar_fields.inc"
    --check
  COMMENT "Checking generated Fallout 4 scalar field schema"
  VERBATIM
)
add_custom_target(radicle_record_schema_check ALL
  COMMAND
    "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/record_schema_codegen.py"
    --schema "${CMAKE_CURRENT_SOURCE_DIR}/schema/fallout4/fields.json"
    --output "${CMAKE_CURRENT_SOURCE_DIR}/include/fo4/schema/generated_record_constraints.inc"
    --check
  COMMENT "Checking generated Fallout 4 record authoring schema"
  VERBATIM
)
add_custom_target(radicle_lstring_schema_check ALL
  COMMAND
    "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/lstring_schema_codegen.py"
    --schema "${CMAKE_CURRENT_SOURCE_DIR}/schema/fallout4/fields.json"
    --output "${CMAKE_CURRENT_SOURCE_DIR}/include/fo4/schema/generated_lstring_fields.inc"
    --check
  COMMENT "Checking generated Fallout 4 localized field schema"
  VERBATIM
)
add_custom_target(radicle_formlink_schema_check ALL
  COMMAND
    "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/formlink_schema_codegen.py"
    --schema "${CMAKE_CURRENT_SOURCE_DIR}/schema/fallout4/fields.json"
    --output "${CMAKE_CURRENT_SOURCE_DIR}/include/fo4/schema/generated_formlink_lists.inc"
    --check
  COMMENT "Checking generated Fallout 4 FormLink list schema"
  VERBATIM
)
add_custom_target(radicle_condition_scope_schema_check ALL
  COMMAND
    "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/condition_scope_codegen.py"
    --schema "${CMAKE_CURRENT_SOURCE_DIR}/schema/fallout4/fields.json"
    --output "${CMAKE_CURRENT_SOURCE_DIR}/include/fo4/schema/generated_condition_scopes.inc"
    --check
  COMMENT "Checking generated Fallout 4 condition scope schema"
  VERBATIM
)
add_custom_target(radicle_condition_function_schema_check ALL
  COMMAND
    "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/condition_function_codegen.py"
    --schema "${CMAKE_CURRENT_SOURCE_DIR}/schema/fallout4/condition_functions.json"
    --output "${CMAKE_CURRENT_SOURCE_DIR}/include/fo4/schema/generated_condition_functions.inc"
    --ctda-output "${CMAKE_CURRENT_SOURCE_DIR}/include/fo4/schema/ctda_functions.inc"
    --check
  COMMENT "Checking generated Fallout 4 condition function table"
  VERBATIM
)
add_custom_target(radicle_hkx_layout_check ALL
  COMMAND
    "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/hkx_layout_codegen.py"
    --layouts "${CMAKE_CURRENT_SOURCE_DIR}/../BGS/src/Hkx/HavokClassLayouts.json"
    --types "${CMAKE_CURRENT_SOURCE_DIR}/../BGS/src/Hkx/HavokClassTypes.json"
    --output "${CMAKE_CURRENT_SOURCE_DIR}/src/hkx/hkx_layouts.generated.inc"
    --check
  COMMENT "Checking generated Havok class layout table"
  VERBATIM
)
add_custom_target(radicle_parity_matrix_check ALL
  COMMAND
    "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/parity_matrix.py"
    --check
  COMMENT "Checking generated Fallout 4 parity matrix"
  VERBATIM
)

function(fo4_apply_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive- $<$<BOOL:${RADICLE_WARNINGS_AS_ERRORS}>:/WX>)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic $<$<BOOL:${RADICLE_WARNINGS_AS_ERRORS}>:-Werror>)
  endif()
endfunction()

function(fo4_apply_sanitizers target)
  if(FO4_ENABLE_ASAN)
    target_compile_options(${target} PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE -fsanitize=address)
  endif()
  if(FO4_ENABLE_UBSAN)
    target_compile_options(${target} PRIVATE -fsanitize=undefined -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE -fsanitize=undefined)
  endif()
endfunction()

# RADICLE_SHARED builds one radicle.dll with the C++ API and the C ABI. Without
# it, BUILD_SHARED_LIBS still decides, as it did before the option existed.
if(RADICLE_SHARED)
  set(FO4_LIBRARY_KIND SHARED)
elseif(BUILD_SHARED_LIBS)
  set(FO4_LIBRARY_KIND SHARED)
else()
  set(FO4_LIBRARY_KIND STATIC)
endif()

add_library(fo4 ${FO4_LIBRARY_KIND}
  src/plugin_key.cpp
  src/text/encoding.cpp
  src/loadorder/plugin_list.cpp
  src/loadorder/assemble.cpp
  src/loadorder/mo2_profile.cpp
  src/formid.cpp
  src/archive/ba2.cpp
  src/archive/ba2_hash.cpp
  src/archive/ba2_texture.cpp
  src/archive/ba2_reader.cpp
  src/archive/assets.cpp
  src/binary/cursor.cpp
  src/nif/nif.cpp
  src/nif/geometry.cpp
  src/nif/block_transform.cpp
  src/nif/shader.cpp
  src/nif/skin.cpp
  src/nif/collision.cpp
  src/nif/hknp_packfile.cpp
  src/nif/hknp_encode.cpp
  src/nif/hknp_decode_fields.cpp
  src/nif/hknp_decode_shapes.cpp
  src/nif/hknp_decode_system.cpp
  src/texture/bcn.cpp
  src/texture/dds.cpp
  src/texture/dds_codec.cpp
  src/nif/write_edits.cpp
  src/nif/write_model.cpp
  src/nif/import_obj.cpp
  src/nif/import_build.cpp
  src/nif/fix_nif.cpp
  src/forms/armor.cpp
  src/forms/armor_edit.cpp
  src/forms/cell.cpp
  src/forms/cell_create.cpp
  src/forms/components.cpp
  src/forms/effects.cpp
  src/forms/placed_reference.cpp
  src/forms/placed_reference_copy.cpp
  src/forms/info_copy.cpp
  src/forms/copy_as_new.cpp
  src/forms/copy_as_wrapper.cpp
  src/forms/placed_reference_edit.cpp
  src/forms/placed_reference_create.cpp
  src/forms/placed_reference_move.cpp
  src/forms/static_collection.cpp
  src/forms/eyes.cpp
  src/forms/weather.cpp
  src/forms/leveled_list.cpp
  src/forms/leveled_spell.cpp
  src/forms/leveled_spell_creation.cpp
  src/forms/proj_expl_hazd.cpp
  src/forms/quest_aliases_decode.cpp
  src/forms/quest_aliases_encode.cpp
  src/forms/quest_aliases_extras.cpp
  src/forms/quest_stages.cpp
  src/forms/quest_objectives.cpp
  src/forms/quest_alias_edit.cpp
  src/forms/quest_vmad.cpp
  src/forms/race.cpp
  src/forms/race_body.cpp
  src/forms/npc.cpp
  src/forms/omod.cpp
  src/forms/perk.cpp
  src/forms/faction.cpp
  src/forms/weapon.cpp
  src/io/atomic_write.cpp
  src/localization/strings.cpp
  src/plugin/localization_plugin.cpp
  src/plugin/localized_writes.cpp
  src/plugin/tes4.cpp
  src/plugin/reader.cpp
  src/plugin/master_kinds.cpp
  src/plugin/load_order.cpp
  src/plugin/flags.cpp
  src/plugin/mutation.cpp
  src/plugin/record_flags.cpp
  src/plugin/copy_hierarchy.cpp
  src/plugin/vmad_writer.cpp
  src/plugin/vmad_fragment_edits.cpp
  src/plugin/navmesh_writer.cpp
  src/plugin/field_writer.cpp
  src/plugin/field_writer_containers.cpp
  src/plugin/destructible_stage.cpp
  src/plugin/subrecord_authoring.cpp
  src/plugin/subrecord_copy.cpp
  src/plugin/scene_writer.cpp
  src/plugin/sound_writer.cpp
  src/plugin/scene_index.cpp
  src/plugin/master_usage.cpp
  src/plugin/maintenance.cpp
  src/plugin/placement.cpp
  src/plugin/onam.cpp
  src/plugin/writer.cpp
  src/schema/condition_functions.cpp
  src/schema/condition_scope_path.cpp
  src/schema/condition_scopes.cpp
  src/schema/ctda.cpp
  src/schema/form_links.cpp
  src/schema/form_links_ctda.cpp
  src/schema/form_link_mixed.cpp
  src/schema/form_link_records.cpp
  src/schema/form_link_schema.cpp
  src/schema/behavior_layout.cpp
  src/schema/localized_fields.cpp
  src/schema/formlink_lists.cpp
  src/schema/formlink_segments.cpp
  src/schema/formlink_list_read.cpp
  src/schema/nested_formlink_lists.cpp
  src/schema/nested_formlink_list_null.cpp
  src/schema/preflight.cpp
  src/schema/non_formid_slots.cpp
  src/schema/scalar.cpp
  src/schema/field_address.cpp
  src/schema/formlink_slots.cpp
  src/schema/vmad.cpp
  src/graph/conflict_graph.cpp
  src/graph/conflict_kind.cpp
  src/graph/check_errors.cpp
  src/graph/check_errors_records.cpp
  src/graph/plugin_diff.cpp
  src/graph/reachability.cpp
  src/environment/environment.cpp
  src/environment/world.cpp
  src/environment/region.cpp
  src/environment/placed.cpp
  src/environment/terrain.cpp
  src/environment/strings.cpp
  src/environment/vegetation.cpp
  src/environment/navmesh.cpp
  src/environment/behavior.cpp
  src/environment/package_procedure.cpp
  src/environment/package_procedure_tree.cpp
  src/environment/dialogue.cpp
  src/environment/scene_layout.cpp
  src/environment/base_records.cpp
  src/environment/sound.cpp
  src/environment/material_swap.cpp
  src/pex/pex.cpp
  src/papyrus/status.cpp
  src/papyrus/value.cpp
  src/papyrus/host.cpp
  src/papyrus/array_ops.cpp
  src/papyrus/interpreter.cpp
  src/papyrus/execution.cpp
  src/papyrus/struct_ops.cpp
  src/papyrus/owner.cpp
  src/papyrus/registration.cpp
  src/papyrus/sha256.cpp
  src/papyrus/save_state.cpp
  src/papyrus/quest_rules.cpp
  src/papyrus/runtime.cpp
  src/papyrus/scheduler.cpp
  src/papyrus/execute.cpp
  src/papyrus/decompiler.cpp
  src/papyrus/decompiler_expr.cpp
  src/papyrus/decompiler_stmt.cpp
  src/papyrus/lexer.cpp
  src/papyrus/parser.cpp
  src/papyrus/parser_statement.cpp
  src/papyrus/parser_expression.cpp
  src/papyrus/type.cpp
  src/papyrus/binding.cpp
  src/papyrus/resolver.cpp
  src/papyrus/type_checker.cpp
  src/papyrus/script_index.cpp
  src/papyrus/symbols.cpp
  src/papyrus/declaration_check.cpp
  src/papyrus/file_walk.cpp
  src/papyrus/user_flags.cpp
  src/papyrus/code_generator.cpp
  src/papyrus/body_statement.cpp
  src/papyrus/body_expression.cpp
  src/papyrus/body_call.cpp
  src/papyrus/creation_kit_fidelity.cpp
  src/papyrus/compile_script.cpp
  src/papyrus/analysis_service.cpp
  src/f4se/binding_validator.cpp
  src/f4se/cpp_emitter.cpp
  src/f4se/cpp_scanner.cpp
  src/f4se/cross_check.cpp
  src/f4se/emitter.cpp
  src/f4se/native_oracle.cpp
  src/f4se/papyrus_header_emitter.cpp
  src/f4se/plugin_emitter.cpp
  src/f4se/region_merge.cpp
  src/f4se/registration_extractor.cpp
  src/f4se/type_map.cpp
  src/f4se/version_diff.cpp
  src/graph/builtin_node_definitions.cpp
  src/graph/graph_compiler.cpp
  src/graph/graph_definite_assignment.cpp
  src/graph/graph_document.cpp
  src/graph/graph_exec_flow.cpp
  src/graph/graph_lifter.cpp
  src/graph/graph_lifter_expr.cpp
  src/graph/graph_linearizer.cpp
  src/graph/graph_linearizer_expr.cpp
  src/graph/graph_name_table.cpp
  src/graph/graph_script_loader.cpp
  src/graph/graph_tool_service.cpp
  src/graph/graph_type_resolver.cpp
  src/graph/graph_validator.cpp
  src/graph/node_palette.cpp
  src/graph/papyrus_source_writer.cpp
  src/graph/wiki_docs.cpp
  src/swf/swf.cpp
  src/swf/swf_tags.cpp
  src/swf/swf_placement.cpp
  src/swf/swf_shape.cpp
  src/swf/swf_morph.cpp
  src/swf/swf_bitmap.cpp
  src/swf/swf_bitmap_png.cpp
  src/swf/swf_text.cpp
  src/swf/swf_timeline.cpp
  src/swf/swf_display.cpp
  src/swf/swf_frame_control.cpp
  src/swf/swf_edit.cpp
  src/swf/swf_depth.cpp
  src/swf/swf_keyframe.cpp
  src/swf/swf_characters.cpp
  src/swf/swf_history.cpp
  src/swf/swf_text_edit.cpp
  src/swf/swf_write.cpp
  src/swf/swf_lzma.cpp
  src/swf/swf_glyph.cpp
  src/swf/swf_glyph_edit.cpp
  src/swf/swf_bitmap_edit.cpp
  src/swf/swf_sound.cpp
  src/swf/swf_sound_edit.cpp
  src/swf/swf_census.cpp
  src/swf/swf_svg.cpp
  src/swf/swf_frame_svg.cpp
  src/swf/swf_svg_filters.cpp
  src/pex_analysis/opcodes.cpp
  src/pex_analysis/cfg.cpp
  src/pex_analysis/disasm.cpp
  src/pex_analysis/callgraph.cpp
  src/pex_analysis/parent_chain.cpp
  src/pex_analysis/summary.cpp
  src/pex_analysis/fragments.cpp
  src/pex_analysis/script_set.cpp
  src/abc/abc_file.cpp
  src/abc/abc_write.cpp
  src/abc/byte_reader.cpp
  src/abc/constant_pool.cpp
  src/abc/opcodes.cpp
  src/abc/disassembly.cpp
  src/abc/edit.cpp
  src/abc/summary.cpp
  src/abc/abc_flow.cpp
  src/abc/abc_stack.cpp
  src/abc/abc_verify.cpp
  src/abc/abc_text.cpp
  src/abc/abc_text_body.cpp
  src/abc/abc_asm_lex.cpp
  src/abc/abc_asm.cpp
  src/abc/abc_asm_code.cpp
  src/avm1/opcodes.cpp
  src/avm1/decode.cpp
  src/avm1/encode.cpp
  src/avm1/edit.cpp
  src/avm1/disassembly.cpp
  src/avm1/control_ops.cpp
  src/avm1/function_ops.cpp
  src/avm1/interpreter.cpp
  src/avm1/misc_ops.cpp
  src/avm1/object_ops.cpp
  src/avm1/text_ops.cpp
  src/avm1/timeline_ops.cpp
  src/avm2/name.cpp
  src/avm2/value.cpp
  src/avm2/coercion.cpp
  src/avm2/object.cpp
  src/avm2/class.cpp
  src/avm2/runtime.cpp
  src/avm2/runtime_link.cpp
  src/avm2/domain.cpp
  src/avm2/interpreter.cpp
  src/avm2/interpreter_ops.cpp
  src/avm2/interpreter_property.cpp
  src/avm2/interpreter_memory.cpp
  src/avm2/interpreter_xml_text.cpp
  src/avm2/interpreter_support.cpp
  src/avm2/builtins.cpp
  src/avm2/native_object.cpp
  src/avm2/native_primitive.cpp
  src/avm2/native_math.cpp
  src/avm2/native_top.cpp
  src/avm2/native_string.cpp
  src/avm2/native_array.cpp
  src/avm2/native_error.cpp
  src/avm2/display.cpp
  src/avm2/native_display.cpp
  src/avm2/swf_movie_data.cpp
  src/avm2/swf_runtime.cpp
  src/avm2/snapshot.cpp
  src/avm2/display_snapshot.cpp
  src/avm2/hit_test.cpp
  src/avm2/amf.cpp
  src/avm2/replay.cpp
  src/avm2/native_shared.cpp
  src/avm2/native_bgs.cpp
  src/avm2/native_compat.cpp
  src/avm2/sound_decode.cpp
  src/avm2/events.cpp
  src/avm2/native_events.cpp
  src/avm2/text.cpp
  src/avm2/utf16.cpp
  src/avm2/native_text.cpp
  src/avm2/native_net.cpp
  src/avm2/native_system.cpp
  src/avm2/native_bytearray.cpp
  src/hkx/layout.cpp
  src/hkx/packfile.cpp
  src/hkx/skeleton.cpp
  src/hkx/animation.cpp
  src/hkx/animation_binding.cpp
  src/hkx/animation_spline.cpp
  src/material/material_file.cpp
  src/seq/seq.cpp
  src/quest/runtime.cpp
  src/quest/structure.cpp
)

if(TARGET radicle_schema_check)
  add_dependencies(fo4 radicle_schema_check)
endif()
if(TARGET radicle_record_schema_check)
  add_dependencies(fo4 radicle_record_schema_check)
endif()
if(TARGET radicle_formlink_schema_check)
  add_dependencies(fo4 radicle_formlink_schema_check)
endif()
if(TARGET radicle_condition_scope_schema_check)
  add_dependencies(fo4 radicle_condition_scope_schema_check)
endif()
if(TARGET radicle_condition_function_schema_check)
  add_dependencies(fo4 radicle_condition_function_schema_check)
endif()
if(TARGET radicle_hkx_layout_check)
  add_dependencies(fo4 radicle_hkx_layout_check)
endif()

set_target_properties(fo4 PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(fo4
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(fo4 PRIVATE ZLIB::ZLIB)
target_link_libraries(fo4 PUBLIC $<BUILD_INTERFACE:nlohmann_json::nlohmann_json>)
fo4_apply_warnings(fo4)
fo4_apply_sanitizers(fo4)

set(RADICLE_C_SOURCES
  src/c_api.cpp src/c_api_env.cpp src/c_api_navmesh.cpp src/c_api_vegetation.cpp
  src/c_api_world.cpp src/c_api_placed.cpp src/c_api_terrain.cpp src/c_api_strings.cpp
  src/c_api_archive.cpp src/c_api_writeback.cpp src/c_api_fields.cpp src/c_api_ref_fields.cpp
  src/c_api_base_records.cpp src/c_api_sound.cpp src/c_api_materials.cpp src/c_api_check.cpp
  src/c_api_maintenance.cpp src/c_api_pex.cpp src/c_api_material_file.cpp src/c_api_seq.cpp
  src/c_api_quest.cpp src/c_api_quest_structure.cpp src/c_api_loadorder.cpp
  src/c_api_version.cpp
  src/c_api_records.cpp src/c_api_authoring.cpp src/c_api_vmad.cpp src/c_api_behavior.cpp
  src/c_api_conditions.cpp
  src/c_api_dialogue.cpp
  src/c_api_armor.cpp src/c_api_eyes.cpp src/c_api_faction.cpp src/c_api_components.cpp
  src/c_api_cell.cpp
  src/c_api_patch.cpp
  src/c_api_conflict.cpp
  src/c_api_nif.cpp src/c_api_dds.cpp
  src/c_api_graph.cpp src/c_api_papyrus_source.cpp
  src/c_api_pex_analysis.cpp src/c_api_vmad_read.cpp src/c_api_script_set.cpp src/c_api_papyrus.cpp
  src/c_api_papyrus_runtime.cpp src/c_api_papyrus_save.cpp src/c_api_papyrus_quest.cpp
  src/c_api_papyrus_compiler.cpp src/c_api_papyrus_analysis.cpp
  src/c_api_swf.cpp src/c_api_swf_art.cpp src/c_api_swf_characters.cpp src/c_api_swf_display.cpp
  src/c_api_swf_edit.cpp src/c_api_swf_history.cpp src/c_api_swf_verbs.cpp src/c_api_swf_abc_write.cpp
  src/c_api_swf_jpeg.cpp src/c_api_swf_sound.cpp src/c_api_runtime.cpp
)

if(RADICLE_SHARED)
  # One library carries the C++ API and the C ABI, so the editor and the C#
  # bindings load the same radicle.dll. The C ABI keeps its explicit dllexport;
  # the C++ symbols the editor links against are exported from a generated
  # module definition file.
  target_compile_definitions(fo4 PRIVATE RADICLE_C_BUILD)
  target_sources(fo4 PRIVATE ${RADICLE_C_SOURCES})
  set_target_properties(fo4 PROPERTIES
    OUTPUT_NAME radicle
    WINDOWS_EXPORT_ALL_SYMBOLS ON)
  # A PE import library holds at most 65535 objects (LNK1189).
  if(MSVC)
    target_compile_options(fo4 PRIVATE /Zc:inline)
  endif()
  # Stamp the PE image version. SOVERSION stays empty: on the Linux path it only
  # adds the libradicle.so -> libradicle.so.0 symlink step, which some checkout
  # volumes cannot create.
  if(WIN32)
    set_target_properties(fo4 PROPERTIES VERSION ${PROJECT_VERSION})
  endif()
else()
  add_library(radicle_c SHARED ${RADICLE_C_SOURCES})
  target_compile_definitions(radicle_c PRIVATE RADICLE_C_BUILD)
  target_link_libraries(radicle_c PRIVATE fo4)
  set_target_properties(radicle_c PROPERTIES
    OUTPUT_NAME radicle
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR})
  fo4_apply_warnings(radicle_c)
  fo4_apply_sanitizers(radicle_c)
endif()

target_sources(fo4 PRIVATE src/patch/delta_patch.cpp)
target_sources(fo4 PRIVATE src/patch/list_merge.cpp)
target_sources(fo4 PRIVATE src/papyrus_source/lexer.cpp src/papyrus_source/parser.cpp src/papyrus_source/parser_decl.cpp src/papyrus_source/parser_expr.cpp src/papyrus_source/parser_stmt.cpp src/papyrus_source/summary.cpp)

if(RADICLE_INSTALL)
  include(GNUInstallDirs)
  set(RADICLE_INSTALL_TARGETS fo4)
  if(TARGET radicle_c)
    list(APPEND RADICLE_INSTALL_TARGETS radicle_c)
  endif()
  install(TARGETS ${RADICLE_INSTALL_TARGETS}
    EXPORT radicle-targets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  )
  install(DIRECTORY include/fo4 DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
  install(EXPORT radicle-targets
    NAMESPACE radicle::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/radicle
  )

  include(CMakePackageConfigHelpers)
  configure_package_config_file(
    cmake/radicleConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/radicleConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/radicle
  )
  write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/radicleConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
  )
  install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/radicleConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/radicleConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/radicle
  )
endif()

if(RADICLE_BUILD_TOOLS)
  add_executable(fo4inspect tools/inspect/main.cpp)
  target_link_libraries(fo4inspect PRIVATE fo4)
  fo4_apply_warnings(fo4inspect)
  fo4_apply_sanitizers(fo4inspect)
  add_executable(radicle_env_bench tools/env_bench/main.cpp tools/env_bench/env_bench.cpp)
  target_include_directories(radicle_env_bench PRIVATE tools)
  target_link_libraries(radicle_env_bench PRIVATE fo4)
  fo4_apply_warnings(radicle_env_bench)
  fo4_apply_sanitizers(radicle_env_bench)
  add_executable(radicle_parity_dump tools/parity/main.cpp)
  target_include_directories(radicle_parity_dump PRIVATE tools)
  target_link_libraries(radicle_parity_dump PRIVATE fo4)
  fo4_apply_warnings(radicle_parity_dump)
  add_executable(radicle_parity_writeback tools/parity/writeback.cpp)
  target_include_directories(radicle_parity_writeback PRIVATE tools)
  target_link_libraries(radicle_parity_writeback PRIVATE fo4)
  if(TARGET radicle_c)
    target_link_libraries(radicle_parity_writeback PRIVATE radicle_c)
  endif()
  fo4_apply_warnings(radicle_parity_writeback)
endif()

if(RADICLE_BUILD_TESTS)
  include(CTest)
  enable_testing()
  include(FetchContent)
  set(GTEST_VERSION v1.17.0)
  FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/${GTEST_VERSION}.zip
    URL_HASH SHA256=40d4ec942217dcc84a9ebe2a68584ada7d4a33a8ee958755763278ea1c5e18ff
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    INACTIVITY_TIMEOUT 60
  )
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)

  add_executable(fo4_tests
    tests/test_achr_extra_rows.cpp
    tests/test_achr_rows.cpp
    tests/test_achr_parity_rows.cpp
    tests/test_parw_rows.cpp
    tests/test_signature.cpp
    tests/test_formid.cpp
    tests/test_cursor.cpp
    tests/test_reader.cpp
    tests/test_compression.cpp
    tests/test_undecodable_record.cpp
    tests/test_undecodable_edit.cpp
    tests/test_real_plugin.cpp
    tests/test_row_roundtrip_real.cpp
    tests/test_actor_value_perk_real.cpp
    tests/test_formkey_load.cpp
    tests/test_master_kinds.cpp
    tests/test_master_metadata.cpp
    tests/test_load_order.cpp
    tests/test_plugin_flags.cpp
    tests/test_tes4_header_setters.cpp
    tests/test_mutation.cpp
    tests/test_editor_id_text.cpp
    tests/test_record_lifecycle.cpp
    tests/test_delete_child_groups.cpp
    tests/test_copy_containment.cpp
    tests/test_copy_cell_children.cpp
    tests/test_cell_rows.cpp
    tests/test_cell_abi_extras.cpp
    tests/test_scalar_mutation.cpp
    tests/test_counted_array.cpp
    tests/test_uncounted_arrays.cpp
    tests/test_subrecord_group.cpp
    tests/test_nested_group_runs.cpp
    tests/test_writer.cpp
    tests/test_atomic_write.cpp
    tests/test_hedr_count.cpp
    tests/test_header_versions_real.cpp
    tests/test_tes4_masters_hedr.cpp
    tests/test_formlinks.cpp
    tests/test_clmt_formlinks.cpp
    tests/test_ipds_formlinks.cpp
    tests/test_dobj_formlinks.cpp
    tests/test_wp9_link_rows.cpp
    tests/test_wp9_bounds_model_rows.cpp
    tests/test_wp9_ltex_llkc_rows.cpp
    tests/test_wp9_union_rows.cpp
    tests/test_wp9_misc_rows_a.cpp
    tests/test_wp9_list_rows.cpp
    tests/test_wp9_misc_rows_b.cpp
    tests/test_wp9_misc_rows_c.cpp
    tests/test_wp9_small_records.cpp
    tests/test_wp9_prps_rows.cpp
    tests/test_wp9_cobj_rows.cpp
    tests/test_wp9_race_weap_rows.cpp
    tests/test_race_data_rows.cpp
    tests/test_race_tint_rows.cpp
    tests/test_race_tint_real.cpp
    tests/test_race_data_links.cpp
    tests/test_race_link_rows.cpp
    tests/test_wp9_scol.cpp
    tests/test_furn_rows.cpp
    tests/test_wrld_rows.cpp
    tests/test_refr_extra_rows.cpp
    tests/test_ipds_movt_rows.cpp
    tests/test_armo_arma_rows.cpp
    tests/test_arma_sculpt_rows.cpp
    tests/test_destructible_rows.cpp
    tests/test_destructible_stage_members.cpp
    tests/test_destructible_header.cpp
    tests/test_packed_formlink_arrays.cpp
    tests/test_formlink_ordinals.cpp
    tests/test_rekey_formlinks.cpp
    tests/test_form_link_schema_remap.cpp
    tests/test_formlink_lists.cpp
    tests/test_flst_formlink_lists.cpp
    tests/test_kwda_formlink_lists.cpp
    tests/test_components.cpp
    tests/test_magic_effects.cpp
    tests/test_nested_formlink_lists.cpp
    tests/test_formlink_list_management.cpp
    tests/test_sraf_formlinks.cpp
    tests/test_exterior_cell_grid.cpp
    tests/test_exterior_cell_grid_real.cpp
    tests/test_exterior_cell_creation.cpp
    tests/test_interior_cell_create.cpp
    tests/test_placed_data_rows.cpp
    tests/test_placed_reference.cpp
    tests/test_placed_reference_copy.cpp
    tests/test_copy_as_new.cpp
    tests/test_copy_as_wrapper.cpp
    tests/test_info_copy_new.cpp
    tests/test_placed_variants.cpp
    tests/test_placed_variants_real.cpp
    tests/test_placed_reference_create.cpp
    tests/test_placed_reference_move.cpp
    tests/test_placed_reference_move_exterior.cpp
    tests/test_record_flags.cpp
    tests/test_record_flags_deleted.cpp
    tests/test_extension_light_kinds.cpp
    tests/test_static_collection.cpp
    tests/test_quest_aliases.cpp
    tests/test_quest_alias_real.cpp
    tests/test_quest_stages_real.cpp
    tests/test_quest_objectives_real.cpp
    tests/test_proj_expl_hazd_real.cpp
    tests/test_proj_expl_hazd_creation.cpp
    tests/test_proj_expl_hazd_reencode_real.cpp
    tests/test_scalar_embedded.cpp
    tests/test_field_writer.cpp
    tests/test_localized_value_writes.cpp
    tests/test_localization_tables.cpp
    tests/test_plugin_localization.cpp
    tests/test_subrecord_authoring.cpp
    tests/test_subrecord_copy.cpp
    tests/test_c_api_authoring.cpp
    tests/test_c_api_fields.cpp
    tests/test_delta_patch.cpp
    tests/test_list_merge.cpp
    tests/test_climate_weather_authoring.cpp
    tests/test_clmt_weather_entries.cpp
    tests/test_clmt_weather_real.cpp
    tests/test_climate_weather_c_api.cpp
    tests/test_climate_weather_real.cpp
    tests/test_quest_authoring_real.cpp
    tests/test_plugin_corpus.cpp
    tests/test_struct_payload_formlinks_real.cpp
    tests/test_obts_formlinks.cpp
    tests/test_omod_rows.cpp
    tests/test_quest_authoring.cpp
    tests/test_quest_objectives.cpp
    tests/test_quest_stages.cpp
    tests/test_qust_general_rows.cpp
    tests/test_typed_families_real.cpp
    tests/test_localized_fields.cpp
    tests/test_quest_alias_encoding.cpp
    tests/test_quest_alias_formlinks.cpp
    tests/test_quest_alias_location_formlinks.cpp
    tests/test_quest_alias_mutation.cpp
    tests/test_quest_alias_collection.cpp
    tests/test_quest_alias_formlink_remap.cpp
    tests/test_quest_alias_remaining_fills.cpp
    tests/test_quest_alias_extra_decode.cpp
    tests/test_scalar.cpp
    tests/test_preflight.cpp
    tests/test_ctda_strings.cpp
    tests/test_ctda_model.cpp
    tests/test_ctda_alias_formids.cpp
    tests/test_condition_scopes.cpp
    tests/test_condition_scopes_records.cpp
    tests/test_condition_scopes_mesg.cpp
    tests/test_condition_scopes_term.cpp
    tests/test_condition_root_insert.cpp
    tests/test_root_condition_lists.cpp
    tests/test_condition_scopes_real.cpp
    tests/test_condition_functions.cpp
    tests/test_package_targets.cpp
    tests/test_package_tree.cpp
    tests/test_package_tree_c_api.cpp
    tests/test_package_tree_real.cpp
    tests/test_perk_entrypoints.cpp
    tests/test_perk_rows.cpp
    tests/test_vegetation.cpp
    tests/test_vegetation_edit.cpp
    tests/test_vegetation_authoring.cpp
    tests/test_vegetation_real.cpp
    tests/test_navmesh.cpp
    tests/test_navmesh_authoring.cpp
    tests/test_navmesh_writer_real.cpp
    tests/test_navmesh_real.cpp
    tests/test_navmesh_formlinks.cpp
    tests/test_armor_creation.cpp
    tests/test_armor_edit.cpp
    tests/test_eyes.cpp
    tests/test_base_items.cpp
    tests/test_base_items_real.cpp
    tests/test_magic_location_records.cpp
    tests/test_magic_location_records_real.cpp
    tests/test_object_base_records.cpp
    tests/test_object_base_records_real.cpp
    tests/test_object_base_model_info.cpp
    tests/test_weather_concept.cpp
    tests/test_weather_formlinks.cpp
    tests/test_lvsp.cpp
    tests/test_leveled_list.cpp
    tests/test_leveled_list_real.cpp
    tests/test_leveled_list_creation.cpp
    tests/test_race_creation.cpp
    tests/test_race_body.cpp
    tests/test_npc_lists.cpp
    tests/test_npc_real.cpp
    tests/test_faction.cpp
    tests/test_faction_real.cpp
    tests/test_static_light_records.cpp
    tests/test_static_light_records_real.cpp
    tests/test_config_records.cpp
    tests/test_config_records_real.cpp
    tests/test_image_fx_records.cpp
    tests/test_image_fx_records_real.cpp
    tests/test_material_impact_records.cpp
    tests/test_material_impact_records_real.cpp
    tests/test_actor_support_records.cpp
    tests/test_actor_support_records_real.cpp
    tests/test_item_mod_records.cpp
    tests/test_item_mod_records_real.cpp
    tests/test_story_manager_records.cpp
    tests/test_story_manager_records_real.cpp
    tests/test_term_menu_rows.cpp
    tests/test_world_misc_records.cpp
    tests/test_world_misc_records_real.cpp
    tests/test_weapon.cpp
    tests/test_weapon_real.cpp
    tests/test_onam.cpp
    tests/test_vmad_writer.cpp
    tests/test_vmad_writer_real.cpp
    tests/test_vmad_writer_c_api.cpp
    tests/test_vmad.cpp
    tests/test_vmad_unsupported.cpp
    tests/test_vmad_array_elements.cpp
    tests/test_c_api_vmad_read.cpp
    tests/test_vmad_structure.cpp
    tests/test_vmad_quest_fragments.cpp
    tests/test_vmad_fragments.cpp
    tests/test_conflict_graph.cpp
    tests/test_conflict_kind.cpp
    tests/test_conflict_compare.cpp
    tests/test_repoint_references.cpp
    tests/test_deep_copy.cpp
    tests/test_check_errors.cpp
    tests/test_check_errors_conditions.cpp
    tests/test_check_errors_group1.cpp
    tests/test_check_errors_record_lists.cpp
    tests/test_check_errors_real.cpp
    tests/test_check_dialogue.cpp
    tests/test_remap_non_formid_slots.cpp
    tests/test_struct_payload_remap.cpp
    tests/test_generator_gap_remap.cpp
    tests/test_union_payload_remap.cpp
    tests/test_record_struct_remap.cpp
    tests/test_remap_scalar_enum_slots.cpp
    tests/test_master_usage.cpp
    tests/test_plugin_diff.cpp
    tests/test_reachability.cpp
    tests/test_maintenance.cpp
    tests/test_maintenance_nested.cpp
    tests/test_esl_compaction.cpp
    tests/test_c_api_maintenance.cpp
    tests/test_c_api_pex.cpp
    tests/test_c_api_papyrus_source.cpp
    tests/test_c_api_seq.cpp
    tests/test_c_api_quest.cpp
    tests/test_ba2.cpp
    tests/test_ba2_hardening.cpp
    tests/test_ba2_texture.cpp
    tests/test_localized_strings.cpp
    tests/test_c_api.cpp
    tests/test_environment.cpp
    tests/test_behavior.cpp
    tests/test_behavior_authoring.cpp
    tests/test_behavior_writer_c_api.cpp
    tests/test_behavior_writer_real.cpp
    tests/test_behavior_real.cpp
    tests/test_dialogue.cpp
    tests/test_dialogue_real.cpp
    tests/test_dialogue_writer.cpp
    tests/test_dialogue_subrecords.cpp
    tests/test_dialogue_writer_c_api.cpp
    tests/test_dialogue_writer_real.cpp
    tests/test_scene_writer.cpp
    tests/test_scene_phase_edit.cpp
    tests/test_scene_actor_action_edit.cpp
    tests/test_environment_world.cpp
    tests/test_environment_region.cpp
    tests/test_environment_water_edges.cpp
    tests/test_environment_placed.cpp
    tests/test_environment_script_elements.cpp
    tests/test_environment_terrain.cpp
    tests/test_environment_terrain_real.cpp
    tests/test_environment_strings.cpp
    tests/test_ba2_reader.cpp
    tests/test_ba2_write.cpp
    tests/test_ba2_long_names.cpp
    tests/test_ba2_real.cpp
    tests/test_ba2_versions_real.cpp
    tests/test_nif.cpp
    tests/test_nif_nodes.cpp
    tests/test_nif_corpus.cpp
    tests/test_nif_collision.cpp
    tests/test_nif_collision_real.cpp
    tests/test_c_api_nif.cpp
    tests/test_c_api_nif_collision.cpp
    tests/test_c_api_swf.cpp
    tests/test_c_api_swf_text.cpp
    tests/test_c_api_swf_morph.cpp
    tests/test_c_api_swf_display.cpp
    tests/test_c_api_swf_characters.cpp
    tests/test_c_api_swf_history.cpp
    tests/test_c_api_swf_edit.cpp
    tests/test_c_api_swf_abc_sprite.cpp
    tests/test_c_api_swf_glyph.cpp
    tests/test_c_api_swf_sound.cpp
    tests/test_c_api_swf_real.cpp
    tests/test_dds.cpp
    tests/test_nif_write.cpp
    tests/test_environment_writeback.cpp
    tests/test_writeback_real.cpp
    tests/test_environment_ref_fields.cpp
    tests/test_environment_base_records.cpp
    tests/test_c_api_conditions.cpp
    tests/test_water_real.cpp
    tests/test_water_authoring.cpp
    tests/test_water_c_api.cpp
    tests/test_sound.cpp
    tests/test_sound_real.cpp
    tests/test_sound_authoring.cpp
    tests/test_sound_subrecords.cpp
    tests/test_music_track_lists.cpp
    tests/test_sound_writer_c_api.cpp
    tests/test_sound_structure_c_api.cpp
    tests/test_sound_writer_real.cpp
    tests/test_audio_records.cpp
    tests/test_audio_records_real.cpp
    tests/test_sky_parameters.cpp
    tests/test_sky_real.cpp
    tests/test_environment_material_swap.cpp
    tests/test_original_storage.cpp
    tests/test_env_bench_real.cpp
    tests/test_text_boundary.cpp
    tests/test_parity_dds.cpp
    tests/test_pex.cpp
    tests/test_pex_hardening.cpp
    tests/test_pex_corpus.cpp
    tests/test_pex_oracle.cpp
    tests/test_papyrus_vm.cpp
    tests/test_papyrus_structs.cpp
    tests/test_papyrus_runtime.cpp
    tests/test_papyrus_save_state.cpp
    tests/test_papyrus_quest_rules.cpp
    tests/test_c_api_papyrus.cpp
    tests/test_c_api_papyrus_save_quest.cpp
    tests/test_c_api_papyrus_runtime.cpp
    tests/test_c_api_script_set.cpp
    tests/test_pex_analysis.cpp
    tests/test_pex_analysis_c_api.cpp
    tests/test_pex_analysis_real.cpp
    tests/test_script_set.cpp
    tests/test_script_set_real.cpp
    tests/test_pex_disasm.cpp
    tests/test_papyrus_source.cpp
    tests/test_papyrus_source_real.cpp
    tests/test_papyrus_decompiler.cpp
    tests/test_papyrus_decompiler_real.cpp
    tests/test_papyrus_lexer.cpp
    tests/test_papyrus_parser.cpp
    tests/test_papyrus_parse_real.cpp
    tests/test_papyrus_resolver.cpp
    tests/test_papyrus_type_checker.cpp
    tests/test_papyrus_symbols.cpp
    tests/test_papyrus_script_index.cpp
    tests/test_papyrus_declaration_check.cpp
    tests/test_papyrus_semantic_real.cpp
    tests/test_papyrus_codegen.cpp
    tests/test_papyrus_codegen_corners.cpp
    tests/test_papyrus_codegen_real.cpp
    tests/test_papyrus_user_flags.cpp
    tests/test_papyrus_analysis_service.cpp
    tests/test_c_api_papyrus_analysis.cpp
    tests/test_f4se_type_map.cpp
    tests/test_f4se_cross_check.cpp
    tests/test_f4se_extractor.cpp
    tests/test_f4se_region_merge.cpp
    tests/test_f4se_emitter.cpp
    tests/test_f4se_corpus_real.cpp
    tests/test_graph_compiler.cpp
    tests/test_graph_document.cpp
    tests/test_graph_flow.cpp
    tests/test_graph_lift.cpp
    tests/test_graph_lift_real.cpp
    tests/test_graph_node_palette.cpp
    tests/test_graph_script_loader.cpp
    tests/test_graph_tool_service.cpp
    tests/test_graph_validator.cpp
    tests/test_abc.cpp
    tests/test_abc_edit.cpp
    tests/test_abc_pool.cpp
    tests/test_abc_write.cpp
    tests/test_abc_summary.cpp
    tests/test_abc_flow.cpp
    tests/test_abc_verify.cpp
    tests/test_abc_text.cpp
    tests/test_abc_text_real.cpp
    tests/test_abc_asm.cpp
    tests/test_abc_asm_real.cpp
    tests/test_abc_pushshort.cpp
    tests/test_avm2_value.cpp
    tests/test_avm2_object.cpp
    tests/test_avm2_runtime.cpp
    tests/test_avm2_call_indexed.cpp
    tests/test_avm2_interpreter.cpp
    tests/test_avm2_scope_depth.cpp
    tests/test_avm2_scripts_real.cpp
    tests/test_avm2_builtins.cpp
    tests/test_avm2_runtime_names.cpp
    tests/test_avm2_display.cpp
    tests/test_avm2_swf_runtime.cpp
    tests/test_avm2_snapshot.cpp
    tests/test_avm2_host.cpp
    tests/test_avm2_bgs_bridge.cpp
    tests/test_avm2_domain.cpp
    tests/test_avm2_script.cpp
    tests/test_avm2_shared.cpp
    tests/test_c_api_runtime.cpp
    tests/test_c_api_runtime_real.cpp
    tests/test_avm2_compat.cpp
    tests/test_avm2_sound_decode.cpp
    tests/test_avm2_display_api.cpp
    tests/test_avm2_display_snapshot.cpp
    tests/test_avm2_document_class.cpp
    tests/test_avm2_events.cpp
    tests/test_avm2_focus_events.cpp
    tests/test_avm2_text.cpp
    tests/test_avm2_string_utf16.cpp
    tests/test_avm2_static_text.cpp
    tests/test_avm2_system_classes.cpp
    tests/test_avm2_bytearray.cpp
    tests/test_avm2_more_ops.cpp
    tests/test_avm2_domain_memory.cpp
    tests/test_avm2_late_ops.cpp
    tests/test_swf_fonts.cpp
    tests/test_swf_fonts_real.cpp
    tests/test_material_file.cpp
    tests/test_material_file_real.cpp
    tests/test_swf.cpp
    tests/test_swf_doabc_round_trip.cpp
    tests/test_swf_glyph.cpp
    tests/test_swf_text_glyph.cpp
    tests/test_swf_lzma.cpp
    tests/test_swf_sound.cpp
    tests/test_swf_tags.cpp
    tests/test_swf_plan.cpp
    tests/test_swf_place.cpp
    tests/test_swf_place_filters.cpp
    tests/test_swf_display.cpp
    tests/test_swf_frame_control.cpp
    tests/test_swf_edit.cpp
    tests/test_swf_edit_text.cpp
    tests/test_swf_label_edit.cpp
    tests/test_swf_history.cpp
    tests/test_swf_depth.cpp
    tests/test_swf_keyframe.cpp
    tests/test_swf_characters.cpp
    tests/test_swf_art.cpp
    tests/test_swf_morph_tween.cpp
    tests/test_swf_real.cpp
    tests/test_swf_census.cpp
    tests/test_swf_census_real.cpp
    tests/test_avm1.cpp
    tests/test_avm1_encode.cpp
    tests/test_avm1_ops.cpp
    tests/test_avm1_objects.cpp
    tests/test_avm1_functions.cpp
    tests/test_avm1_control.cpp
    tests/test_avm1_timeline.cpp
    tests/test_avm1_real.cpp
    tests/test_swf_svg.cpp
    tests/test_swf_svg_text_matrix.cpp
    tests/test_swf_svg_buttons.cpp
    tests/test_swf_svg_text_clip.cpp
    tests/test_abc_real.cpp
    tests/test_hkx_packfile.cpp
    tests/test_hkx_animation.cpp
    tests/test_hkx_binding.cpp
    tests/test_hkx_real.cpp
    tests/test_c_api_material_file.cpp
    tests/test_seq.cpp
    tests/test_seq_corpus.cpp
    tests/test_quest_runtime.cpp
    tests/test_quest_structure.cpp
    tests/test_quest_structure_real.cpp
    tests/test_load_order_files.cpp
    tests/test_load_order_real.cpp
    tests/test_c_api_loadorder.cpp
    tests/test_mo2_profile.cpp
    tests/test_c_api_referenced_by.cpp
    tests/test_c_api_conflict.cpp
    tests/test_regn_rows.cpp
    tests/test_regn_region_decode.cpp
    tests/test_c_api_forms_modules.cpp
    tests/test_weap_rows.cpp
    tests/test_npc_link_rows.cpp
    tests/test_furn_marker_params.cpp
    tests/test_npc_template_actor_rows.cpp
    tests/test_npc_attack_data_rows.cpp
    tests/test_npc_face_tint_rows.cpp
    tests/test_npc_furn_attach_slots.cpp
    tools/env_bench/env_bench.cpp
    tests/test_fsts_rows.cpp
    tests/test_info_response_bytes.cpp
    tests/test_subrecord_group_members.cpp
    tests/test_mswp_rows.cpp
    tests/test_sopm_onam_rows.cpp
    tests/test_stat_lod_rows.cpp
    tests/plugin_bytes.cpp
    tests/real_data_build.cpp
    tests/test_real_data_build.cpp
  )
  target_include_directories(fo4_tests PRIVATE tests tools)
  target_link_libraries(fo4_tests PRIVATE fo4 GTest::gtest_main ZLIB::ZLIB)
  if(TARGET radicle_c)
    target_link_libraries(fo4_tests PRIVATE radicle_c)
  endif()
  target_compile_definitions(fo4_tests PRIVATE
    FO4_TESTDATA_DIR="testdata"
    _CRT_SECURE_NO_WARNINGS
  )
  target_compile_options(fo4_tests PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/wd4996>)
  fo4_apply_warnings(fo4_tests)
  fo4_apply_sanitizers(fo4_tests)

  include(GoogleTest)
  gtest_discover_tests(fo4_tests DISCOVERY_TIMEOUT 120
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
  add_test(NAME radicle_field_address_codegen
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_field_address_codegen.py)
  add_test(NAME radicle_counted_array_segments
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_counted_array_segments.py)
  add_test(NAME radicle_subrecord_group_codegen
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_subrecord_group_codegen.py)
  add_test(NAME radicle_subrecord_group_string_rows
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_subrecord_group_string_rows.py)
  add_test(NAME radicle_field_address_version_windows
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_field_address_version_windows.py)
  add_test(NAME radicle_parity_matrix_unit COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_parity_matrix.py)
  add_test(NAME radicle_condition_function_codegen
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_condition_function_codegen.py)
  if(RADICLE_BUILD_TOOLS)
    add_test(NAME radicle_papyrus_dump
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/parity/tests/test_papyrus_dump.py
                     $<TARGET_FILE:radicle_parity_dump>)
    add_test(NAME radicle_env_bench_non_ansi_data_dir
             COMMAND ${CMAKE_COMMAND} -DBENCH=$<TARGET_FILE:radicle_env_bench>
                     -DPLUGIN=${CMAKE_CURRENT_SOURCE_DIR}/testdata/SlowTime.esp
                     -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/env_bench/non_ansi_data_dir.cmake)
    add_test(NAME radicle_env_bench_unreadable_data_dir
             COMMAND ${CMAKE_COMMAND} -DBENCH=$<TARGET_FILE:radicle_env_bench>
                     -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/env_bench/unreadable_data_dir.cmake)
    set_tests_properties(radicle_env_bench_unreadable_data_dir PROPERTIES SKIP_REGULAR_EXPRESSION "skipped: ")
    add_test(NAME radicle_parity_tools
             COMMAND ${CMAKE_COMMAND} -DWRITEBACK=$<TARGET_FILE:radicle_parity_writeback>
                     -DDUMP=$<TARGET_FILE:radicle_parity_dump>
                     -DPLUGIN=${CMAKE_CURRENT_SOURCE_DIR}/testdata/SlowTime.esp
                     -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/parity/parity_tools.cmake)
  endif()

  # C# bindings: the probe prints the C layout of every public struct for Radicle.Interop.Tests.
  add_executable(radicle_abi_layout_probe bindings/csharp/tests/abi_layout_probe.cpp)
  target_include_directories(radicle_abi_layout_probe PRIVATE include)
  fo4_apply_warnings(radicle_abi_layout_probe)

  add_library(radicle_abi_stub SHARED bindings/csharp/tests/abi_stub.cpp)
  target_include_directories(radicle_abi_stub PRIVATE include)
  target_compile_definitions(radicle_abi_stub PRIVATE RADICLE_C_BUILD "RADICLE_ABI_STUB_VERSION=RADICLE_ABI_VERSION+1u")
  # ABI 1 is the last generation without the material swap exports.
  add_library(radicle_abi_stub_v1 SHARED bindings/csharp/tests/abi_stub.cpp)
  target_include_directories(radicle_abi_stub_v1 PRIVATE include)
  target_compile_definitions(radicle_abi_stub_v1 PRIVATE RADICLE_C_BUILD RADICLE_ABI_STUB_VERSION=1u)

  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_Interpreter_FOUND)
    # Fails when fo4/c_api*.h changed without regenerating the committed C# interop and probe.
    set(_interop_dir ${CMAKE_CURRENT_SOURCE_DIR}/bindings/csharp)
    set(_interop_out ${CMAKE_CURRENT_BINARY_DIR}/interop_check)
    add_test(NAME radicle_csharp_interop_up_to_date
             COMMAND ${CMAKE_COMMAND}
                     -DPYTHON=${Python3_EXECUTABLE}
                     -DGENERATOR=${_interop_dir}/tools/generate_interop.py
                     -DINCLUDE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/include
                     -DOUT_DIR=${_interop_out}
                     -DCOMMITTED_CS=${_interop_dir}/Radicle.Interop/RadicleNative.g.cs
                     -DCOMMITTED_PROBE=${_interop_dir}/tests/abi_layout_probe.cpp
                     -P ${_interop_dir}/tools/check_generated.cmake)
    add_test(NAME radicle_csharp_interop_check_ignores_line_endings
             COMMAND ${CMAKE_COMMAND}
                     -DPYTHON=${Python3_EXECUTABLE}
                     -DGENERATOR=${_interop_dir}/tools/generate_interop.py
                     -DINCLUDE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/include
                     -DOUT_DIR=${_interop_out}-eol
                     -DCOMMITTED_CS=${_interop_dir}/Radicle.Interop/RadicleNative.g.cs
                     -DCOMMITTED_PROBE=${_interop_dir}/tests/abi_layout_probe.cpp
                     -DCHECK=${_interop_dir}/tools/check_generated.cmake
                     -P ${_interop_dir}/tools/check_generated_eol.cmake)
    add_test(NAME radicle_run_managed_tests
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/bindings/csharp/tools/test_run_managed_tests.py)
  endif()

  option(RADICLE_CSHARP_TESTS "Run the C# binding tests with the .NET 8 SDK and checked-in package feed" OFF)
  if(RADICLE_CSHARP_TESTS)
    find_program(DOTNET_EXECUTABLE dotnet REQUIRED)
    # A shared Radicle build has no radicle_c target; the C ABI lives in fo4 then.
    if(TARGET radicle_c)
      set(RADICLE_C_LIBRARY_TARGET radicle_c)
    else()
      set(RADICLE_C_LIBRARY_TARGET fo4)
    endif()
    add_test(NAME radicle_csharp_bindings
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/bindings/csharp/tools/run_managed_tests.py
                     --dotnet ${DOTNET_EXECUTABLE})
    set_tests_properties(radicle_csharp_bindings PROPERTIES
      ENVIRONMENT "RADICLE_NATIVE_PATH=$<TARGET_FILE:${RADICLE_C_LIBRARY_TARGET}>;RADICLE_ABI_PROBE=$<TARGET_FILE:radicle_abi_layout_probe>;RADICLE_ABI_STUB=$<TARGET_FILE:radicle_abi_stub>;RADICLE_ABI_STUB_V1=$<TARGET_FILE:radicle_abi_stub_v1>")
  endif()
endif()
