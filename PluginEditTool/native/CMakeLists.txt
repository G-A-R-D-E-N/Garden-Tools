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

set(RADICLE_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(RADICLE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../Radicle" "${CMAKE_BINARY_DIR}/radicle")

add_subdirectory(src)
add_subdirectory(apps/fo4ide)

if(FO4IDE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
