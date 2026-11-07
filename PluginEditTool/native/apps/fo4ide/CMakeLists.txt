add_executable(fo4ide main.cpp McpTools.cpp McpCatalog.cpp McpCatalogRead.cpp McpCatalogEdit.cpp McpCatalogService.cpp)

target_link_libraries(
    fo4ide
    PRIVATE
        fo4ide_core
        fo4ide_logging
        fo4ide_settings
        fo4ide_process
        fo4ide_events
        fo4ide_rpc
        fo4ide_mcp
        fo4ide_nif
        fo4ide_window
        fo4ide_app
        fo4ide_plugin
)

if(FO4IDE_BUILD_HTTP)
    target_link_libraries(fo4ide PRIVATE fo4ide_http)
    target_compile_definitions(fo4ide PRIVATE FO4IDE_HAS_HTTP=1)
endif()

target_compile_definitions(
    fo4ide
    PRIVATE FO4IDE_SOURCE_WEB_ROOT="${PROJECT_SOURCE_DIR}/../web/dist"
)

fo4ide_enable_warnings(fo4ide)
fo4ide_enable_sanitizers(fo4ide)

# Preserve the historical executable name so existing MO2 launch entries and
# MCP configurations continue to work after the C++ cutover.
set_target_properties(fo4ide PROPERTIES OUTPUT_NAME "FO4RecordEditor")

fo4ide_use_utf8_code_page()
