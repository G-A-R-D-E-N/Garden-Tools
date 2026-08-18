cmake_minimum_required(VERSION 3.25)

project(F4LODGen VERSION 0.1.0 LANGUAGES C CXX)

option(F4LOD_BUILD_TESTS "Build F4LODGen smoke tests" ON)

include(FetchContent)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_FUZZERS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(INSTALL_PROJECT OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    miniz
    GIT_REPOSITORY https://github.com/richgel999/miniz.git
    GIT_TAG 77d0dce8627735138c51770d1799a1ef48f2117d
)
FetchContent_MakeAvailable(miniz)

set(MESHOPT_BUILD_DEMO OFF CACHE BOOL "" FORCE)
set(MESHOPT_BUILD_GLTFPACK OFF CACHE BOOL "" FORCE)
set(MESHOPT_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(MESHOPT_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    meshoptimizer
    GIT_REPOSITORY https://github.com/zeux/meshoptimizer.git
    GIT_TAG 97bbdce4716f6257c9527b051515136882f33e79
)
FetchContent_MakeAvailable(meshoptimizer)

add_library(f4lod_core
    src/core/archive_order.cpp
    src/core/assets.cpp
    src/core/ba2.cpp
    src/core/btr_calibration.cpp
    src/core/btr_geometry.cpp
    src/core/btr_material.cpp
    src/core/btr_material_writer.cpp
    src/core/btr_probe.cpp
    src/core/btr_writer.cpp
    src/core/commonwealth_calibration.cpp
    src/core/commonwealth_calibration_report.cpp
    src/core/dds.cpp
    src/core/landscape.cpp
    src/core/lod_settings.cpp
    src/core/nif.cpp
    src/core/nif_writer.cpp
    src/core/object_lod.cpp
    src/core/pipeline.cpp
    src/core/plugin.cpp
    src/core/record_data.cpp
    src/core/record_graph.cpp
    src/core/scanner.cpp
    src/core/terrain.cpp
    src/core/terrain_block_mesh.cpp
    src/core/terrain_blocks.cpp
    src/core/terrain_compile.cpp
    src/core/terrain_greedy.cpp
    src/core/terrain_job.cpp
    src/core/terrain_mesh.cpp
    src/core/terrain_preview.cpp
    src/core/terrain_profile.cpp
    src/core/terrain_quality.cpp
    src/core/terrain_simplify.cpp
    src/core/terrain_source_audit.cpp
    src/core/terrain_texture_graph.cpp
    src/core/terrain_unseen.cpp
    src/core/worldspace.cpp
)

target_include_directories(f4lod_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(f4lod_core PRIVATE miniz meshoptimizer)
target_compile_features(f4lod_core PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(f4lod_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(f4lod_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(f4lodgen src/cli/main.cpp)
target_link_libraries(f4lodgen PRIVATE f4lod_core)
target_compile_features(f4lodgen PRIVATE cxx_std_20)

add_executable(f4lod-btr-probe src/cli/btr_probe_main.cpp)
target_link_libraries(f4lod-btr-probe PRIVATE f4lod_core)
target_compile_features(f4lod-btr-probe PRIVATE cxx_std_20)

add_executable(f4lod-calibrate-commonwealth src/cli/commonwealth_calibration_main.cpp)
target_link_libraries(f4lod-calibrate-commonwealth PRIVATE f4lod_core)
target_compile_features(f4lod-calibrate-commonwealth PRIVATE cxx_std_20)

if(F4LOD_BUILD_TESTS)
    enable_testing()
    function(f4lod_add_smoke target source test_name)
        add_executable(${target} ${source})
        target_link_libraries(${target} PRIVATE f4lod_core)
        target_compile_features(${target} PRIVATE cxx_std_20)
        add_test(NAME ${test_name} COMMAND ${target})
    endfunction()

    f4lod_add_smoke(f4lod_pipeline_smoke tests/pipeline_smoke.cpp pipeline-smoke)
    f4lod_add_smoke(f4lod_scanner_smoke tests/scanner_smoke.cpp scanner-smoke)
    f4lod_add_smoke(f4lod_plugin_smoke tests/plugin_smoke.cpp plugin-smoke)
    f4lod_add_smoke(f4lod_ba2_smoke tests/ba2_smoke.cpp ba2-smoke)
    f4lod_add_smoke(f4lod_ba2_texture_smoke tests/ba2_texture_smoke.cpp ba2-texture-smoke)
    f4lod_add_smoke(f4lod_dds_smoke tests/dds_smoke.cpp dds-smoke)
    f4lod_add_smoke(f4lod_archive_order_smoke tests/archive_order_smoke.cpp archive-order-smoke)
    f4lod_add_smoke(f4lod_record_graph_smoke tests/record_graph_smoke.cpp record-graph-smoke)
    f4lod_add_smoke(f4lod_group_context_smoke tests/group_context_smoke.cpp group-context-smoke)
    f4lod_add_smoke(f4lod_worldspace_smoke tests/worldspace_smoke.cpp worldspace-smoke)
    f4lod_add_smoke(f4lod_record_data_smoke tests/record_data_smoke.cpp record-data-smoke)
    f4lod_add_smoke(f4lod_landscape_smoke tests/landscape_smoke.cpp landscape-smoke)
    f4lod_add_smoke(f4lod_terrain_smoke tests/terrain_smoke.cpp terrain-smoke)
    f4lod_add_smoke(f4lod_terrain_land_selection_smoke tests/terrain_land_selection_smoke.cpp terrain-land-selection-smoke)
    f4lod_add_smoke(f4lod_terrain_mesh_smoke tests/terrain_mesh_smoke.cpp terrain-mesh-smoke)
    f4lod_add_smoke(f4lod_terrain_blocks_smoke tests/terrain_blocks_smoke.cpp terrain-blocks-smoke)
    f4lod_add_smoke(f4lod_terrain_block_mesh_smoke tests/terrain_block_mesh_smoke.cpp terrain-block-mesh-smoke)
    f4lod_add_smoke(f4lod_terrain_greedy_smoke tests/terrain_greedy_smoke.cpp terrain-greedy-smoke)
    f4lod_add_smoke(f4lod_terrain_simplify_smoke tests/terrain_simplify_smoke.cpp terrain-simplify-smoke)
    f4lod_add_smoke(f4lod_terrain_quality_smoke tests/terrain_quality_smoke.cpp terrain-quality-smoke)
    f4lod_add_smoke(f4lod_terrain_unseen_smoke tests/terrain_unseen_smoke.cpp terrain-unseen-smoke)
    f4lod_add_smoke(f4lod_terrain_source_audit_smoke tests/terrain_source_audit_smoke.cpp terrain-source-audit-smoke)
    f4lod_add_smoke(f4lod_terrain_profile_smoke tests/terrain_profile_smoke.cpp terrain-profile-smoke)
    f4lod_add_smoke(f4lod_terrain_texture_graph_smoke tests/terrain_texture_graph_smoke.cpp terrain-texture-graph-smoke)
    f4lod_add_smoke(f4lod_terrain_job_smoke tests/terrain_job_smoke.cpp terrain-job-smoke)
    f4lod_add_smoke(f4lod_terrain_compile_smoke tests/terrain_compile_smoke.cpp terrain-compile-smoke)
    f4lod_add_smoke(f4lod_terrain_preview_smoke tests/terrain_preview_smoke.cpp terrain-preview-smoke)
    f4lod_add_smoke(f4lod_lod_settings_smoke tests/lod_settings_smoke.cpp lod-settings-smoke)
    f4lod_add_smoke(f4lod_assets_smoke tests/assets_smoke.cpp assets-smoke)
    f4lod_add_smoke(f4lod_nif_smoke tests/nif_smoke.cpp nif-smoke)
    f4lod_add_smoke(f4lod_btr_geometry_smoke tests/btr_geometry_smoke.cpp btr-geometry-smoke)
    f4lod_add_smoke(f4lod_nif_writer_smoke tests/nif_writer_smoke.cpp nif-writer-smoke)
    f4lod_add_smoke(f4lod_btr_writer_smoke tests/btr_writer_smoke.cpp btr-writer-smoke)
    f4lod_add_smoke(f4lod_btr_material_smoke tests/btr_material_smoke.cpp btr-material-smoke)
    f4lod_add_smoke(f4lod_btr_probe_smoke tests/btr_probe_smoke.cpp btr-probe-smoke)
    f4lod_add_smoke(f4lod_btr_calibration_smoke tests/btr_calibration_smoke.cpp btr-calibration-smoke)
    f4lod_add_smoke(f4lod_commonwealth_calibration_report_smoke tests/commonwealth_calibration_report_smoke.cpp commonwealth-calibration-report-smoke)
    f4lod_add_smoke(f4lod_object_lod_smoke tests/object_lod_smoke.cpp object-lod-smoke)
endif()
