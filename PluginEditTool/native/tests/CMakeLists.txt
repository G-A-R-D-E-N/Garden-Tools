include(Catch)

set(FO4IDE_TEST_TIMEOUT 10)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(FO4IDE_TEST_TIMEOUT 60)
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME fo4ide_native_mcp_smoke
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/native_mcp_smoke.py" "$<TARGET_FILE:fo4ide>")
set_tests_properties(fo4ide_native_mcp_smoke PROPERTIES TIMEOUT 120)

function(fo4ide_add_test name source library)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE ${library} Catch2::Catch2WithMain)
    fo4ide_enable_warnings(${name})
    fo4ide_enable_sanitizers(${name})
    catch_discover_tests(${name} PROPERTIES TIMEOUT ${FO4IDE_TEST_TIMEOUT})
endfunction()

fo4ide_add_test(fo4ide_test_wiki unit/WikiServiceTests.cpp fo4ide_app)
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
catch_discover_tests(fo4ide_test_texture_resolver PROPERTIES TIMEOUT ${FO4IDE_TEST_TIMEOUT})
fo4ide_add_test(fo4ide_test_textures unit/TextureCodecTests.cpp fo4ide_textures)

add_executable(fo4ide_fake_process helpers/fake_process.cpp)
fo4ide_enable_warnings(fo4ide_fake_process)
fo4ide_enable_sanitizers(fo4ide_fake_process)

add_executable(
    fo4ide_test_core
    unit/EventBusTests.cpp
    unit/RpcDispatcherTests.cpp
    unit/McpServerTests.cpp
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

add_executable(
    fo4ide_test_graph
    unit/GraphDocumentTests.cpp
    unit/NodePaletteTests.cpp
    unit/GraphValidatorTests.cpp
    unit/GraphFlowTests.cpp
    unit/GraphCompilerTests.cpp
    unit/GraphLiftTests.cpp
    unit/GraphScriptLoaderTests.cpp
    unit/GraphToolServiceTests.cpp
)
target_link_libraries(fo4ide_test_graph PRIVATE fo4ide_graph Catch2::Catch2WithMain)
fo4ide_enable_warnings(fo4ide_test_graph)
fo4ide_enable_sanitizers(fo4ide_test_graph)
catch_discover_tests(fo4ide_test_graph TEST_SPEC "~GraphCompiler*" PROPERTIES TIMEOUT ${FO4IDE_TEST_TIMEOUT})
# Invariant: Oracle1/Oracle2 compile, decompile and recompile all 28 fixtures, so they keep the 60 s timeout, not the 10 s Release default.
catch_discover_tests(fo4ide_test_graph TEST_SPEC "GraphCompiler*" PROPERTIES TIMEOUT 60)

add_executable(
    fo4ide_test_papyrus
    unit/PapyrusLexerTests.cpp
    unit/PapyrusParserTests.cpp
    unit/PapyrusResolverTests.cpp
    unit/PapyrusTypeCheckerTests.cpp
    unit/PapyrusPexTests.cpp
    unit/PapyrusCodeGenTests.cpp
    unit/PapyrusUserFlagTableTests.cpp
    unit/PapyrusDecompilerTests.cpp
    unit/PapyrusSecurityTests.cpp
    unit/PapyrusSymbolsTests.cpp
    unit/PapyrusAnalysisServiceTests.cpp
    unit/F4SETypeMapTests.cpp
    unit/F4SECrossCheckTests.cpp
    unit/F4SERegionMergeTests.cpp
    unit/F4SEEmitterTests.cpp
    unit/F4SEExtractorTests.cpp
)
target_link_libraries(fo4ide_test_papyrus PRIVATE fo4ide_papyrus fo4ide_f4se Catch2::Catch2WithMain)
fo4ide_enable_warnings(fo4ide_test_papyrus)
fo4ide_enable_sanitizers(fo4ide_test_papyrus)
catch_discover_tests(fo4ide_test_papyrus TEST_SPEC "~F4SE*" PROPERTIES TIMEOUT ${FO4IDE_TEST_TIMEOUT})
catch_discover_tests(fo4ide_test_papyrus TEST_SPEC "F4SE*" PROPERTIES TIMEOUT 120)

add_executable(
    fo4ide_test_corpus
    unit/PapyrusCorpusTests.cpp
    unit/PapyrusDifferentialTests.cpp
    unit/F4SECorpusTests.cpp
)
target_link_libraries(fo4ide_test_corpus PRIVATE fo4ide_papyrus fo4ide_f4se Catch2::Catch2WithMain)
fo4ide_enable_warnings(fo4ide_test_corpus)
fo4ide_enable_sanitizers(fo4ide_test_corpus)
# The F4SE corpus sweep scans real F4SE trees when FO4RE_F4SE_SRC is set.
catch_discover_tests(fo4ide_test_corpus TEST_SPEC "F4SE*" PROPERTIES TIMEOUT 120)
# The corpus sweep parses thousands of real base scripts; give it headroom.
catch_discover_tests(fo4ide_test_corpus TEST_SPEC "The*" PROPERTIES TIMEOUT 300)
# The differential compiles every vanilla pair with a fresh index each; give it 3x the corpus headroom.
catch_discover_tests(fo4ide_test_corpus TEST_SPEC "Compiled*" PROPERTIES TIMEOUT 900)

fo4ide_add_test(fo4ide_test_service_targets unit/ServiceTargetsTests.cpp fo4ide_app)
fo4ide_add_test(fo4ide_test_audio_service unit/AudioServiceTests.cpp fo4ide_app)
fo4ide_add_test(fo4ide_test_material_target unit/MaterialTargetTests.cpp fo4ide_app)
fo4ide_add_test(fo4ide_test_settings_security unit/SettingsTargetSecurityTests.cpp fo4ide_app)
fo4ide_add_test(fo4ide_test_papyrus_target unit/PapyrusTargetTests.cpp fo4ide_app)
fo4ide_add_test(fo4ide_test_plugin_backend_target unit/PluginBackendTargetTests.cpp fo4ide_app)
fo4ide_add_test(fo4ide_test_plugin_element_routing unit/PluginBackendElementRoutingTests.cpp fo4ide_app)
fo4ide_add_test(fo4ide_test_mo2 unit/Mo2ProfileTests.cpp fo4ide_mo2)
fo4ide_add_test(fo4ide_test_app_interop unit/AppInteropTargetTests.cpp fo4ide_app)
fo4ide_add_test(fo4ide_test_app_interop_rollback unit/AppInteropRollbackTests.cpp fo4ide_app)

# Fixture trees are found through an absolute path rather than __FILE__:
# ccache's base_dir rewrites __FILE__ to a build-relative path, which no
# longer resolves from the directory ctest runs the tests in.
foreach(fixture_test
        fo4ide_test_graph fo4ide_test_papyrus fo4ide_test_corpus fo4ide_test_service_targets fo4ide_test_papyrus_target)
    target_compile_definitions(
        ${fixture_test}
        PRIVATE FO4IDE_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    )
endforeach()

add_executable(
    fo4ide_test_plugin_backend
    unit/RadiclePluginBackendTests.cpp
    unit/RadiclePluginStringsTests.cpp
    unit/RadiclePluginWorklistTests.cpp
    unit/RadiclePluginMastersTests.cpp
    unit/RadiclePluginMutationTests.cpp
    unit/RadiclePluginLifecycleTests.cpp
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
    unit/RadiclePluginFormLinkListTests.cpp
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
    unit/RadiclePluginTypedRecordsTests.cpp
    unit/RadiclePluginTypedEntryEditTests.cpp
    unit/RadiclePluginSoundTests.cpp
    unit/RadiclePluginPackageTests.cpp
    unit/RadiclePluginEyesTests.cpp
    unit/RadiclePluginEyesGridTests.cpp
    unit/RadiclePluginLvspTests.cpp
    unit/RadiclePluginLvspGridTests.cpp
    unit/RadiclePluginBatchMutationTests.cpp
    unit/RadiclePluginAddMastersTests.cpp
    unit/RadiclePluginCopyAsWrapperTests.cpp
    unit/RadiclePluginItmNestedTests.cpp
    unit/RadiclePluginCompareTests.cpp
    unit/RadiclePluginReachableTests.cpp
    unit/RadiclePluginReadApiTests.cpp
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
    unit/RadiclePluginQuestEventDataTests.cpp
    unit/RadiclePluginQuestPartialFormTests.cpp
    unit/RadiclePluginQuestCreateLevelTests.cpp
    unit/RadiclePluginQuestConditionDefaultsTests.cpp
    unit/RadiclePluginDeferredLocalizationTests.cpp
    unit/RadiclePluginPendingLifecycleTests.cpp
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
    PRIVATE fo4ide_plugin fo4ide_app Catch2::Catch2WithMain ZLIB::ZLIB
)
target_compile_definitions(fo4ide_test_plugin_backend PRIVATE FO4IDE_TEST_HOOKS)
fo4ide_enable_warnings(fo4ide_test_plugin_backend)
fo4ide_enable_sanitizers(fo4ide_test_plugin_backend)
catch_discover_tests(fo4ide_test_plugin_backend PROPERTIES TIMEOUT ${FO4IDE_TEST_TIMEOUT})

add_executable(fo4ide_test_sound_semantic_real unit/RadiclePluginSoundSemanticRealTests.cpp)
target_link_libraries(fo4ide_test_sound_semantic_real PRIVATE fo4ide_plugin Catch2::Catch2WithMain)
fo4ide_enable_warnings(fo4ide_test_sound_semantic_real)
fo4ide_enable_sanitizers(fo4ide_test_sound_semantic_real)
catch_discover_tests(fo4ide_test_sound_semantic_real PROPERTIES TIMEOUT 1800)

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
catch_discover_tests(fo4ide_test_archive_backend PROPERTIES TIMEOUT ${FO4IDE_TEST_TIMEOUT})

fo4ide_use_utf8_code_page()

if(FO4IDE_BUILD_HTTP)
    add_subdirectory(integration)
endif()
