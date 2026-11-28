include(Catch)

set(FO4IDE_TEST_TIMEOUT 10)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(FO4IDE_TEST_TIMEOUT 60)
endif()
set(FO4IDE_SLOW_TEST_TIMEOUT 300)
# Tests tagged [multi-process] spawn a fresh worker per crash phase and must keep this longer timeout under runner load.
set(FO4IDE_MULTI_PROCESS_TEST_TIMEOUT 120)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME fo4ide_native_mcp_smoke
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/native_mcp_smoke.py" "$<TARGET_FILE:fo4ide>")
set_tests_properties(fo4ide_native_mcp_smoke PROPERTIES TIMEOUT 120)

function(fo4ide_add_test name source library)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE ${library} Catch2::Catch2WithMain)
    fo4ide_enable_warnings(${name})
    fo4ide_enable_sanitizers(${name})
    catch_discover_tests(${name} TEST_SPEC "~[slow]" PROPERTIES TIMEOUT ${FO4IDE_TEST_TIMEOUT})
    catch_discover_tests(${name} TEST_SPEC "[slow]" OUTPUT_SUFFIX _slow PROPERTIES TIMEOUT ${FO4IDE_SLOW_TEST_TIMEOUT})
endfunction()

fo4ide_add_test(fo4ide_test_wiki unit/WikiServiceTests.cpp fo4ide_app)
fo4ide_add_test(fo4ide_test_previs_target unit/PrevisTargetTests.cpp fo4ide_app)
fo4ide_add_test(fo4ide_test_debug_checks unit/DebugChecksTargetTests.cpp fo4ide_app)
fo4ide_add_test(fo4ide_test_debug_tools unit/DebugToolsTargetTests.cpp fo4ide_app)
fo4ide_add_test(fo4ide_test_debug_tools_regression unit/DebugToolsRegressionTests.cpp fo4ide_app)
target_compile_definitions(
    fo4ide_test_debug_tools
    PRIVATE FO4IDE_RADICLE_TESTDATA_DIR="${PROJECT_SOURCE_DIR}/../../Radicle/testdata"
)
fo4ide_add_test(fo4ide_test_nif unit/NifServiceTests.cpp fo4ide_nif)
fo4ide_add_test(fo4ide_test_nif_gltf unit/NifGltfServiceTests.cpp fo4ide_nif)
fo4ide_add_test(fo4ide_test_asset_material_json unit/AssetMaterialJsonTests.cpp fo4ide_assets)
fo4ide_add_test(fo4ide_test_nif_collision unit/NifCollisionServiceTests.cpp fo4ide_nif)
fo4ide_add_test(fo4ide_test_vertex_color_surface unit/VertexColorSurfaceTests.cpp fo4ide_nif)
fo4ide_add_test(fo4ide_test_uv_surface unit/UvSurfaceTests.cpp fo4ide_nif)
fo4ide_add_test(fo4ide_test_nif_extra unit/NifExtraBlockServiceTests.cpp fo4ide_nif)
fo4ide_add_test(fo4ide_test_material unit/MaterialServiceTests.cpp fo4ide_material)
fo4ide_add_test(fo4ide_test_layout unit/LayoutMathTests.cpp fo4ide_layout)
fo4ide_add_test(fo4ide_test_popout_route unit/PopoutRouteTests.cpp fo4ide_window)
fo4ide_add_test(fo4ide_test_layout_target unit/LayoutTargetTests.cpp fo4ide_app)

add_executable(
    fo4ide_test_texture_resolver
    unit/TextureResolverTests.cpp
    unit/TextureResolverPriorityTests.cpp
)
target_link_libraries(
    fo4ide_test_texture_resolver
    PRIVATE fo4ide_assets fo4ide_textures fo4 Catch2::Catch2WithMain
)
fo4ide_enable_warnings(fo4ide_test_texture_resolver)
fo4ide_enable_sanitizers(fo4ide_test_texture_resolver)
catch_discover_tests(fo4ide_test_texture_resolver PROPERTIES TIMEOUT ${FO4IDE_TEST_TIMEOUT})
fo4ide_add_test(fo4ide_test_textures unit/TextureCodecTests.cpp fo4ide_textures)
fo4ide_add_test(fo4ide_test_asset_path unit/AssetPathTests.cpp fo4ide_assets)
fo4ide_add_test(fo4ide_test_texture_real_data unit/TextureResolverRealDataTests.cpp "fo4ide_assets;fo4ide_nif")
fo4ide_add_test(fo4ide_test_preview_raster unit/PreviewRasterTests.cpp fo4ide_preview)
fo4ide_add_test(fo4ide_test_preview_render unit/PreviewRenderTests.cpp fo4ide_preview)
fo4ide_add_test(fo4ide_test_preview_target unit/PreviewTargetTests.cpp fo4ide_app)

add_executable(fo4ide_fake_process helpers/fake_process.cpp)
fo4ide_enable_warnings(fo4ide_fake_process)
fo4ide_enable_sanitizers(fo4ide_fake_process)

add_executable(
    fo4ide_test_core
    unit/EventBusTests.cpp
    unit/RpcDispatcherTests.cpp
    unit/McpServerTests.cpp
    unit/McpPathPolicyTests.cpp
    ../apps/fo4ide/McpPathPolicy.cpp
    ../apps/fo4ide/McpTools.cpp
    ../apps/fo4ide/McpPreviewTools.cpp
    ../apps/fo4ide/McpCatalog.cpp
    ../apps/fo4ide/McpCatalogRead.cpp
    ../apps/fo4ide/McpCatalogEdit.cpp
    ../apps/fo4ide/McpCatalogService.cpp
    ../apps/fo4ide/McpCatalogAssets.cpp
    ../apps/fo4ide/McpCatalogLayout.cpp
    unit/SettingsTests.cpp
    unit/LoggerTests.cpp
    unit/ToolLocatorTests.cpp
    unit/ProcessRunnerTests.cpp
)
target_link_libraries(
    fo4ide_test_core
    PRIVATE
        fo4ide_events
        fo4ide_rpc
        fo4ide_mcp
        fo4ide_settings
        fo4ide_logging
        fo4ide_tools
        fo4ide_process
        Catch2::Catch2WithMain
)
target_compile_definitions(
    fo4ide_test_core
    PRIVATE FO4IDE_FAKE_PROCESS_PATH="$<TARGET_FILE:fo4ide_fake_process>"
)
add_dependencies(fo4ide_test_core fo4ide_fake_process)
fo4ide_enable_warnings(fo4ide_test_core)
fo4ide_enable_sanitizers(fo4ide_test_core)
catch_discover_tests(fo4ide_test_core PROPERTIES TIMEOUT ${FO4IDE_TEST_TIMEOUT})

fo4ide_add_test(fo4ide_test_service_targets "unit/ServiceTargetsTests.cpp;unit/CellTargetTests.cpp;unit/McpNativeToolTests.cpp" fo4ide_app)
target_sources(
    fo4ide_test_service_targets
    PRIVATE
        "${PROJECT_SOURCE_DIR}/apps/fo4ide/McpTools.cpp"
        "${PROJECT_SOURCE_DIR}/apps/fo4ide/McpCatalog.cpp"
        "${PROJECT_SOURCE_DIR}/apps/fo4ide/McpCatalogRead.cpp"
        "${PROJECT_SOURCE_DIR}/apps/fo4ide/McpCatalogEdit.cpp"
        "${PROJECT_SOURCE_DIR}/apps/fo4ide/McpCatalogService.cpp"
        "${PROJECT_SOURCE_DIR}/apps/fo4ide/McpCatalogAssets.cpp"
        "${PROJECT_SOURCE_DIR}/apps/fo4ide/McpCatalogLayout.cpp"
        "${PROJECT_SOURCE_DIR}/apps/fo4ide/McpPreviewTools.cpp"
        "${PROJECT_SOURCE_DIR}/apps/fo4ide/McpPathPolicy.cpp"
)
target_include_directories(fo4ide_test_service_targets PRIVATE "${PROJECT_SOURCE_DIR}/apps/fo4ide")
target_link_libraries(fo4ide_test_service_targets PRIVATE fo4ide_mcp fo4ide_logging)
fo4ide_add_test(fo4ide_test_graph_target unit/GraphTargetTests.cpp fo4ide_app)
fo4ide_add_test(fo4ide_test_lod_target unit/LodTargetTests.cpp fo4ide_app)
fo4ide_add_test(fo4ide_test_nif_target unit/NifTargetTests.cpp fo4ide_app)
target_include_directories(
    fo4ide_test_nif_target
    PRIVATE "${PROJECT_SOURCE_DIR}/../../Radicle/tests"
)
target_compile_definitions(
    fo4ide_test_nif_target
    PRIVATE FO4IDE_HKX_SAMPLES_DIR="${PROJECT_SOURCE_DIR}/../../Radicle/tests/fixtures/hkx/samples"
)
fo4ide_add_test(fo4ide_test_audio_service unit/AudioServiceTests.cpp fo4ide_app)
fo4ide_add_test(fo4ide_test_material_target unit/MaterialTargetTests.cpp fo4ide_app)
# The subgraph case builds a RACE plugin with the shared byte writer, so this target
# needs plugin_bytes.cpp's definitions as well as its header.
add_executable(
    fo4ide_test_hkx_target
    unit/HkxTargetTests.cpp
    "${PROJECT_SOURCE_DIR}/../../Radicle/tests/plugin_bytes.cpp"
)
target_include_directories(fo4ide_test_hkx_target PRIVATE "${PROJECT_SOURCE_DIR}/../../Radicle/tests")
target_compile_definitions(
    fo4ide_test_hkx_target
    PRIVATE FO4IDE_HKX_SAMPLES_DIR="${PROJECT_SOURCE_DIR}/../../Radicle/tests/fixtures/hkx/samples"
)
target_link_libraries(fo4ide_test_hkx_target PRIVATE fo4ide_app Catch2::Catch2WithMain)
fo4ide_enable_warnings(fo4ide_test_hkx_target)
fo4ide_enable_sanitizers(fo4ide_test_hkx_target)
catch_discover_tests(fo4ide_test_hkx_target TEST_SPEC "~[slow]" PROPERTIES TIMEOUT ${FO4IDE_TEST_TIMEOUT})
catch_discover_tests(fo4ide_test_hkx_target TEST_SPEC "[slow]" OUTPUT_SUFFIX _slow PROPERTIES TIMEOUT ${FO4IDE_SLOW_TEST_TIMEOUT})
target_include_directories(
    fo4ide_test_hkx_target
    PRIVATE "${PROJECT_SOURCE_DIR}/../../Radicle/tests"
)
fo4ide_add_test(fo4ide_test_swf_target unit/SwfTargetTests.cpp fo4ide_app)
fo4ide_add_test(fo4ide_test_swf_target_save_as unit/SwfTargetSaveAsTests.cpp fo4ide_app)
fo4ide_add_test(fo4ide_test_swf_target_take_text unit/SwfTargetTakeTextTests.cpp fo4ide_app)
fo4ide_add_test(fo4ide_test_settings_security unit/SettingsTargetSecurityTests.cpp fo4ide_app)
fo4ide_add_test(fo4ide_test_papyrus_target unit/PapyrusTargetTests.cpp fo4ide_app)
fo4ide_add_test(fo4ide_test_plugin_backend_target unit/PluginBackendTargetTests.cpp fo4ide_app)

add_executable(
    fo4ide_test_plugin_check
    unit/PluginCheckCliTests.cpp
)
target_include_directories(
    fo4ide_test_plugin_check
    PRIVATE "${PROJECT_SOURCE_DIR}/../../Radicle/tests"
)
target_link_libraries(fo4ide_test_plugin_check PRIVATE fo4ide_plugin Catch2::Catch2WithMain)
target_compile_definitions(fo4ide_test_plugin_check PRIVATE FO4IDE_APP_EXE="$<TARGET_FILE:fo4ide>")
add_dependencies(fo4ide_test_plugin_check fo4ide)
fo4ide_enable_warnings(fo4ide_test_plugin_check)
fo4ide_enable_sanitizers(fo4ide_test_plugin_check)
catch_discover_tests(fo4ide_test_plugin_check PROPERTIES TIMEOUT ${FO4IDE_TEST_TIMEOUT})
fo4ide_add_test(fo4ide_test_plugin_backend_target_perk unit/PluginBackendTargetPerkTests.cpp fo4ide_app)
fo4ide_add_test(fo4ide_test_plugin_element_routing unit/PluginBackendElementRoutingTests.cpp fo4ide_app)
fo4ide_add_test(fo4ide_test_mo2 unit/Mo2ProfileTests.cpp fo4ide_mo2)
fo4ide_add_test(fo4ide_test_app_interop unit/AppInteropTargetTests.cpp fo4ide_app)
fo4ide_add_test(fo4ide_test_app_interop_rollback unit/AppInteropRollbackTests.cpp fo4ide_app)
fo4ide_add_test(fo4ide_test_gui_endpoint unit/GuiEndpointTests.cpp fo4ide_app)

# Fixture trees are found through an absolute path rather than __FILE__:
# ccache's base_dir rewrites __FILE__ to a build-relative path, which no
# longer resolves from the directory ctest runs the tests in.
set(FO4IDE_GRAPH_FIXTURES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../Radicle/testdata/graph")
foreach(fixture_test
        fo4ide_test_service_targets fo4ide_test_papyrus_target
        fo4ide_test_graph_target)
    target_compile_definitions(
        ${fixture_test}
        PRIVATE FO4IDE_TEST_FIXTURES_DIR="${FO4IDE_GRAPH_FIXTURES_DIR}"
    )
endforeach()

add_executable(
    fo4ide_test_plugin_backend
    unit/RadiclePluginBackendTests.cpp
    unit/RadiclePluginNameFieldTests.cpp
    unit/RadiclePluginMessageButtonsTests.cpp
    unit/RadiclePluginQuestObjectiveTests.cpp
    unit/RadiclePluginQuestAuthorTests.cpp
    unit/RadiclePluginQuestFragmentAuthorTests.cpp
    unit/RadiclePluginQuestScriptEditTests.cpp
    unit/RadiclePluginQuestScriptSafetyTests.cpp
    unit/RadiclePluginJournalRootTests.cpp
    unit/RadiclePluginJournalIntegrationTests.cpp
    unit/RadiclePluginJournalDirectoryTests.cpp
    unit/RadiclePluginJournalArchiveTests.cpp
    unit/RadiclePluginJournalReadStringsTests.cpp
    unit/RadiclePluginBa2SourceTests.cpp
    unit/RadiclePluginStringJournalTests.cpp
    unit/RadiclePluginWindowsJournalProbeTests.cpp
    unit/RadiclePluginWindowsJournalSessionTests.cpp
    unit/RadiclePluginWindowsJournalIntentTests.cpp
    unit/RadiclePluginHedrCountTests.cpp
    unit/RadiclePluginStringsTests.cpp
    unit/RadiclePluginStringLanguagesTests.cpp
    unit/RadiclePluginWorklistTests.cpp
    unit/RadiclePluginMastersTests.cpp
    unit/RadiclePluginMutationTests.cpp
    unit/RadiclePluginPrevisPatchTests.cpp
    unit/RadiclePluginPrevisLodTargetTests.cpp
    unit/RadiclePluginPrevisTomeLedgerTests.cpp
    unit/PrevisRealCellTests.cpp
    unit/RadiclePluginLifecycleTests.cpp
    unit/WindowsDebugReportsToStderr.cpp
    unit/RadiclePluginDeleteChildGroupTests.cpp
    unit/RadiclePluginEditorIdTextTests.cpp
    unit/RadiclePluginCreateTests.cpp
    unit/RadiclePluginMasterOverrideTests.cpp
    unit/RadiclePluginScalarTests.cpp
    unit/RadiclePluginSchemaGridTests.cpp
    unit/RadiclePluginW0Tests.cpp
    unit/RadiclePluginSoundSemanticTests.cpp
    unit/RadiclePluginSchemaAuthoringTests.cpp
    unit/RadiclePluginWaterSemanticTests.cpp
    unit/RadiclePluginVegetationSemanticTests.cpp
    unit/RadiclePluginClimateSemanticTests.cpp
    unit/RadiclePluginPreflightTests.cpp
    unit/RadiclePluginArmorCreationTests.cpp
    unit/RadiclePluginRaceCreationTests.cpp
    unit/RadiclePluginRichCreationTests.cpp
    unit/RadiclePluginValidationTests.cpp
    unit/RadiclePluginMemoryTests.cpp
    unit/RadiclePluginFormLinkListTests.cpp
    unit/RadiclePluginFormListTests.cpp
    unit/RadiclePluginListItemTests.cpp
    unit/RadiclePluginComponentsTests.cpp
    unit/RadiclePluginNestedFormLinkListTests.cpp
    unit/RadiclePluginElementTests.cpp
    unit/RadiclePluginMaintenanceTests.cpp
    unit/RadiclePluginUndecodableMaintenanceTests.cpp
    unit/RadiclePluginDeepCopyTests.cpp
    unit/RadiclePluginRenumberTests.cpp
    unit/RadiclePluginLeveledTests.cpp
    unit/RadiclePluginSeqTests.cpp
    unit/RadiclePluginReferenceRemapTests.cpp
    unit/RadiclePluginOverridePatchTests.cpp
    unit/RadiclePluginExteriorCellTests.cpp
    unit/RadiclePluginCellTests.cpp
    unit/RadiclePluginCellEditTests.cpp
    unit/RadiclePluginExteriorEditTests.cpp
    unit/RadiclePluginCellCleanupTests.cpp
    unit/RadicleCellLightJsonTests.cpp
    unit/RadiclePluginCellReferenceTests.cpp
    unit/RadiclePluginCellReferenceEditTests.cpp
    unit/RadiclePluginBaseObjectTests.cpp
    unit/RadiclePluginAssetTests.cpp
    unit/RadiclePluginTypedRecordsTests.cpp
    unit/RadiclePluginTypedEntryEditTests.cpp
    unit/RadiclePluginSoundTests.cpp
    unit/RadiclePluginPackageTests.cpp
    unit/RadiclePluginScriptTests.cpp
    unit/RadiclePluginQuestAliasScriptTests.cpp
    unit/RadiclePluginEyesTests.cpp
    unit/RadiclePluginEyesGridTests.cpp
    unit/RadiclePluginLvspTests.cpp
    unit/RadiclePluginLvspGridTests.cpp
    unit/RadiclePluginPerkTests.cpp
    unit/RadiclePluginPerkRoundTripTests.cpp
    unit/RadiclePluginBatchMutationTests.cpp
    unit/RadiclePluginAddMastersTests.cpp
    unit/RadiclePluginCopyAsWrapperTests.cpp
    unit/RadiclePluginItmNestedTests.cpp
    unit/RadiclePluginCompareTests.cpp
    unit/RadiclePluginReachableTests.cpp
    unit/RadiclePluginReadApiTests.cpp
    unit/RadiclePluginReadGapTests.cpp
    unit/RadiclePluginEslTests.cpp
    unit/RadiclePluginCompactTests.cpp
    unit/RadiclePluginEditableTests.cpp
    unit/RadiclePluginBackendHygieneTests.cpp
    unit/ConditionFunctionsTests.cpp
    unit/ConditionBackendTests.cpp
    unit/ConditionCodecValidationTests.cpp
    unit/RadiclePluginListReplaceTests.cpp
    unit/ConditionBackendParamTests.cpp
    unit/ConditionCodecTests.cpp
    unit/ConditionOwnerContextTests.cpp
    unit/QuestOpaqueConditionTests.cpp
    unit/ConditionPreservationBackendTests.cpp
    unit/RadiclePluginConcurrencyTests.cpp
    unit/RadiclePluginProjExplHazdTests.cpp
    unit/RadiclePluginProjExplHazdFormLinkTests.cpp
    unit/RadiclePluginProjFlagsTests.cpp
    unit/RadiclePluginProjExplHazdLimitsTests.cpp
    unit/RadiclePluginQuestAuthoringTests.cpp
    unit/RadiclePluginQuestAliasKeepTests.cpp
    unit/RadiclePluginQuestLocalizedCnamTests.cpp
    unit/RadiclePluginQuestLocalizationTests.cpp
    unit/RadiclePluginQuestReadbackTests.cpp
    unit/RadiclePluginQuestEventDataTests.cpp
    unit/RadiclePluginQuestPartialFormTests.cpp
    unit/RadiclePluginQuestCreateLevelTests.cpp
    unit/RadiclePluginQuestConditionDefaultsTests.cpp
    unit/RadiclePluginDeferredLocalizationTests.cpp
    unit/RadiclePluginPendingLifecycleTests.cpp
    unit/RadiclePluginQuestEndToEndTests.cpp
    unit/RadiclePluginDialogueTests.cpp
    unit/RadiclePluginSceneTests.cpp
    unit/RadiclePluginFragmentsTests.cpp
    unit/RadiclePluginDialogueBulkTests.cpp
    unit/RadiclePluginAssetReferencesTests.cpp
    unit/RadiclePluginCheckQuestTests.cpp
    unit/DestructibleTargetTests.cpp
    "${PROJECT_SOURCE_DIR}/../../Radicle/tests/plugin_bytes.cpp"
    ../apps/fo4ide/McpPathPolicy.cpp
    ../apps/fo4ide/McpCatalog.cpp
    ../apps/fo4ide/McpCatalogRead.cpp
    ../apps/fo4ide/McpCatalogEdit.cpp
    ../apps/fo4ide/McpCatalogService.cpp
    ../apps/fo4ide/McpCatalogAssets.cpp
    ../apps/fo4ide/McpCatalogLayout.cpp
)
target_include_directories(
    fo4ide_test_plugin_backend
    PRIVATE "${PROJECT_SOURCE_DIR}/../../Radicle/tests"
)
target_compile_definitions(
    fo4ide_test_plugin_backend
    PRIVATE FO4IDE_RADICLE_TESTDATA_DIR="${PROJECT_SOURCE_DIR}/../../Radicle/testdata"
)
target_link_libraries(
    fo4ide_test_plugin_backend
    PRIVATE fo4ide_plugin fo4ide_app fo4ide_mcp fo4ide_logging Catch2::Catch2WithMain ZLIB::ZLIB
)
if(WIN32)
    target_link_libraries(fo4ide_test_plugin_backend PRIVATE ntdll advapi32 userenv)
endif()
target_compile_definitions(fo4ide_test_plugin_backend PRIVATE FO4IDE_TEST_HOOKS)
target_compile_definitions(fo4ide_test_plugin_backend PRIVATE FO4IDE_TEST_FIXTURES_DIR="${FO4IDE_GRAPH_FIXTURES_DIR}")
target_compile_definitions(
    fo4ide_test_plugin_backend
    PRIVATE FO4IDE_PREVIS_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../../Radicle/testdata/previs/oracles"
)
fo4ide_enable_warnings(fo4ide_test_plugin_backend)
fo4ide_enable_sanitizers(fo4ide_test_plugin_backend)
catch_discover_tests(fo4ide_test_plugin_backend TEST_SPEC "~[multi-process]~QuestScriptReal.*" PROPERTIES TIMEOUT ${FO4IDE_TEST_TIMEOUT})
catch_discover_tests(fo4ide_test_plugin_backend TEST_SPEC "[multi-process]" OUTPUT_SUFFIX _multi_process PROPERTIES TIMEOUT ${FO4IDE_MULTI_PROCESS_TEST_TIMEOUT})
# Real-game QuestScriptReal cases decompile and compile whole vanilla archives; they need the long real-data timeout.
catch_discover_tests(fo4ide_test_plugin_backend TEST_SPEC "QuestScriptReal.*" OUTPUT_SUFFIX _quest_script_real PROPERTIES TIMEOUT 1800)

add_executable(
    fo4ide_test_plugin_oracles
    unit/RadiclePluginWriteOracleTests.cpp
    unit/RadiclePluginEslPolicyOracleTests.cpp
    unit/RadiclePluginAuditOracleTests.cpp
    unit/RadicleWriteConflictOracleTests.cpp
    unit/RadiclePluginArmorResistanceOracleTests.cpp
    unit/RadicleSwfWrapperOracleTests.cpp
    unit/RadiclePluginObjectTemplateOracleTests.cpp
    unit/RadiclePluginWriteGapOracleTests.cpp
    ../apps/fo4ide/McpPathPolicy.cpp
    ../apps/fo4ide/McpCatalog.cpp
    ../apps/fo4ide/McpCatalogRead.cpp
    ../apps/fo4ide/McpCatalogEdit.cpp
    ../apps/fo4ide/McpCatalogService.cpp
    ../apps/fo4ide/McpCatalogAssets.cpp
    ../apps/fo4ide/McpCatalogLayout.cpp
    "${PROJECT_SOURCE_DIR}/../../Radicle/tests/plugin_bytes.cpp"
)
target_include_directories(
    fo4ide_test_plugin_oracles
    PRIVATE "${PROJECT_SOURCE_DIR}/../../Radicle/tests"
)
target_link_libraries(
    fo4ide_test_plugin_oracles
    PRIVATE fo4ide_plugin fo4ide_app fo4ide_mcp fo4ide_logging Catch2::Catch2WithMain ZLIB::ZLIB
)
if(WIN32)
    target_link_libraries(fo4ide_test_plugin_oracles PRIVATE ntdll advapi32 userenv)
endif()
fo4ide_enable_warnings(fo4ide_test_plugin_oracles)
fo4ide_enable_sanitizers(fo4ide_test_plugin_oracles)
catch_discover_tests(fo4ide_test_plugin_oracles PROPERTIES TIMEOUT ${FO4IDE_TEST_TIMEOUT})

# Built by the native-real-data workflow, which runs on the host that holds the Fallout 4 data.
add_executable(fo4ide_test_sound_semantic_real unit/RadiclePluginSoundSemanticRealTests.cpp unit/PluginValidationRealTests.cpp)
target_link_libraries(fo4ide_test_sound_semantic_real PRIVATE fo4ide_plugin Catch2::Catch2WithMain)
fo4ide_enable_warnings(fo4ide_test_sound_semantic_real)
fo4ide_enable_sanitizers(fo4ide_test_sound_semantic_real)
catch_discover_tests(fo4ide_test_sound_semantic_real PROPERTIES TIMEOUT 1800)

add_executable(
    fo4ide_test_archive_backend
    unit/RadicleArchiveBackendTests.cpp
    unit/RadicleArchiveOracleTests.cpp
    ../apps/fo4ide/McpPathPolicy.cpp
    ../apps/fo4ide/McpCatalog.cpp
    ../apps/fo4ide/McpCatalogRead.cpp
    ../apps/fo4ide/McpCatalogEdit.cpp
    ../apps/fo4ide/McpCatalogService.cpp
    ../apps/fo4ide/McpCatalogAssets.cpp
    ../apps/fo4ide/McpCatalogLayout.cpp
)
target_link_libraries(
    fo4ide_test_archive_backend
    PRIVATE fo4ide_app fo4ide_mcp fo4ide_logging fo4ide_archive fo4 Catch2::Catch2WithMain
)
fo4ide_enable_warnings(fo4ide_test_archive_backend)
fo4ide_enable_sanitizers(fo4ide_test_archive_backend)
catch_discover_tests(fo4ide_test_archive_backend PROPERTIES TIMEOUT ${FO4IDE_TEST_TIMEOUT})

add_executable(fo4ide_test_asset_catalog unit/RadiclePluginAssetCatalogTests.cpp)
target_link_libraries(fo4ide_test_asset_catalog PRIVATE fo4ide_plugin Catch2::Catch2WithMain)
fo4ide_enable_warnings(fo4ide_test_asset_catalog)
catch_discover_tests(fo4ide_test_asset_catalog PROPERTIES TIMEOUT ${FO4IDE_TEST_TIMEOUT})

# The cell viewer real-data cases must use the slow-test timeout: they load Fallout4.esm and index its archives.
add_executable(fo4ide_test_cell_viewer_real unit/CellViewerRealDataTests.cpp)
target_link_libraries(fo4ide_test_cell_viewer_real PRIVATE fo4ide_plugin fo4ide_app Catch2::Catch2WithMain ZLIB::ZLIB)
if(WIN32)
    target_link_libraries(fo4ide_test_cell_viewer_real PRIVATE ntdll advapi32 userenv)
endif()
fo4ide_enable_warnings(fo4ide_test_cell_viewer_real)
fo4ide_enable_sanitizers(fo4ide_test_cell_viewer_real)
catch_discover_tests(fo4ide_test_cell_viewer_real PROPERTIES TIMEOUT ${FO4IDE_SLOW_TEST_TIMEOUT})

fo4ide_use_utf8_code_page()

if(FO4IDE_BUILD_HTTP)
    fo4ide_add_test(fo4ide_test_remote_target unit/RemoteTargetTests.cpp fo4ide_remote)
    target_link_libraries(fo4ide_test_remote_target PRIVATE httplib::httplib)
    add_subdirectory(integration)
endif()
