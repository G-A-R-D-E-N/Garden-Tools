add_library(fo4ide_core INTERFACE)
target_include_directories(fo4ide_core INTERFACE "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(fo4ide_core INTERFACE nlohmann_json::nlohmann_json)

add_library(fo4ide_plugin_api INTERFACE)
target_link_libraries(fo4ide_plugin_api INTERFACE fo4ide_core)

add_library(
    fo4ide_plugin
    plugin/RadiclePluginBackend.cpp
    plugin/RadiclePluginQueries.cpp
    plugin/RadiclePluginConditions.cpp
    plugin/RadiclePluginPending.cpp
    plugin/RadiclePluginAnalysis.cpp
    plugin/RadiclePluginMasters.cpp
    plugin/RadiclePluginMutation.cpp
    plugin/RadiclePluginLifecycle.cpp
    plugin/RadiclePluginProjExplHazd.cpp
    plugin/RadiclePluginCreate.cpp
    plugin/RadiclePluginScalar.cpp
    plugin/RadiclePluginFields.cpp
    plugin/RadiclePluginArmor.cpp
    plugin/RadiclePluginGraphCache.cpp
    plugin/RadiclePluginEffects.cpp
    plugin/RadiclePluginFormLinkList.cpp
    plugin/RadiclePluginComponents.cpp
    plugin/RadiclePluginQuest.cpp
    plugin/RadiclePluginQuestArgs.cpp
    plugin/RadiclePluginQuestFormKeys.cpp
    plugin/RadiclePluginQuestCondition.cpp
    plugin/RadiclePluginQuestAliasFill.cpp
    plugin/RadiclePluginQuestAliases.cpp
    plugin/RadiclePluginQuestExisting.cpp
    plugin/RadiclePluginQuestExistingExtras.cpp
    plugin/RadiclePluginQuestLocalized.cpp
    plugin/RadiclePluginStrings.cpp
    plugin/RadiclePluginStringsEdit.cpp
    plugin/RadiclePluginElement.cpp
    plugin/RadiclePluginMaintenance.cpp
    plugin/RadiclePluginCompact.cpp
    plugin/RadiclePluginRenumber.cpp
    plugin/RadiclePluginInject.cpp
    plugin/RadiclePluginDeepCopy.cpp
    plugin/RadiclePluginReferenceRemap.cpp
    plugin/RadiclePluginOverridePatch.cpp
    plugin/RadiclePluginLeveled.cpp
    plugin/RadiclePluginPerk.cpp
    plugin/RadiclePluginSeq.cpp
    plugin/RadiclePluginCompare.cpp
    plugin/RadiclePluginDisabledOverride.cpp
    plugin/RadiclePluginCopyAsWrapper.cpp
    plugin/RadiclePluginCheck.cpp
    plugin/RadiclePluginBatchMutation.cpp
    plugin/RadiclePluginBulkElements.cpp
    plugin/RadiclePluginMasterMutation.cpp
    plugin/RadiclePluginReadApi.cpp
    plugin/RadiclePluginEsl.cpp
    plugin/RadiclePluginCell.cpp
    plugin/RadiclePluginTypedRecords.cpp
    plugin/RadiclePluginSound.cpp
    plugin/RadiclePluginPackage.cpp
    plugin/RadiclePluginScripts.cpp
    plugin/RadiclePluginMemory.cpp
    plugin/ConditionCodec.cpp
    plugin/ConditionFunctions.cpp
)
target_link_libraries(fo4ide_plugin PUBLIC fo4ide_plugin_api fo4 PRIVATE ZLIB::ZLIB)
fo4ide_enable_warnings(fo4ide_plugin)
fo4ide_enable_sanitizers(fo4ide_plugin)
if(FO4IDE_BUILD_TESTS)
    target_compile_definitions(fo4ide_plugin PRIVATE FO4IDE_TEST_HOOKS)
endif()

add_library(fo4ide_logging logging/Logger.cpp)
target_link_libraries(fo4ide_logging PUBLIC fo4ide_core spdlog::spdlog)
fo4ide_enable_warnings(fo4ide_logging)
fo4ide_enable_sanitizers(fo4ide_logging)

add_library(fo4ide_settings settings/Settings.cpp)
target_link_libraries(fo4ide_settings PUBLIC fo4ide_core nlohmann_json::nlohmann_json)
fo4ide_enable_warnings(fo4ide_settings)
fo4ide_enable_sanitizers(fo4ide_settings)

add_library(fo4ide_process process/ProcessRunner.cpp)
target_link_libraries(fo4ide_process PUBLIC fo4ide_core PRIVATE reproc++)
fo4ide_enable_warnings(fo4ide_process)
fo4ide_enable_sanitizers(fo4ide_process)

add_library(fo4ide_platform platform/PlatformServices.cpp)
target_link_libraries(fo4ide_platform PUBLIC fo4ide_core fo4ide_process)
if(WIN32)
    target_link_libraries(fo4ide_platform PRIVATE ole32 shell32 uuid)
endif()
fo4ide_enable_warnings(fo4ide_platform)
fo4ide_enable_sanitizers(fo4ide_platform)

add_library(fo4ide_window platform/NativeWindow.cpp)
target_link_libraries(fo4ide_window PUBLIC fo4ide_core)
if(WIN32)
    target_include_directories(fo4ide_window PRIVATE "${FO4IDE_WEBVIEW2_INCLUDE_DIR}")
    target_compile_definitions(fo4ide_window PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(
        fo4ide_window
        PRIVATE
            "${FO4IDE_WEBVIEW2_LOADER_LIB}"
            user32
            gdi32
            ole32
            oleaut32
            shell32
            shlwapi
            version
    )
endif()
fo4ide_enable_warnings(fo4ide_window)
fo4ide_enable_sanitizers(fo4ide_window)

add_library(fo4ide_mo2 mo2/Mo2Profile.cpp)
target_link_libraries(fo4ide_mo2 PUBLIC fo4ide_core fo4)
fo4ide_enable_warnings(fo4ide_mo2)
fo4ide_enable_sanitizers(fo4ide_mo2)

add_library(fo4ide_tools tools/ToolLocator.cpp)
target_link_libraries(fo4ide_tools PUBLIC fo4ide_core fo4ide_settings)
if(WIN32)
    target_link_libraries(fo4ide_tools PRIVATE advapi32)
endif()
fo4ide_enable_warnings(fo4ide_tools)
fo4ide_enable_sanitizers(fo4ide_tools)

add_library(fo4ide_events events/EventBus.cpp)
target_link_libraries(fo4ide_events PUBLIC fo4ide_core nlohmann_json::nlohmann_json)
fo4ide_enable_warnings(fo4ide_events)
fo4ide_enable_sanitizers(fo4ide_events)

add_library(fo4ide_rpc rpc/RpcDispatcher.cpp)
target_link_libraries(fo4ide_rpc PUBLIC fo4ide_core nlohmann_json::nlohmann_json)
fo4ide_enable_warnings(fo4ide_rpc)
fo4ide_enable_sanitizers(fo4ide_rpc)

add_library(fo4ide_mcp mcp/McpServer.cpp)
target_link_libraries(fo4ide_mcp PUBLIC fo4ide_core fo4ide_rpc nlohmann_json::nlohmann_json)
fo4ide_enable_warnings(fo4ide_mcp)
fo4ide_enable_sanitizers(fo4ide_mcp)

add_library(fo4ide_nif assets/NifService.cpp assets/NifRender.cpp)
target_link_libraries(fo4ide_nif PUBLIC fo4ide_core fo4ide_process fo4 nlohmann_json::nlohmann_json)
fo4ide_enable_warnings(fo4ide_nif)
fo4ide_enable_sanitizers(fo4ide_nif)

add_library(fo4ide_audio audio/AudioService.cpp)
target_link_libraries(fo4ide_audio PUBLIC fo4ide_core fo4ide_process fo4ide_tools)
fo4ide_enable_warnings(fo4ide_audio)
fo4ide_enable_sanitizers(fo4ide_audio)

add_library(fo4ide_material material/MaterialService.cpp)
target_link_libraries(fo4ide_material PUBLIC fo4ide_core nlohmann_json::nlohmann_json fo4)
fo4ide_enable_warnings(fo4ide_material)
fo4ide_enable_sanitizers(fo4ide_material)

add_library(
    fo4ide_textures
    textures/DdsCodec.cpp
    textures/BcnDecoder.cpp
    textures/DdsDecoder.cpp
    textures/PngWriter.cpp
)
target_link_libraries(fo4ide_textures PUBLIC fo4ide_core ZLIB::ZLIB)
fo4ide_enable_warnings(fo4ide_textures)
fo4ide_enable_sanitizers(fo4ide_textures)

add_library(
    fo4ide_archive
    archive/ArchiveService.cpp
    archive/ArchiveBackendCompatibility.cpp
    archive/RadicleArchiveBackend.cpp
    archive/Ba2Reader.cpp
)
target_link_libraries(
    fo4ide_archive
    PUBLIC fo4ide_core fo4ide_process fo4ide_tools nlohmann_json::nlohmann_json fo4
    PRIVATE fo4ide_textures
)
fo4ide_enable_warnings(fo4ide_archive)
fo4ide_enable_sanitizers(fo4ide_archive)

add_library(
    fo4ide_papyrus
    papyrus/Lexer.cpp
    papyrus/Parser.cpp
    papyrus/Type.cpp
    papyrus/Binding.cpp
    papyrus/ScriptIndex.cpp
    papyrus/Resolver.cpp
    papyrus/TypeChecker.cpp
    papyrus/CodeGenerator.cpp
    papyrus/Decompiler.cpp
    papyrus/Symbols.cpp
    papyrus/CreationKitFidelity.cpp
    papyrus/AnalysisService.cpp
    papyrus/CompileScript.cpp
)
target_link_libraries(fo4ide_papyrus PUBLIC fo4ide_core fo4)
fo4ide_enable_warnings(fo4ide_papyrus)
fo4ide_enable_sanitizers(fo4ide_papyrus)

add_library(
    fo4ide_graph
    graph/GraphDocument.cpp
    graph/WikiDocs.cpp
    graph/BuiltinNodeDefinitions.cpp
    graph/NodePalette.cpp
    graph/GraphTypeResolver.cpp
    graph/GraphValidator.cpp
    graph/GraphExecFlow.cpp
    graph/GraphDefiniteAssignment.cpp
    graph/GraphNameTable.cpp
    graph/GraphLinearizer.cpp
    graph/PapyrusSourceWriter.cpp
    graph/GraphCompiler.cpp
    graph/GraphLifter.cpp
    graph/GraphScriptLoader.cpp
    graph/GraphToolService.cpp
)
target_link_libraries(fo4ide_graph PUBLIC fo4ide_core fo4ide_papyrus)
fo4ide_enable_warnings(fo4ide_graph)
fo4ide_enable_sanitizers(fo4ide_graph)

add_library(
    fo4ide_f4se
    f4se/TypeMap.cpp
    f4se/BindingValidator.cpp
    f4se/CppScanner.cpp
    f4se/RegionMerge.cpp
    f4se/RegistrationExtractor.cpp
    f4se/NativeOracle.cpp
    f4se/CrossCheck.cpp
    f4se/VersionDiff.cpp
    f4se/CppEmitter.cpp
    f4se/PapyrusHeaderEmitter.cpp
    f4se/PluginEmitter.cpp
    f4se/Emitter.cpp
)
target_link_libraries(fo4ide_f4se PUBLIC fo4ide_core fo4ide_papyrus)
fo4ide_enable_warnings(fo4ide_f4se)
fo4ide_enable_sanitizers(fo4ide_f4se)

add_library(
    fo4ide_assets
    assets/TextureResolver.cpp
)
target_link_libraries(
    fo4ide_assets
    PUBLIC
        fo4ide_core
        fo4ide_archive
        fo4ide_material
        fo4ide_textures
        nlohmann_json::nlohmann_json
)
fo4ide_enable_warnings(fo4ide_assets)
fo4ide_enable_sanitizers(fo4ide_assets)

add_library(
    fo4ide_app
    app/ServiceTargets.cpp
    app/PapyrusTarget.cpp
    app/PapyrusStaging.cpp
    app/GraphTarget.cpp
    app/AppInteropTarget.cpp
    app/AudioTarget.cpp
    app/MaterialTarget.cpp
    app/ArchiveTarget.cpp
    app/PluginBackendTarget.cpp
    app/WikiService.cpp
)
target_link_libraries(
    fo4ide_app
    PUBLIC
        fo4ide_core
        fo4ide_rpc
        fo4ide_settings
        fo4ide_nif
        fo4ide_audio
        fo4ide_material
        fo4ide_archive
        fo4ide_assets
        fo4ide_platform
        fo4ide_process
        fo4ide_mo2
        fo4ide_tools
        fo4ide_papyrus
        fo4ide_graph
        fo4ide_plugin_api
        Threads::Threads
)
fo4ide_enable_warnings(fo4ide_app)
fo4ide_enable_sanitizers(fo4ide_app)

if(FO4IDE_BUILD_HTTP)
    add_subdirectory(http)
endif()
