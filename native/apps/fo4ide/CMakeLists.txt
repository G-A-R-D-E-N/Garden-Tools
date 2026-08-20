add_executable(fo4ide main.cpp)

target_link_libraries(
    fo4ide
    PRIVATE
        fo4ide_core
        fo4ide_logging
        fo4ide_settings
        fo4ide_process
        fo4ide_events
        fo4ide_rpc
        fo4ide_nif
        fo4ide_plugin_api
)

fo4ide_enable_warnings(fo4ide)
fo4ide_enable_sanitizers(fo4ide)

set_target_properties(fo4ide PROPERTIES OUTPUT_NAME "FO4IDE-native")
