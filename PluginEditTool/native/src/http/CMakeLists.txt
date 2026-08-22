add_library(fo4ide_http HttpServer.cpp)

target_link_libraries(
    fo4ide_http
    PUBLIC
        fo4ide_core
        fo4ide_events
        fo4ide_rpc
    PRIVATE
        httplib::httplib
)

fo4ide_enable_warnings(fo4ide_http)
fo4ide_enable_sanitizers(fo4ide_http)
