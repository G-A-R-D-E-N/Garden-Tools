cmake_minimum_required(VERSION 3.25)

project(FO4IDENative VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

option(FO4IDE_BUILD_TESTS "Build native tests" ON)
option(FO4IDE_ENABLE_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)
option(FO4IDE_BUILD_HTTP "Build the native HTTP transport" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Dependencies)
include(CompilerWarnings)
include(Sanitizers)

# Radicle and the native backend tests both consume zlib. Resolve it in the
# top-level directory so the imported ZLIB::ZLIB target is visible to sibling
# native test directories as well as Radicle's subdirectory.
find_package(ZLIB REQUIRED)

set(RADICLE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../Radicle")
if(NOT EXISTS "${RADICLE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "Radicle sources not found at ${RADICLE_DIR}. "
        "PluginEditTool/native expects the sibling Radicle checkout from the same repository.")
endif()
set(RADICLE_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(RADICLE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory("${RADICLE_DIR}" "${CMAKE_BINARY_DIR}/radicle")

add_subdirectory(src)
add_subdirectory(apps/fo4ide)

if(FO4IDE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
