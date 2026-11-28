function(fo4_add_test_executable target)
  add_executable(${target} ${ARGN})
  target_include_directories(${target} PRIVATE tests tools)
  target_link_libraries(${target} PRIVATE fo4 GTest::gtest_main ZLIB::ZLIB)
  if(TARGET radicle_c)
    target_link_libraries(${target} PRIVATE radicle_c)
  endif()
  target_compile_definitions(${target} PRIVATE
    FO4_TESTDATA_DIR="testdata"
    FO4_FLASH_SHIM_NAMES="${CMAKE_CURRENT_SOURCE_DIR}/bindings/csharp/Radicle.FlashShim/emitted_names.txt"
    FO4_ABC_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src/abc"
    _CRT_SECURE_NO_WARNINGS
  )
  if(RADICLE_TEST_HOOKS)
    target_compile_definitions(${target} PRIVATE FO4_TEST_HOOKS)
  endif()
  target_compile_options(${target} PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/wd4996>)
  fo4_apply_warnings(${target})
  fo4_apply_sanitizers(${target})
  gtest_discover_tests(${target} DISCOVERY_TIMEOUT 120
    TEST_FILTER "*:-GraphCompiler.GraphCompilerOracle*"
    PROPERTIES LABELS ${target} SKIP_REGULAR_EXPRESSION "\\[  SKIPPED \\]"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
endfunction()
