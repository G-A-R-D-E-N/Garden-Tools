cmake_minimum_required(VERSION 3.25)

project(fo4sdk
  VERSION 0.1.0
  DESCRIPTION "Radicle: native Fallout 4 plugin SDK"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Standalone Radicle builds resolve zlib normally. Parent projects may provide
# the canonical imported target themselves, which lets PluginEditTool fetch a
# pinned fallback on Windows without requiring a machine-wide zlib install.
if(NOT TARGET ZLIB::ZLIB)
  find_package(ZLIB REQUIRED)
endif()

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
  src/forms/quest.cpp
  src/graph/conflict_graph.cpp
  src/io/atomic_write.cpp
  src/plugin/header.cpp
  src/plugin/mutation.cpp
  src/plugin/onam.cpp
  src/plugin/reader.cpp
  src/plugin/visit.cpp
  src/plugin/writer.cpp
  src/schema/ctda.cpp
  src/schema/form_links.cpp
  src/schema/formlink_lists.cpp
  src/schema/lstring.cpp
  src/schema/nested_formlink_list_null.cpp
  src/schema/nested_formlink_lists.cpp
  src/schema/record_constraints.cpp
  src/schema/scalar.cpp
  src/schema/vmad.cpp
)

target_include_directories(fo4 PUBLIC include)
target_link_libraries(fo4 PUBLIC ZLIB::ZLIB)
fo4_apply_warnings(fo4)
fo4_apply_sanitizers(fo4)
add_dependencies(
  fo4
  radicle_schema_check
  radicle_record_schema_check
  radicle_lstring_schema_check
  radicle_formlink_schema_check
)

add_library(radicle_c SHARED src/c_api.cpp)
target_include_directories(radicle_c PUBLIC include)
target_link_libraries(radicle_c PRIVATE fo4)
set_target_properties(radicle_c PROPERTIES
  OUTPUT_NAME radicle
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR})
fo4_apply_warnings(radicle_c)
fo4_apply_sanitizers(radicle_c)

if(RADICLE_BUILD_TOOLS)
  add_subdirectory(tools)
endif()

if(RADICLE_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()