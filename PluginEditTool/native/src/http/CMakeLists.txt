add_library(fo4ide_remote ../rpc/RemoteTarget.cpp)

target_link_libraries(
    fo4ide_remote
    PUBLIC
        fo4ide_core
        fo4ide_rpc
        nlohmann_json::nlohmann_json
    PRIVATE
        httplib::httplib
)

fo4ide_enable_warnings(fo4ide_remote)

add_library(fo4ide_http HttpServer.cpp)

target_link_libraries(
    fo4ide_http
    PUBLIC
        fo4ide_core
        fo4ide_events
        fo4ide_rpc
        fo4ide_remote
    PRIVATE
        httplib::httplib
)

fo4ide_enable_warnings(fo4ide_http)
fo4ide_enable_sanitizers(fo4ide_http)
