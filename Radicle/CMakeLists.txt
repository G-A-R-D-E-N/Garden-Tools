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
option(RADICLE_BUILD_FUZZERS "Build the libFuzzer reader harness (MSVC needs FO4_ENABLE_ASAN)" OFF)
option(RADICLE_BUILD_TOOLS "Build Radicle command-line tools" ${PROJECT_IS_TOP_LEVEL})
option(RADICLE_BUILD_TESTS "Build Radicle tests" ${PROJECT_IS_TOP_LEVEL})
option(RADICLE_INSTALL "Generate Radicle install/export rules" ${PROJECT_IS_TOP_LEVEL})
option(RADICLE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ${PROJECT_IS_TOP_LEVEL})
option(RADICLE_SHARED "Build Radicle as one shared library that also carries the C ABI" OFF)
option(RADICLE_TEST_HOOKS "Compile the secure-snapshot test hooks into fo4 and the test executables" OFF)
option(RADICLE_CHECK_STRUCTURE "Fail configure when tools/check_project_structure.py finds drift (CI runs it either way)" ${PROJECT_IS_TOP_LEVEL})

find_package(Python3 COMPONENTS Interpreter REQUIRED)

if(RADICLE_CHECK_STRUCTURE)
  execute_process(
    COMMAND "${Python3_EXECUTABLE}" "-I" "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_project_structure.py"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    RESULT_VARIABLE radicle_structure_status
    OUTPUT_VARIABLE radicle_structure_stdout
    ERROR_VARIABLE radicle_structure_stderr
  )
  if(NOT radicle_structure_status EQUAL 0)
    message(FATAL_ERROR "Radicle structure guard failed:\n${radicle_structure_stdout}${radicle_structure_stderr}")
  endif()
  string(STRIP "${radicle_structure_stdout}" radicle_structure_summary)
  if(radicle_structure_summary)
    message(STATUS "${radicle_structure_summary}")
  endif()
endif()

add_custom_target(radicle_schema_check ALL
  COMMAND
    "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/schema_codegen.py"
    --schema "${CMAKE_CURRENT_SOURCE_DIR}/schema/fallout4/fields.json"
    --output "${CMAKE_CURRENT_SOURCE_DIR}/src/schema/generated/generated_scalar_fields.inc"
    --check
  COMMENT "Checking generated Fallout 4 scalar field schema"
  VERBATIM
)
add_custom_target(radicle_record_schema_check ALL
  COMMAND
    "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/record_schema_codegen.py"
    --schema "${CMAKE_CURRENT_SOURCE_DIR}/schema/fallout4/fields.json"
    --output "${CMAKE_CURRENT_SOURCE_DIR}/generated/fo4/schema/generated_record_constraints.inc"
    --check
  COMMENT "Checking generated Fallout 4 record authoring schema"
  VERBATIM
)
add_custom_target(radicle_lstring_schema_check ALL
  COMMAND
    "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/lstring_schema_codegen.py"
    --schema "${CMAKE_CURRENT_SOURCE_DIR}/schema/fallout4/fields.json"
    --output "${CMAKE_CURRENT_SOURCE_DIR}/generated/fo4/schema/generated_lstring_fields.inc"
    --check
  COMMENT "Checking generated Fallout 4 localized field schema"
  VERBATIM
)
add_custom_target(radicle_formlink_schema_check ALL
  COMMAND
    "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/formlink_schema_codegen.py"
    --schema "${CMAKE_CURRENT_SOURCE_DIR}/schema/fallout4/fields.json"
    --output "${CMAKE_CURRENT_SOURCE_DIR}/generated/fo4/schema/generated_formlink_lists.inc"
    --check
  COMMENT "Checking generated Fallout 4 FormLink list schema"
  VERBATIM
)
add_custom_target(radicle_condition_scope_schema_check ALL
  COMMAND
    "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/condition_scope_codegen.py"
    --schema "${CMAKE_CURRENT_SOURCE_DIR}/schema/fallout4/fields.json"
    --output "${CMAKE_CURRENT_SOURCE_DIR}/generated/fo4/schema/generated_condition_scopes.inc"
    --check
  COMMENT "Checking generated Fallout 4 condition scope schema"
  VERBATIM
)
add_custom_target(radicle_record_flag_schema_check ALL
  COMMAND
    "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/record_flag_codegen.py"
    --schema "${CMAKE_CURRENT_SOURCE_DIR}/schema/fallout4/fields.json"
    --output "${CMAKE_CURRENT_SOURCE_DIR}/generated/fo4/schema/generated_record_flags.inc"
    --check
  COMMENT "Checking generated Fallout 4 record flag schema"
  VERBATIM
)
add_custom_target(radicle_condition_function_schema_check ALL
  COMMAND
    "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/condition_function_codegen.py"
    --schema "${CMAKE_CURRENT_SOURCE_DIR}/schema/fallout4/condition_functions.json"
    --output "${CMAKE_CURRENT_SOURCE_DIR}/src/schema/generated/generated_condition_functions.inc"
    --ctda-output "${CMAKE_CURRENT_SOURCE_DIR}/src/schema/generated/ctda_functions.inc"
    --check
  COMMENT "Checking generated Fallout 4 condition function table"
  VERBATIM
)
add_custom_target(radicle_formlink_tables_check ALL
  COMMAND
    "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/form_link_tables_codegen.py"
    --schema "${CMAKE_CURRENT_SOURCE_DIR}/schema/fallout4/fields.json"
    --output-dir "${CMAKE_CURRENT_SOURCE_DIR}/src/schema/generated"
    --check
  COMMENT "Checking generated Fallout 4 global FormLink tables"
  VERBATIM
)
add_custom_target(radicle_xedit_conflict_priority_check ALL
  COMMAND
    "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/xedit_conflict_priority_codegen.py"
    --marks "${CMAKE_CURRENT_SOURCE_DIR}/schema/fallout4/xedit_conflict_priority_marks.json"
    --output "${CMAKE_CURRENT_SOURCE_DIR}/src/checks/xedit_conflict_priorities.inc"
    --unmapped "${CMAKE_CURRENT_SOURCE_DIR}/schema/fallout4/xedit_conflict_unmapped.json"
    --fields "${CMAKE_CURRENT_SOURCE_DIR}/schema/fallout4/fields.json"
    --check
  COMMENT "Checking generated xEdit conflict priority table"
  VERBATIM
)

add_custom_target(radicle_hkx_layout_check ALL
  COMMAND
    "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/hkx_layout_codegen.py"
    --layouts "${CMAKE_CURRENT_SOURCE_DIR}/data/hkx/HavokClassLayouts.json"
    --types "${CMAKE_CURRENT_SOURCE_DIR}/data/hkx/HavokClassTypes.json"
    --output "${CMAKE_CURRENT_SOURCE_DIR}/src/hkx/hkx_layouts.generated.inc"
    --check
  COMMENT "Checking the generated Havok class layout table"
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
  if(FO4_ENABLE_ASAN AND MSVC)
    # MSVC ASan rejects /RTC and wants /Zi; use a non-Debug config (RelWithDebInfo) for it.
    target_compile_options(${target} PRIVATE /fsanitize=address /Zi)
    target_link_options(${target} PRIVATE /INCREMENTAL:NO)
  elseif(FO4_ENABLE_ASAN)
    target_compile_options(${target} PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE -fsanitize=address)
  endif()
  if(FO4_ENABLE_UBSAN AND NOT MSVC)
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

include(${CMAKE_CURRENT_LIST_DIR}/cmake/sources_core.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/sources_plugin.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/sources_schema.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/sources_forms.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/sources_environment.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/sources_assets.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/sources_papyrus.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/sources_flash.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/precompiled_headers.cmake)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  include(GNUInstallDirs)
  list(REMOVE_ITEM FO4_SOURCES_CORE src/text/bounded_regex.cpp)
  list(APPEND FO4_SOURCES_CORE src/text/bounded_regex_linux.cpp)
  add_executable(radicle-regex-worker src/text/bounded_regex_worker.cpp)
  fo4_apply_warnings(radicle-regex-worker)
  target_link_libraries(radicle-regex-worker PRIVATE rt)
  find_package(Threads REQUIRED)
  add_library(radicle-regex-runtime SHARED src/text/bounded_regex_runtime.cpp)
  target_link_libraries(radicle-regex-runtime PRIVATE Threads::Threads)
  target_link_options(radicle-regex-runtime PRIVATE -Wl,-z,now -Wl,-z,nodelete)
  set_target_properties(radicle-regex-runtime PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:radicle-regex-worker>")
  fo4_apply_warnings(radicle-regex-runtime)
endif()

add_library(fo4 ${FO4_LIBRARY_KIND}
  ${FO4_SOURCES_CORE}
  ${FO4_SOURCES_PLUGIN}
  ${FO4_SOURCES_SCHEMA}
  ${FO4_SOURCES_FORMS}
  ${FO4_SOURCES_ENVIRONMENT}
  ${FO4_SOURCES_ASSETS}
  ${FO4_SOURCES_PAPYRUS}
  ${FO4_SOURCES_FLASH}
)
fo4_use_pch(fo4 ${FO4_PCH_HEADERS})

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
if(TARGET radicle_record_flag_schema_check)
  add_dependencies(fo4 radicle_record_flag_schema_check)
endif()
if(TARGET radicle_condition_function_schema_check)
  add_dependencies(fo4 radicle_condition_function_schema_check)
endif()
if(TARGET radicle_formlink_tables_check)
  add_dependencies(fo4 radicle_formlink_tables_check)
endif()
if(TARGET radicle_hkx_layout_check)
  add_dependencies(fo4 radicle_hkx_layout_check)
endif()

set_target_properties(fo4 PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(TARGET radicle-regex-worker)
  add_dependencies(fo4 radicle-regex-worker radicle-regex-runtime)
  target_link_libraries(fo4 PRIVATE ${CMAKE_DL_LIBS})
  target_compile_definitions(fo4 PRIVATE RADICLE_REGEX_WORKER_INSTALL_PATH="${CMAKE_INSTALL_FULL_BINDIR}/radicle-regex-worker")
endif()

target_include_directories(fo4
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/generated>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(fo4 PRIVATE ZLIB::ZLIB)
if(WIN32)
  target_link_libraries(fo4 PRIVATE ntdll advapi32)
endif()
if(RADICLE_BUILD_TESTS AND RADICLE_TEST_HOOKS)
  target_compile_definitions(fo4 PRIVATE FO4_TEST_HOOKS)
endif()
target_link_libraries(fo4 PUBLIC $<BUILD_INTERFACE:nlohmann_json::nlohmann_json>)
fo4_apply_warnings(fo4)
fo4_apply_sanitizers(fo4)

include(${CMAKE_CURRENT_LIST_DIR}/cmake/sources_c_api.cmake)

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
  if(TARGET radicle-regex-worker)
    install(TARGETS radicle-regex-worker RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(TARGETS radicle-regex-runtime LIBRARY DESTINATION ${CMAKE_INSTALL_BINDIR})
  endif()
  install(DIRECTORY include/fo4 DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
  install(DIRECTORY generated/fo4 DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
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
  add_executable(previs_oracle_extract tools/previs_oracle_extract/main.cpp)
  target_link_libraries(previs_oracle_extract PRIVATE fo4)
  fo4_apply_warnings(previs_oracle_extract)
  fo4_apply_sanitizers(previs_oracle_extract)
  add_executable(vanilla_broken_tsv tools/previs/vanilla_broken_tsv.cpp)
  target_link_libraries(vanilla_broken_tsv PRIVATE fo4)
  fo4_apply_warnings(vanilla_broken_tsv)
  fo4_apply_sanitizers(vanilla_broken_tsv)
  add_executable(radicle_env_bench tools/env_bench/main.cpp tools/env_bench/env_bench.cpp)
  target_include_directories(radicle_env_bench PRIVATE tools)
  target_link_libraries(radicle_env_bench PRIVATE fo4)
  fo4_apply_warnings(radicle_env_bench)
  fo4_apply_sanitizers(radicle_env_bench)
  add_executable(radicle_parity_dump tools/parity/main.cpp)
  target_include_directories(radicle_parity_dump PRIVATE tools)
  target_link_libraries(radicle_parity_dump PRIVATE fo4)
  fo4_apply_warnings(radicle_parity_dump)
  fo4_apply_sanitizers(radicle_parity_dump)
  add_executable(radicle_parity_writeback tools/parity/writeback.cpp)
  target_include_directories(radicle_parity_writeback PRIVATE tools)
  target_link_libraries(radicle_parity_writeback PRIVATE fo4)
  if(TARGET radicle_c)
    target_link_libraries(radicle_parity_writeback PRIVATE radicle_c)
  endif()
  fo4_apply_warnings(radicle_parity_writeback)
  fo4_apply_sanitizers(radicle_parity_writeback)
endif()

if(RADICLE_BUILD_FUZZERS)
  add_executable(radicle_fuzz_readers tools/fuzz/fuzz_readers.cpp)
  target_link_libraries(radicle_fuzz_readers PRIVATE fo4)
  if(MSVC)
    # libFuzzer on MSVC needs ASan and coverage counters in the readers (fo4) as well as the harness.
    target_compile_options(fo4 PRIVATE /fsanitize=fuzzer)
    target_compile_options(radicle_fuzz_readers PRIVATE /fsanitize=fuzzer)
  else()
    target_compile_options(fo4 PRIVATE -fsanitize=fuzzer-no-link)
    target_compile_options(radicle_fuzz_readers PRIVATE -fsanitize=fuzzer)
    target_link_options(radicle_fuzz_readers PRIVATE -fsanitize=fuzzer)
  endif()
  fo4_apply_warnings(radicle_fuzz_readers)
  fo4_apply_sanitizers(radicle_fuzz_readers)
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
  if(FO4_ENABLE_ASAN AND MSVC)
    # gtest must carry the same STL annotation setting as the instrumented targets (LNK2038 otherwise).
    add_compile_options(/fsanitize=address /Zi)
  endif()
  FetchContent_MakeAvailable(googletest)

  include(${CMAKE_CURRENT_LIST_DIR}/cmake/tests_support.cmake)
  include(${CMAKE_CURRENT_LIST_DIR}/cmake/tests_core.cmake)
  include(${CMAKE_CURRENT_LIST_DIR}/cmake/tests_papyrus.cmake)
  include(${CMAKE_CURRENT_LIST_DIR}/cmake/tests_flash.cmake)
  include(${CMAKE_CURRENT_LIST_DIR}/cmake/tests_assets.cmake)

  include(GoogleTest)
  include(${CMAKE_CURRENT_LIST_DIR}/cmake/test_executable.cmake)
  fo4_add_test_executable(fo4_tests_core ${FO4_TESTS_CORE} ${FO4_TEST_SUPPORT_SOURCES})
  if(TARGET radicle-regex-runtime)
    target_compile_definitions(fo4_tests_core PRIVATE
      RADICLE_REGEX_RUNTIME_TEST_PATH="$<TARGET_FILE:radicle-regex-runtime>"
      RADICLE_REGEX_WORKER_TEST_PATH="$<TARGET_FILE:radicle-regex-worker>")
    target_link_libraries(fo4_tests_core PRIVATE ${CMAKE_DL_LIBS})
  endif()
  fo4_add_test_executable(fo4_tests_papyrus ${FO4_TESTS_PAPYRUS} ${FO4_TEST_SUPPORT_SOURCES})
  fo4_use_pch(fo4_tests_papyrus ${FO4_TEST_PCH_HEADERS})
  fo4_reuse_pch(fo4_tests_core fo4_tests_papyrus)
  fo4_add_test_executable(fo4_tests_flash ${FO4_TESTS_FLASH} ${FO4_TEST_SUPPORT_SOURCES})
  fo4_reuse_pch(fo4_tests_flash fo4_tests_papyrus)
  fo4_add_test_executable(fo4_tests_assets ${FO4_TESTS_ASSETS} ${FO4_TEST_SUPPORT_SOURCES})
  fo4_reuse_pch(fo4_tests_assets fo4_tests_papyrus)
  # The GraphCompiler Oracle cases must keep PET's 60 s limit; an unbounded scratch sweep stalls them.
  gtest_discover_tests(fo4_tests_papyrus DISCOVERY_TIMEOUT 120
    TEST_FILTER "GraphCompiler.GraphCompilerOracle*"
    PROPERTIES LABELS fo4_tests_papyrus TIMEOUT 60 SKIP_REGULAR_EXPRESSION "\\[  SKIPPED \\]"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
  add_custom_target(fo4_tests)
  add_dependencies(fo4_tests fo4_tests_core fo4_tests_papyrus fo4_tests_flash fo4_tests_assets)
  add_test(NAME radicle_field_address_codegen
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_field_address_codegen.py)
  add_test(NAME radicle_counted_array_segments
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_counted_array_segments.py)
  add_test(NAME radicle_run_rows_codegen
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_run_rows_codegen.py)
  add_test(NAME radicle_subrecord_group_codegen
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_subrecord_group_codegen.py)
  add_test(NAME radicle_subrecord_group_string_rows
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_subrecord_group_string_rows.py)
  add_test(NAME radicle_field_address_version_windows
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_field_address_version_windows.py)
  add_test(NAME radicle_parity_matrix_unit COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_parity_matrix.py)
  add_test(NAME radicle_condition_function_codegen
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_condition_function_codegen.py)
  add_test(NAME radicle_formlink_tables_codegen
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_form_link_tables_codegen.py)
  add_test(NAME radicle_hkx_layout_codegen
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_hkx_layout_codegen.py)
  add_test(NAME radicle_formlink_list_storage
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_formlink_list_storage.py)
  add_test(NAME radicle_localized_row_codegen
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_localized_row_codegen.py)
  add_test(NAME radicle_max_form_version
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_max_form_version.py)
  add_test(NAME radicle_xedit_conflict_marks
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_xedit_conflict_marks.py)
  add_test(NAME radicle_xedit_count_coverage
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_xedit_count_coverage.py)
  add_test(NAME radicle_record_flag_codegen
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_record_flag_codegen.py)
  add_test(NAME radicle_project_structure_guard
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_check_project_structure.py)
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

  # Stubs report another major, ABI 0.1 (before the material swap exports) and a lower minor, all rejected, and a higher minor, accepted.
  add_library(radicle_abi_stub SHARED bindings/csharp/tests/abi_stub.cpp)
  target_include_directories(radicle_abi_stub PRIVATE include)
  target_compile_definitions(radicle_abi_stub PRIVATE RADICLE_C_BUILD "RADICLE_ABI_STUB_MAJOR=RADICLE_ABI_MAJOR+1u" "RADICLE_ABI_STUB_MINOR=RADICLE_ABI_MINOR")
  add_library(radicle_abi_stub_v1 SHARED bindings/csharp/tests/abi_stub.cpp)
  target_include_directories(radicle_abi_stub_v1 PRIVATE include)
  target_compile_definitions(radicle_abi_stub_v1 PRIVATE RADICLE_C_BUILD RADICLE_ABI_STUB_MAJOR=0u RADICLE_ABI_STUB_MINOR=1u)
  add_library(radicle_abi_stub_legacy SHARED bindings/csharp/tests/abi_stub_legacy.cpp)
  target_include_directories(radicle_abi_stub_legacy PRIVATE include)
  target_compile_definitions(radicle_abi_stub_legacy PRIVATE RADICLE_C_BUILD)
  add_library(radicle_abi_stub_older_minor SHARED bindings/csharp/tests/abi_stub.cpp)
  target_include_directories(radicle_abi_stub_older_minor PRIVATE include)
  target_compile_definitions(radicle_abi_stub_older_minor PRIVATE RADICLE_C_BUILD "RADICLE_ABI_STUB_MAJOR=RADICLE_ABI_MAJOR" "RADICLE_ABI_STUB_MINOR=RADICLE_ABI_MINOR-1u")
  add_library(radicle_abi_stub_newer_minor SHARED bindings/csharp/tests/abi_stub.cpp)
  target_include_directories(radicle_abi_stub_newer_minor PRIVATE include)
  target_compile_definitions(radicle_abi_stub_newer_minor PRIVATE RADICLE_C_BUILD "RADICLE_ABI_STUB_MAJOR=RADICLE_ABI_MAJOR" "RADICLE_ABI_STUB_MINOR=RADICLE_ABI_MINOR+1u")

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
    add_test(NAME radicle_csharp_interop_header_discovery
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/bindings/csharp/tools/test_generate_interop.py)
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
      ENVIRONMENT "RADICLE_NATIVE_PATH=$<TARGET_FILE:${RADICLE_C_LIBRARY_TARGET}>;RADICLE_ABI_PROBE=$<TARGET_FILE:radicle_abi_layout_probe>;RADICLE_ABI_STUB=$<TARGET_FILE:radicle_abi_stub>;RADICLE_ABI_STUB_V1=$<TARGET_FILE:radicle_abi_stub_v1>;RADICLE_ABI_STUB_LEGACY=$<TARGET_FILE:radicle_abi_stub_legacy>;RADICLE_ABI_STUB_OLDER_MINOR=$<TARGET_FILE:radicle_abi_stub_older_minor>;RADICLE_ABI_STUB_NEWER_MINOR=$<TARGET_FILE:radicle_abi_stub_newer_minor>")
  endif()
endif()
