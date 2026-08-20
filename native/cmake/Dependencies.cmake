include(FetchContent)

set(FETCHCONTENT_QUIET OFF)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.12.0
    GIT_SHALLOW TRUE
)

FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.17.0
    GIT_SHALLOW TRUE
)

set(REPROC++ ON CACHE BOOL "" FORCE)
set(REPROC_TEST OFF CACHE BOOL "" FORCE)
set(REPROC_EXAMPLES OFF CACHE BOOL "" FORCE)
set(REPROC_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    reproc
    GIT_REPOSITORY https://github.com/DaanDeMeyer/reproc.git
    GIT_TAG v14.2.7
    GIT_SHALLOW TRUE
)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.15.3
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(nlohmann_json spdlog reproc)

if(FO4IDE_BUILD_TESTS)
    FetchContent_MakeAvailable(Catch2)
    list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
endif()

if(FO4IDE_BUILD_HTTP)
    find_package(Boost 1.88 REQUIRED COMPONENTS system)
endif()
