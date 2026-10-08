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

option(FO4_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(FO4_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(RADICLE_BUILD_TOOLS "Build Radicle command-line tools" ${PROJECT_IS_TOP_LEVEL})
option(RADICLE_BUILD_TESTS "Build Radicle tests" ${PROJECT_IS_TOP_LEVEL})
option(RADICLE_INSTALL "Generate Radicle install/export rules" ${PROJECT_IS_TOP_LEVEL})
option(RADICLE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ${PROJECT_IS_TOP_LEVEL})

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

add_library(fo4
  src/plugin_key.cpp
  src/text/encoding.cpp
  src/formid.cpp
  src/archive/ba2.cpp
  src/archive/ba2_hash.cpp
  src/archive/ba2_texture.cpp
  src/archive/ba2_reader.cpp
  src/archive/assets.cpp
  src/binary/cursor.cpp
  src/forms/armor.cpp
  src/forms/armor_edit.cpp
  src/forms/cell.cpp
  src/forms/components.cpp
  src/forms/placed_reference.cpp
  src/forms/placed_reference_copy.cpp
  src/forms/placed_reference_edit.cpp
  src/forms/static_collection.cpp
  src/forms/eyes.cpp
  src/forms/weather_concept.cpp
  src/forms/leveled_spell.cpp
  src/forms/leveled_spell_creation.cpp
  src/forms/proj_expl_hazd.cpp
  src/forms/quest_aliases.cpp
  src/forms/quest_stages.cpp
  src/forms/quest_objectives.cpp
  src/forms/quest_alias_edit.cpp
  src/forms/quest_vmad.cpp
  src/forms/race.cpp
  src/forms/race_body.cpp
  src/io/atomic_write.cpp
  src/localization/strings.cpp
  src/plugin/tes4.cpp
  src/plugin/reader.cpp
  src/plugin/master_kinds.cpp
  src/plugin/load_order.cpp
  src/plugin/flags.cpp
  src/plugin/mutation.cpp
  src/plugin/field_writer.cpp
  src/plugin/subrecord_authoring.cpp
  src/plugin/master_usage.cpp
  src/plugin/maintenance.cpp
  src/plugin/placement.cpp
  src/plugin/onam.cpp
  src/plugin/writer.cpp
  src/schema/condition_scopes.cpp
  src/schema/ctda.cpp
  src/schema/form_links.cpp
  src/schema/form_link_mixed.cpp
  src/schema/localized_fields.cpp
  src/schema/formlink_lists.cpp
  src/schema/nested_formlink_lists.cpp
  src/schema/nested_formlink_list_null.cpp
  src/schema/preflight.cpp
  src/schema/non_formid_slots.cpp
  src/schema/scalar.cpp
  src/schema/field_address.cpp
  src/schema/vmad.cpp
  src/graph/conflict_graph.cpp
  src/graph/check_errors.cpp
  src/graph/plugin_diff.cpp
  src/graph/reachability.cpp
  src/environment/environment.cpp
  src/environment/world.cpp
  src/environment/placed.cpp
  src/environment/terrain.cpp
  src/environment/strings.cpp
  src/environment/vegetation.cpp
  src/environment/base_records.cpp
  src/environment/material_swap.cpp
  src/pex/pex.cpp
  src/seq/seq.cpp
  src/quest/runtime.cpp
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

set_target_properties(fo4 PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(fo4
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(fo4 PRIVATE ZLIB::ZLIB)
fo4_apply_warnings(fo4)
fo4_apply_sanitizers(fo4)

add_library(radicle_c SHARED src/c_api.cpp src/c_api_env.cpp src/c_api_vegetation.cpp src/c_api_world.cpp src/c_api_placed.cpp src/c_api_terrain.cpp src/c_api_strings.cpp src/c_api_archive.cpp src/c_api_writeback.cpp src/c_api_fields.cpp src/c_api_ref_fields.cpp src/c_api_base_records.cpp src/c_api_materials.cpp src/c_api_check.cpp src/c_api_maintenance.cpp src/c_api_pex.cpp src/c_api_seq.cpp src/c_api_quest.cpp)
target_compile_definitions(radicle_c PRIVATE RADICLE_C_BUILD)
target_link_libraries(radicle_c PRIVATE fo4)
set_target_properties(radicle_c PROPERTIES
  OUTPUT_NAME radicle
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR})
fo4_apply_warnings(radicle_c)
fo4_apply_sanitizers(radicle_c)
target_sources(radicle_c PRIVATE src/c_api_version.cpp)
target_sources(radicle_c PRIVATE src/c_api_records.cpp src/c_api_authoring.cpp)

if(RADICLE_INSTALL)
  include(GNUInstallDirs)
  install(TARGETS fo4 radicle_c
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
  target_link_libraries(radicle_parity_writeback PRIVATE fo4 radicle_c)
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
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)

  add_executable(fo4_tests
    tests/test_signature.cpp
    tests/test_formid.cpp
    tests/test_cursor.cpp
    tests/test_reader.cpp
    tests/test_xxxx.cpp
    tests/test_compression.cpp
    tests/test_real_plugin.cpp
    tests/test_formkey_load.cpp
    tests/test_master_kinds.cpp
    tests/test_master_metadata.cpp
    tests/test_load_order.cpp
    tests/test_plugin_flags.cpp
    tests/test_mutation.cpp
    tests/test_record_lifecycle.cpp
    tests/test_copy_containment.cpp
    tests/test_scalar_mutation.cpp
    tests/test_writer.cpp
    tests/test_atomic_write.cpp
    tests/test_hedr_count.cpp
    tests/test_formlinks.cpp
    tests/test_clmt_formlinks.cpp
    tests/test_ipds_formlinks.cpp
    tests/test_dobj_formlinks.cpp
    tests/test_packed_formlink_arrays.cpp
    tests/test_formlink_ordinals.cpp
    tests/test_rekey_formlinks.cpp
    tests/test_formlink_lists.cpp
    tests/test_flst_formlink_lists.cpp
    tests/test_kwda_formlink_lists.cpp
    tests/test_components.cpp
    tests/test_nested_formlink_lists.cpp
    tests/test_formlink_list_management.cpp
    tests/test_sraf_formlinks.cpp
    tests/test_exterior_cell_grid.cpp
    tests/test_exterior_cell_grid_real.cpp
    tests/test_exterior_cell_creation.cpp
    tests/test_placed_reference.cpp
    tests/test_placed_reference_copy.cpp
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
    tests/test_subrecord_authoring.cpp
    tests/test_c_api_authoring.cpp
    tests/test_c_api_fields.cpp
    tests/test_quest_authoring_real.cpp
    tests/test_plugin_corpus.cpp
    tests/test_struct_payload_formlinks_real.cpp
    tests/test_obts_formlinks.cpp
    tests/test_quest_authoring.cpp
    tests/test_quest_objectives.cpp
    tests/test_quest_stages.cpp
    tests/test_typed_families_real.cpp
    tests/test_localized_fields.cpp
    tests/test_quest_alias_encoding.cpp
    tests/test_quest_alias_formlinks.cpp
    tests/test_quest_alias_location_formlinks.cpp
    tests/test_quest_alias_mutation.cpp
    tests/test_quest_alias_boundary.cpp
    tests/test_quest_alias_collection_grouping.cpp
    tests/test_quest_alias_formlink_remap.cpp
    tests/test_quest_alias_remaining_fills.cpp
    tests/test_quest_alias_extra_decode.cpp
    tests/test_quest_alias_collection_authoring.cpp
    tests/test_scalar.cpp
    tests/test_preflight.cpp
    tests/test_ctda_strings.cpp
    tests/test_ctda_model.cpp
    tests/test_condition_scopes.cpp
    tests/test_package_targets.cpp
    tests/test_perk_entrypoints.cpp
    tests/test_vegetation.cpp
    tests/test_vegetation_real.cpp
    tests/test_navmesh_formlinks.cpp
    tests/test_armor_creation.cpp
    tests/test_armor_edit.cpp
    tests/test_eyes.cpp
    tests/test_eyes_creation.cpp
    tests/test_weather_concept.cpp
    tests/test_weather_formlinks.cpp
    tests/test_lvsp.cpp
    tests/test_lvsp_creation.cpp
    tests/test_race_creation.cpp
    tests/test_race_body.cpp
    tests/test_onam.cpp
    tests/test_vmad.cpp
    tests/test_vmad_unsupported.cpp
    tests/test_conflict_graph.cpp
    tests/test_check_errors.cpp
    tests/test_remap_non_formid_slots.cpp
    tests/test_master_usage.cpp
    tests/test_plugin_diff.cpp
    tests/test_reachability.cpp
    tests/test_maintenance.cpp
    tests/test_maintenance_nested.cpp
    tests/test_c_api_maintenance.cpp
    tests/test_c_api_pex.cpp
    tests/test_c_api_seq.cpp
    tests/test_c_api_quest.cpp
    tests/test_ba2.cpp
    tests/test_ba2_hardening.cpp
    tests/test_ba2_texture.cpp
    tests/test_localized_strings.cpp
    tests/test_c_api.cpp
    tests/test_environment.cpp
    tests/test_environment_world.cpp
    tests/test_environment_placed.cpp
    tests/test_environment_terrain.cpp
    tests/test_environment_strings.cpp
    tests/test_ba2_reader.cpp
    tests/test_ba2_real.cpp
    tests/test_environment_writeback.cpp
    tests/test_writeback_real.cpp
    tests/test_environment_ref_fields.cpp
    tests/test_environment_base_records.cpp
    tests/test_environment_material_swap.cpp
    tests/test_original_storage.cpp
    tests/test_env_bench_real.cpp
    tests/test_text_boundary.cpp
    tests/test_parity_dds.cpp
    tests/test_pex.cpp
    tests/test_pex_hardening.cpp
    tests/test_pex_corpus.cpp
    tests/test_pex_oracle.cpp
    tests/test_seq.cpp
    tests/test_seq_corpus.cpp
    tests/test_quest_runtime.cpp
    tools/env_bench/env_bench.cpp
    tests/plugin_bytes.cpp
  )
  target_include_directories(fo4_tests PRIVATE tests tools)
  target_link_libraries(fo4_tests PRIVATE fo4 radicle_c GTest::gtest_main ZLIB::ZLIB)
  target_compile_definitions(fo4_tests PRIVATE
    FO4_TESTDATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/testdata"
    _CRT_SECURE_NO_WARNINGS
  )
  target_compile_options(fo4_tests PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/wd4996>)
  fo4_apply_warnings(fo4_tests)
  fo4_apply_sanitizers(fo4_tests)
  target_sources(fo4_tests PRIVATE tests/test_c_api_version.cpp)

  include(GoogleTest)
  gtest_discover_tests(fo4_tests DISCOVERY_TIMEOUT 120)
  add_test(NAME radicle_field_address_codegen
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_field_address_codegen.py)
  if(RADICLE_BUILD_TOOLS)
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
  # ABI 4 is the last generation without the record payload and topic exports.
  add_library(radicle_abi_stub_v4 SHARED bindings/csharp/tests/abi_stub.cpp)
  target_include_directories(radicle_abi_stub_v4 PRIVATE include)
  target_compile_definitions(radicle_abi_stub_v4 PRIVATE RADICLE_C_BUILD RADICLE_ABI_STUB_VERSION=4u)

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
  endif()

  option(RADICLE_CSHARP_TESTS "Run the C# binding tests with the .NET 8 SDK and checked-in package feed" OFF)
  if(RADICLE_CSHARP_TESTS)
    find_program(DOTNET_EXECUTABLE dotnet REQUIRED)
    add_test(NAME radicle_csharp_bindings
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/bindings/csharp/tools/run_managed_tests.py
                     --dotnet ${DOTNET_EXECUTABLE})
    set_tests_properties(radicle_csharp_bindings PROPERTIES
      ENVIRONMENT "RADICLE_NATIVE_PATH=$<TARGET_FILE:radicle_c>;RADICLE_ABI_PROBE=$<TARGET_FILE:radicle_abi_layout_probe>;RADICLE_ABI_STUB=$<TARGET_FILE:radicle_abi_stub>;RADICLE_ABI_STUB_V1=$<TARGET_FILE:radicle_abi_stub_v1>;RADICLE_ABI_STUB_V4=$<TARGET_FILE:radicle_abi_stub_v4>")
  endif()
endif()
