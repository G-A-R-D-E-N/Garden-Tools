cmake_minimum_required(VERSION 3.25)

project(FO4IDENative VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

if(WIN32)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

option(FO4IDE_BUILD_TESTS "Build native tests" ON)
option(FO4IDE_ENABLE_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)
option(FO4IDE_BUILD_HTTP "Build the native HTTP transport" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Dependencies)
include(CompilerWarnings)
include(Sanitizers)
include(Utf8CodePage)

# The cell geometry batch reads many meshes through Radicle on a bounded worker
# pool, so the app library uses std::thread (needs pthreads on POSIX).
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# Prefer a system zlib, but make the native app buildable on a clean Windows
# machine without vcpkg or another package manager. This is the same pinned
# revision used by the Windows cross-compile CI lane.
find_package(ZLIB QUIET)
if(NOT TARGET ZLIB::ZLIB)
    set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        fo4ide_zlib
        GIT_REPOSITORY https://github.com/madler/zlib.git
        GIT_TAG 51b7f2abdade71cd9bb0e7a373ef2610ec6f9daf
        GIT_SHALLOW FALSE
    )
    FetchContent_MakeAvailable(fo4ide_zlib)
    if(TARGET zlibstatic)
        add_library(ZLIB::ZLIB ALIAS zlibstatic)
    elseif(TARGET zlib)
        add_library(ZLIB::ZLIB ALIAS zlib)
    else()
        message(FATAL_ERROR "Fetched zlib did not define zlibstatic or zlib")
    endif()
endif()

set(RADICLE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../Radicle")
if(NOT EXISTS "${RADICLE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "Radicle sources not found at ${RADICLE_DIR}. "
        "PluginEditTool/native expects the sibling Radicle checkout from the same repository.")
endif()
# Not forced, so CI can build and test Radicle from this tree in one pass.
set(RADICLE_BUILD_TOOLS OFF CACHE BOOL "")
set(RADICLE_BUILD_TESTS OFF CACHE BOOL "")
set(RADICLE_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory("${RADICLE_DIR}" "${CMAKE_BINARY_DIR}/radicle")

# Radicle's radicle_c target is versioned (VERSION/SOVERSION), which makes
# CMake emit a `cmake_symlink_library` step to create libradicle.so.0 and
# libradicle.so. The build volume for this checkout is an ntfs3 mount that
# cannot create symbolic links, so that step fails on every configure. Unset
# the version properties here so only plain libradicle.so is produced; every
# consumer in this tree links via -lradicle, so the unversioned name is all
# that is needed. A shared Radicle build (RADICLE_SHARED) has no separate
# radicle_c target: the C ABI is compiled into fo4 itself.
if(TARGET radicle_c)
  set_property(TARGET radicle_c PROPERTY VERSION)
  set_property(TARGET radicle_c PROPERTY SOVERSION)
endif()

add_subdirectory(src)
add_subdirectory(apps/fo4ide)

if(FO4IDE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()