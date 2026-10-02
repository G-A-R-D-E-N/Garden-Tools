include(Catch)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME fo4ide_native_mcp_smoke
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/native_mcp_smoke.py" "$<TARGET_FILE:fo4ide>")
set_tests_properties(fo4ide_native_mcp_smoke PROPERTIES TIMEOUT 120)

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
fo4ide_add_test(fo4ide_test_wiki unit/WikiServiceTests.cpp fo4ide_app)
fo4ide_add_test(fo4ide_test_graph_document unit/GraphDocumentTests.cpp fo4ide_graph)
fo4ide_add_test(fo4ide_test_graph_palette unit/NodePaletteTests.cpp fo4ide_graph)
fo4ide_add_test(fo4ide_test_graph_validator unit/GraphValidatorTests.cpp fo4ide_graph)
fo4ide_add_test(fo4ide_test_graph_flow unit/GraphFlowTests.cpp fo4ide_graph)
fo4ide_add_test(fo4ide_test_graph_compiler unit/GraphCompilerTests.cpp fo4ide_graph)
fo4ide_add_test(fo4ide_test_graph_lift unit/GraphLiftTests.cpp fo4ide_graph)
fo4ide_add_test(fo4ide_test_graph_loader unit/GraphScriptLoaderTests.cpp fo4ide_graph)
fo4ide_add_test(fo4ide_test_graph_tool unit/GraphToolServiceTests.cpp fo4ide_graph)
# The F4SE corpus sweep scans real F4SE trees when FO4RE_F4SE_SRC is set.
add_executable(
    fo4ide_test_f4se
    unit/F4SETypeMapTests.cpp
    unit/F4SECrossCheckTests.cpp
    unit/F4SERegionMergeTests.cpp
    unit/F4SEEmitterTests.cpp
    unit/F4SEExtractorTests.cpp
    unit/F4SECorpusTests.cpp
)
target_link_libraries(fo4ide_test_f4se PRIVATE fo4ide_f4se fo4ide_papyrus Catch2::Catch2WithMain)
fo4ide_enable_warnings(fo4ide_test_f4se)
fo4ide_enable_sanitizers(fo4ide_test_f4se)
catch_discover_tests(fo4ide_test_f4se PROPERTIES TIMEOUT 120)
# The corpus sweep parses thousands of real base scripts; give it headroom.
add_executable(fo4ide_test_papyrus_corpus unit/PapyrusCorpusTests.cpp)
target_link_libraries(fo4ide_test_papyrus_corpus PRIVATE fo4ide_papyrus Catch2::Catch2WithMain)
fo4ide_enable_warnings(fo4ide_test_papyrus_corpus)
fo4ide_enable_sanitizers(fo4ide_test_papyrus_corpus)
catch_discover_tests(fo4ide_test_papyrus_corpus PROPERTIES TIMEOUT 300)
fo4ide_add_test(fo4ide_test_logging unit/LoggerTests.cpp fo4ide_logging)
fo4ide_add_test(fo4ide_test_nif unit/NifServiceTests.cpp fo4ide_nif)
fo4ide_add_test(fo4ide_test_material unit/MaterialServiceTests.cpp fo4ide_material)

add_executable(
    fo4ide_test_texture_resolver
    unit/TextureResolverTests.cpp
    unit/TextureResolverPriorityTests.cpp
)
target_link_libraries(
    fo4ide_test_texture_resolver
    PRIVATE fo4ide_assets fo4 Catch2::Catch2WithMain
)
fo4ide_enable_warnings(fo4ide_test_texture_resolver)
fo4ide_enable_sanitizers(fo4ide_test_texture_resolver)
catch_discover_tests(fo4ide_test_texture_resolver PROPERTIES TIMEOUT 10)
fo4ide_add_test(fo4ide_test_textures unit/TextureCodecTests.cpp fo4ide_textures)
fo4ide_add_test(fo4ide_test_papyrus_lexer unit/PapyrusLexerTests.cpp fo4ide_papyrus)
fo4ide_add_test(fo4ide_test_papyrus_parser unit/PapyrusParserTests.cpp fo4ide_papyrus)
fo4ide_add_test(fo4ide_test_papyrus_resolver unit/PapyrusResolverTests.cpp fo4ide_papyrus)
fo4ide_add_test(fo4ide_test_papyrus_type_checker unit/PapyrusTypeCheckerTests.cpp fo4ide_papyrus)
fo4ide_add_test(fo4ide_test_papyrus_pex unit/PapyrusPexTests.cpp fo4ide_papyrus)
fo4ide_add_test(fo4ide_test_papyrus_codegen unit/PapyrusCodeGenTests.cpp fo4ide_papyrus)
fo4ide_add_test(fo4ide_test_papyrus_decompiler unit/PapyrusDecompilerTests.cpp fo4ide_papyrus)
fo4ide_add_test(fo4ide_test_papyrus_security unit/PapyrusSecurityTests.cpp fo4ide_papyrus)
fo4ide_add_test(fo4ide_test_papyrus_symbols unit/PapyrusSymbolsTests.cpp fo4ide_papyrus)
fo4ide_add_test(fo4ide_test_papyrus_analysis unit/PapyrusAnalysisServiceTests.cpp fo4ide_papyrus)
fo4ide_add_test(fo4ide_test_service_targets unit/ServiceTargetsTests.cpp fo4ide_app)
fo4ide_add_test(fo4ide_test_plugin_backend_target unit/PluginBackendTargetTests.cpp fo4ide_app)
fo4ide_add_test(fo4ide_test_plugin_element_routing unit/PluginBackendElementRoutingTests.cpp fo4ide_app)
fo4ide_add_test(fo4ide_test_mo2 unit/Mo2ProfileTests.cpp fo4ide_mo2)
fo4ide_add_test(fo4ide_test_app_interop unit/AppInteropTargetTests.cpp fo4ide_app)
fo4ide_add_test(fo4ide_test_tools unit/ToolLocatorTests.cpp fo4ide_tools)

add_executable(
    fo4ide_test_plugin_backend
    unit/RadiclePluginBackendTests.cpp
    unit/RadiclePluginWorklistTests.cpp
    unit/RadiclePluginMastersTests.cpp
    unit/RadiclePluginMutationTests.cpp
    unit/RadiclePluginLifecycleTests.cpp
    unit/RadiclePluginCreateTests.cpp
    unit/RadiclePluginMasterOverrideTests.cpp
    unit/RadiclePluginScalarTests.cpp
    unit/RadiclePluginSchemaGridTests.cpp
    unit/RadiclePluginPreflightTests.cpp
    unit/RadiclePluginArmorCreationTests.cpp
    unit/RadiclePluginRaceCreationTests.cpp
    unit/RadiclePluginFormLinkListTests.cpp
    unit/RadiclePluginListItemTests.cpp
    unit/RadiclePluginComponentsTests.cpp
    unit/RadiclePluginNestedFormLinkListTests.cpp
    unit/RadiclePluginElementTests.cpp
    unit/RadiclePluginMaintenanceTests.cpp
    unit/RadiclePluginDeepCopyTests.cpp
    unit/RadiclePluginRenumberTests.cpp
    unit/RadiclePluginLeveledTests.cpp
    unit/RadiclePluginSeqTests.cpp
    unit/RadiclePluginReferenceRemapTests.cpp
    unit/RadiclePluginOverridePatchTests.cpp
    unit/RadiclePluginExteriorCellTests.cpp
    unit/RadiclePluginCellTests.cpp
    unit/RadiclePluginEyesTests.cpp
    unit/RadiclePluginEyesGridTests.cpp
    unit/RadiclePluginLvspTests.cpp
    unit/RadiclePluginLvspGridTests.cpp
    unit/RadiclePluginBatchMutationTests.cpp
    unit/RadiclePluginAddMastersTests.cpp
    unit/RadiclePluginReadApiTests.cpp
    unit/RadiclePluginEslTests.cpp
    unit/RadiclePluginEditableTests.cpp
    unit/ConditionFunctionsTests.cpp
    unit/ConditionBackendTests.cpp
    unit/RadiclePluginConcurrencyTests.cpp
    "${PROJECT_SOURCE_DIR}/../../Radicle/tests/plugin_bytes.cpp"
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
    PRIVATE fo4ide_plugin Catch2::Catch2WithMain ZLIB::ZLIB
)
target_compile_definitions(fo4ide_test_plugin_backend PRIVATE FO4IDE_TEST_HOOKS)
fo4ide_enable_warnings(fo4ide_test_plugin_backend)
fo4ide_enable_sanitizers(fo4ide_test_plugin_backend)
catch_discover_tests(fo4ide_test_plugin_backend PROPERTIES TIMEOUT 10)

add_executable(
    fo4ide_test_proj_expl_hazd
    unit/RadiclePluginProjExplHazdTests.cpp
    "${PROJECT_SOURCE_DIR}/../../Radicle/tests/plugin_bytes.cpp"
)
target_include_directories(
    fo4ide_test_proj_expl_hazd
    PRIVATE "${PROJECT_SOURCE_DIR}/../../Radicle/tests"
)
target_link_libraries(
    fo4ide_test_proj_expl_hazd
    PRIVATE fo4ide_plugin Catch2::Catch2WithMain ZLIB::ZLIB
)
fo4ide_enable_warnings(fo4ide_test_proj_expl_hazd)
fo4ide_enable_sanitizers(fo4ide_test_proj_expl_hazd)
catch_discover_tests(fo4ide_test_proj_expl_hazd PROPERTIES TIMEOUT 10)

add_executable(
    fo4ide_test_quest_authoring
    unit/RadiclePluginQuestAuthoringTests.cpp
    unit/RadiclePluginQuestPartialFormTests.cpp
    unit/RadiclePluginQuestCreateLevelTests.cpp
    unit/RadiclePluginDeferredLocalizationTests.cpp
    unit/RadiclePluginPendingLifecycleTests.cpp
    "${PROJECT_SOURCE_DIR}/../../Radicle/tests/plugin_bytes.cpp"
)
target_include_directories(
    fo4ide_test_quest_authoring
    PRIVATE "${PROJECT_SOURCE_DIR}/../../Radicle/tests"
)
target_link_libraries(
    fo4ide_test_quest_authoring
    PRIVATE fo4ide_plugin Catch2::Catch2WithMain ZLIB::ZLIB
)
target_compile_definitions(fo4ide_test_quest_authoring PRIVATE FO4IDE_TEST_HOOKS)
fo4ide_enable_warnings(fo4ide_test_quest_authoring)
fo4ide_enable_sanitizers(fo4ide_test_quest_authoring)
catch_discover_tests(fo4ide_test_quest_authoring PROPERTIES TIMEOUT 10)

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

fo4ide_use_utf8_code_page()

if(FO4IDE_BUILD_HTTP)
    add_subdirectory(integration)
endif()
