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

function(fo4_apply_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive-)
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
  src/binary/cursor.cpp
  src/plugin/tes4.cpp
  src/plugin/reader.cpp
  src/plugin/master_kinds.cpp
  src/plugin/load_order.cpp
  src/plugin/flags.cpp
  src/plugin/mutation.cpp
  src/plugin/writer.cpp
  src/schema/form_links.cpp
  src/schema/vmad.cpp
  src/graph/conflict_graph.cpp
)

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
set_target_properties(radicle_c PROPERTIES OUTPUT_NAME radicle)
fo4_apply_warnings(radicle_c)
fo4_apply_sanitizers(radicle_c)

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
    tests/test_writer.cpp
    tests/test_formlinks.cpp
    tests/test_ctda_strings.cpp
    tests/test_package_targets.cpp
    tests/test_vmad.cpp
    tests/test_vmad_unsupported.cpp
    tests/test_conflict_graph.cpp
    tests/test_c_api.cpp
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
