include(Catch)

function(fo4ide_add_test name source library)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE ${library} Catch2::Catch2WithMain)
    fo4ide_enable_warnings(${name})
    fo4ide_enable_sanitizers(${name})
    catch_discover_tests(${name})
endfunction()

fo4ide_add_test(fo4ide_test_events unit/EventBusTests.cpp fo4ide_events)
fo4ide_add_test(fo4ide_test_rpc unit/RpcDispatcherTests.cpp fo4ide_rpc)
fo4ide_add_test(fo4ide_test_settings unit/SettingsTests.cpp fo4ide_settings)
fo4ide_add_test(fo4ide_test_logging unit/LoggerTests.cpp fo4ide_logging)
fo4ide_add_test(fo4ide_test_nif unit/NifServiceTests.cpp fo4ide_nif)

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
catch_discover_tests(fo4ide_test_process)

if(FO4IDE_BUILD_HTTP)
    add_subdirectory(integration)
endif()
