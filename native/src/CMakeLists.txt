add_library(fo4ide_core INTERFACE)
target_include_directories(fo4ide_core INTERFACE "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(fo4ide_core INTERFACE nlohmann_json::nlohmann_json)

add_library(fo4ide_plugin_api INTERFACE)
target_link_libraries(fo4ide_plugin_api INTERFACE fo4ide_core)

add_library(fo4ide_logging logging/Logger.cpp)
target_link_libraries(fo4ide_logging PUBLIC fo4ide_core spdlog::spdlog)
fo4ide_enable_warnings(fo4ide_logging)
fo4ide_enable_sanitizers(fo4ide_logging)

add_library(fo4ide_settings settings/Settings.cpp)
target_link_libraries(fo4ide_settings PUBLIC fo4ide_core nlohmann_json::nlohmann_json)
fo4ide_enable_warnings(fo4ide_settings)
fo4ide_enable_sanitizers(fo4ide_settings)

add_library(fo4ide_process process/ProcessRunner.cpp)
target_link_libraries(fo4ide_process PUBLIC fo4ide_core PRIVATE reproc++)
fo4ide_enable_warnings(fo4ide_process)
fo4ide_enable_sanitizers(fo4ide_process)

add_library(fo4ide_platform platform/PlatformServices.cpp)
target_link_libraries(fo4ide_platform PUBLIC fo4ide_core fo4ide_process)
if(WIN32)
    target_link_libraries(fo4ide_platform PRIVATE ole32 shell32 uuid)
endif()
fo4ide_enable_warnings(fo4ide_platform)
fo4ide_enable_sanitizers(fo4ide_platform)

add_library(fo4ide_mo2 mo2/Mo2Profile.cpp)
target_link_libraries(fo4ide_mo2 PUBLIC fo4ide_core)
fo4ide_enable_warnings(fo4ide_mo2)
fo4ide_enable_sanitizers(fo4ide_mo2)

add_library(fo4ide_events events/EventBus.cpp)
target_link_libraries(fo4ide_events PUBLIC fo4ide_core nlohmann_json::nlohmann_json)
fo4ide_enable_warnings(fo4ide_events)
fo4ide_enable_sanitizers(fo4ide_events)

add_library(fo4ide_rpc rpc/RpcDispatcher.cpp)
target_link_libraries(fo4ide_rpc PUBLIC fo4ide_core nlohmann_json::nlohmann_json)
fo4ide_enable_warnings(fo4ide_rpc)
fo4ide_enable_sanitizers(fo4ide_rpc)

add_library(fo4ide_nif assets/NifService.cpp)
target_link_libraries(fo4ide_nif PUBLIC fo4ide_core fo4ide_process nlohmann_json::nlohmann_json)
fo4ide_enable_warnings(fo4ide_nif)
fo4ide_enable_sanitizers(fo4ide_nif)

add_library(fo4ide_app app/ServiceTargets.cpp app/AppInteropTarget.cpp)
target_link_libraries(fo4ide_app PUBLIC fo4ide_core fo4ide_rpc fo4ide_settings fo4ide_nif fo4ide_platform fo4ide_process fo4ide_mo2)
fo4ide_enable_warnings(fo4ide_app)
fo4ide_enable_sanitizers(fo4ide_app)

if(FO4IDE_BUILD_HTTP)
    add_subdirectory(http)
endif()
