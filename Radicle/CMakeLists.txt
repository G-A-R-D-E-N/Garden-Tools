cmake_minimum_required(VERSION 3.25)

project(fo4sdk
  VERSION 0.1.0
  DESCRIPTION "Radicle: native Fallout 4 plugin SDK"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(ZLIB REQUIRED)

option(FO4_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(FO4_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(RADICLE_BUILD_TOOLS "Build Radicle command-line tools" ${PROJECT_IS_TOP_LEVEL})
option(RADICLE_BUILD_TESTS "Build Radicle tests" ${PROJECT_IS_TOP_LEVEL})

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

function(fo4_apply_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive- /WX)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
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
  src/formid.cpp
  src/archive/ba2.cpp
  src/archive/ba2_hash.cpp
  src/archive/ba2_texture.cpp
  src/binary/cursor.cpp
  src/forms/armor.cpp
  src/forms/armor_edit.cpp
  src/forms/cell.cpp
  src/forms/placed_reference.cpp
  src/forms/placed_reference_copy.cpp
  src/forms/static_collection.cpp
  src/forms/eyes.cpp
  src/forms/leveled_spell.cpp
  src/forms/leveled_spell_creation.cpp
  src/forms/proj_expl_hazd.cpp
  src/forms/quest_aliases.cpp
  src/forms/quest_stages.cpp
  src/forms/quest_objectives.cpp
  src/forms/quest_alias_edit.cpp
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
  src/plugin/onam.cpp
  src/plugin/writer.cpp
  src/schema/ctda.cpp
  src/schema/form_links.cpp
  src/schema/localized_fields.cpp
  src/schema/formlink_lists.cpp
  src/schema/nested_formlink_lists.cpp
  src/schema/nested_formlink_list_null.cpp
  src/schema/preflight.cpp
  src/schema/scalar.cpp
  src/schema/vmad.cpp
  src/graph/conflict_graph.cpp
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

set_target_properties(fo4 PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(fo4
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(fo4 PRIVATE ZLIB::ZLIB)
fo4_apply_warnings(fo4)
fo4_apply_sanitizers(fo4)

add_library(radicle_c SHARED src/c_api.cpp)
target_compile_definitions(radicle_c PRIVATE RADICLE_C_BUILD)
target_link_libraries(radicle_c PRIVATE fo4)
set_target_properties(radicle_c PROPERTIES
  OUTPUT_NAME radicle
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR})
fo4_apply_warnings(radicle_c)
fo4_apply_sanitizers(radicle_c)

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

if(RADICLE_BUILD_TOOLS)
  add_executable(fo4inspect tools/inspect/main.cpp)
  target_link_libraries(fo4inspect PRIVATE fo4)
  fo4_apply_warnings(fo4inspect)
  fo4_apply_sanitizers(fo4inspect)
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
    tests/test_formlink_ordinals.cpp
    tests/test_rekey_formlinks.cpp
    tests/test_formlink_lists.cpp
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
    tests/test_quest_alias_formlink_remap.cpp
    tests/test_quest_alias_remaining_fills.cpp
    tests/test_scalar.cpp
    tests/test_preflight.cpp
    tests/test_ctda_strings.cpp
    tests/test_ctda_model.cpp
    tests/test_package_targets.cpp
    tests/test_perk_entrypoints.cpp
    tests/test_navmesh_formlinks.cpp
    tests/test_armor_creation.cpp
    tests/test_armor_edit.cpp
    tests/test_eyes.cpp
    tests/test_eyes_creation.cpp
    tests/test_lvsp.cpp
    tests/test_lvsp_creation.cpp
    tests/test_race_creation.cpp
    tests/test_race_body.cpp
    tests/test_onam.cpp
    tests/test_vmad.cpp
    tests/test_vmad_unsupported.cpp
    tests/test_conflict_graph.cpp
    tests/test_ba2.cpp
    tests/test_ba2_hardening.cpp
    tests/test_ba2_texture.cpp
    tests/test_localized_strings.cpp
    tests/test_c_api.cpp
    tests/test_original_storage.cpp
    tests/plugin_bytes.cpp
  )
  target_include_directories(fo4_tests PRIVATE tests)
  target_link_libraries(fo4_tests PRIVATE fo4 radicle_c GTest::gtest_main ZLIB::ZLIB)
  target_compile_definitions(fo4_tests PRIVATE
    FO4_TESTDATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/testdata"
  )
  fo4_apply_warnings(fo4_tests)
  fo4_apply_sanitizers(fo4_tests)

  include(GoogleTest)
  gtest_discover_tests(fo4_tests)
endif()
