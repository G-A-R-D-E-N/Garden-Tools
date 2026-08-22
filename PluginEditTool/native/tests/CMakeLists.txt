include(Catch)

function(fo4ide_add_test name source library)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE ${library} Catch2::Catch2WithMain)
    fo4ide_enable_warnings(${name})
    fo4ide_enable_sanitizers(${name})
    catch_discover_tests(${name} PROPERTIES TIMEOUT 10)
endfunction()

fo4ide_add_test(fo4ide_test_events unit/EventBusTests.cpp fo4ide_events)
fo4ide_add_test(fo4ide_test_rpc unit/RpcDispatcherTests.cpp fo4ide_rpc)
fo4ide_add_test(fo4ide_test_mcp unit/McpServerTests.cpp fo4ide_mcp)
fo4ide_add_test(fo4ide_test_settings unit/SettingsTests.cpp fo4ide_settings)
fo4ide_add_test(fo4ide_test_logging unit/LoggerTests.cpp fo4ide_logging)
fo4ide_add_test(fo4ide_test_nif unit/NifServiceTests.cpp fo4ide_nif)
fo4ide_add_test(fo4ide_test_material unit/MaterialServiceTests.cpp fo4ide_material)
fo4ide_add_test(fo4ide_test_service_targets unit/ServiceTargetsTests.cpp fo4ide_app)
fo4ide_add_test(fo4ide_test_plugin_backend_target unit/PluginBackendTargetTests.cpp fo4ide_app)
fo4ide_add_test(fo4ide_test_mo2 unit/Mo2ProfileTests.cpp fo4ide_mo2)
fo4ide_add_test(fo4ide_test_app_interop unit/AppInteropTargetTests.cpp fo4ide_app)
fo4ide_add_test(fo4ide_test_tools unit/ToolLocatorTests.cpp fo4ide_tools)

add_executable(
    fo4ide_test_plugin_backend
    unit/RadiclePluginBackendTests.cpp
    unit/RadiclePluginMastersTests.cpp
    unit/RadiclePluginMutationTests.cpp
    unit/RadiclePluginLifecycleTests.cpp
    unit/RadiclePluginScalarTests.cpp
    unit/RadiclePluginEyesTests.cpp
    unit/RadiclePluginEyesGridTests.cpp
    unit/RadiclePluginLvspTests.cpp
    unit/RadiclePluginLvspGridTests.cpp
    unit/RadiclePluginBatchMutationTests.cpp
    unit/RadiclePluginAddMastersTests.cpp
    unit/RadiclePluginReadApiTests.cpp
    unit/RadiclePluginEslTests.cpp
    unit/RadiclePluginEditableTests.cpp
    "${PROJECT_SOURCE_DIR}/../../Radicle/tests/plugin_bytes.cpp"
)
target_include_directories(
    fo4ide_test_plugin_backend
    PRIVATE "${PROJECT_SOURCE_DIR}/../../Radicle/tests"
)
target_link_libraries(
    fo4ide_test_plugin_backend
    PRIVATE fo4ide_plugin Catch2::Catch2WithMain ZLIB::ZLIB
)
fo4ide_enable_warnings(fo4ide_test_plugin_backend)
fo4ide_enable_sanitizers(fo4ide_test_plugin_backend)
catch_discover_tests(fo4ide_test_plugin_backend PROPERTIES TIMEOUT 10)

add_executable(
    fo4ide_test_archive_backend
    unit/RadicleArchiveBackendTests.cpp
)
target_link_libraries(
    fo4ide_test_archive_backend
    PRIVATE fo4ide_archive fo4 Catch2::Catch2WithMain
)
fo4ide_enable_warnings(fo4ide_test_archive_backend)
fo4ide_enable_sanitizers(fo4ide_test_archive_backend)
catch_discover_tests(fo4ide_test_archive_backend PROPERTIES TIMEOUT 10)

add_executable(fo4ide_fake_process helpers/fake_process.cpp)
fo4ide_enable_warnings(fo4ide_fake_process)
fo4ide_enable_sanitizers(fo4ide_fake_process)

add_executable(fo4ide_test_process unit/ProcessRunnerTests.cpp)
target_link_libraries(fo4ide_test_process PRIVATE fo4ide_process Catch2::Catch2WithMain)
target_compile_definitions(
    fo4ide_test_process
    PRIVATE FO4IDE_FAKE_PROCESS_PATH="$<TARGET_FILE:fo4ide_fake_process>"
)
add_dependencies(fo4ide_test_process fo4ide_fake_process)
fo4ide_enable_warnings(fo4ide_test_process)
fo4ide_enable_sanitizers(fo4ide_test_process)
catch_discover_tests(fo4ide_test_process PROPERTIES TIMEOUT 10)

if(FO4IDE_BUILD_HTTP)
    add_subdirectory(integration)
endif()
