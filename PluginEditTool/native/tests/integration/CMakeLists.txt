add_executable(fo4ide_test_http HttpServerTests.cpp)

target_link_libraries(
    fo4ide_test_http
    PRIVATE
        fo4ide_http
        fo4ide_rpc
        fo4ide_events
        httplib::httplib
        Catch2::Catch2WithMain
)

fo4ide_enable_warnings(fo4ide_test_http)
fo4ide_enable_sanitizers(fo4ide_test_http)
catch_discover_tests(fo4ide_test_http TEST_SPEC "~[large]" PROPERTIES TIMEOUT ${FO4IDE_TEST_TIMEOUT})
catch_discover_tests(fo4ide_test_http TEST_SPEC "[large]" PROPERTIES TIMEOUT 120)

fo4ide_use_utf8_code_page()
