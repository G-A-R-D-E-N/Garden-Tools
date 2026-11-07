include(FetchContent)

set(FETCHCONTENT_QUIET OFF)

FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
    INACTIVITY_TIMEOUT 60
)

FetchContent_Declare(
    spdlog
    URL https://github.com/gabime/spdlog/archive/refs/tags/v1.17.0.tar.gz
    URL_HASH SHA256=d8862955c6d74e5846b3f580b1605d2428b11d97a410d86e2fb13e857cd3a744
    INACTIVITY_TIMEOUT 60
)

set(REPROC++ ON CACHE BOOL "" FORCE)
set(REPROC_TEST OFF CACHE BOOL "" FORCE)
set(REPROC_EXAMPLES OFF CACHE BOOL "" FORCE)
set(REPROC_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    reproc
    URL https://github.com/DaanDeMeyer/reproc/archive/refs/tags/v14.2.7.tar.gz
    URL_HASH SHA256=77914ba566356d86986db240ecdb1762dfdd55bb1e8e127d016d9c5c4c9300e8
    INACTIVITY_TIMEOUT 60
)

FetchContent_Declare(
    Catch2
    URL https://github.com/catchorg/Catch2/archive/refs/tags/v3.15.3.tar.gz
    URL_HASH SHA256=b0299ae552918220a7a6e21e7de5b714777f4e8c883fb70c4bb23fe01df8c6e3
    INACTIVITY_TIMEOUT 60
)

FetchContent_MakeAvailable(nlohmann_json spdlog reproc)

if(FO4IDE_BUILD_TESTS)
    FetchContent_MakeAvailable(Catch2)
    list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
endif()

if(FO4IDE_BUILD_HTTP)
    set(HTTPLIB_INSTALL OFF CACHE BOOL "" FORCE)
    set(HTTPLIB_TEST OFF CACHE BOOL "" FORCE)
    set(HTTPLIB_USE_OPENSSL_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
    set(HTTPLIB_USE_WOLFSSL_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
    set(HTTPLIB_USE_MBEDTLS_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
    set(HTTPLIB_USE_ZLIB_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
    set(HTTPLIB_USE_BROTLI_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
    set(HTTPLIB_USE_ZSTD_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        httplib
        URL https://github.com/yhirose/cpp-httplib/archive/refs/tags/v0.51.0.tar.gz
        URL_HASH SHA256=d740ced75352f44e9d66d08806dc231b5621bb3592b5a5d5b2bd890a9a9d86cc
        INACTIVITY_TIMEOUT 60
    )
    FetchContent_MakeAvailable(httplib)
endif()

if(WIN32)
    set(FO4IDE_WEBVIEW2_VERSION "1.0.4129.50" CACHE STRING "Pinned Microsoft WebView2 SDK version")
    FetchContent_Declare(
        webview2_sdk
        URL "https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/${FO4IDE_WEBVIEW2_VERSION}"
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        INACTIVITY_TIMEOUT 60
    )
    FetchContent_MakeAvailable(webview2_sdk)
    set(FO4IDE_WEBVIEW2_INCLUDE_DIR "${webview2_sdk_SOURCE_DIR}/build/native/include" CACHE INTERNAL "")
    set(FO4IDE_WEBVIEW2_LOADER_LIB "${webview2_sdk_SOURCE_DIR}/build/native/x64/WebView2LoaderStatic.lib" CACHE INTERNAL "")
endif()
