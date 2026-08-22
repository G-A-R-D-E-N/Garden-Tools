#include "f4lod/pipeline.hpp"

#include <array>

namespace f4lod {
namespace {

constexpr std::array kPipeline{
    StageDescriptor{PipelineStage::discover_game_data, "discover-game-data", "Locate the Fallout 4 data view, plugins, loose files, and archives."},
    StageDescriptor{PipelineStage::resolve_load_order, "resolve-load-order", "Resolve active plugins and asset precedence into one deterministic input view."},
    StageDescriptor{PipelineStage::build_record_graph, "build-record-graph", "Build winning WRLD/CELL/LAND/reference/base-object relationships needed by LOD generation."},
    StageDescriptor{PipelineStage::preflight, "preflight", "Find missing assets, unsupported materials, risky reference state, and incompatible settings before generation."},
    StageDescriptor{PipelineStage::generate_terrain, "generate-terrain", "Generate terrain and water LOD geometry plus terrain textures."},
    StageDescriptor{PipelineStage::resolve_object_lod, "resolve-object-lod", "Choose authored LOD assets and determine which full models require generated LOD."},
    StageDescriptor{PipelineStage::generate_missing_lod_models, "generate-missing-lod-models", "Create material-aware simplified LOD models when safe and useful."},
    StageDescriptor{PipelineStage::build_lod_textures, "build-lod-textures", "Stitch, atlas, mip, and compress object LOD texture inputs."},
    StageDescriptor{PipelineStage::generate_object_lod, "generate-object-lod", "Combine static references into FO4 object LOD blocks and cull hidden geometry."},
    StageDescriptor{PipelineStage::validate_output, "validate-output", "Re-read generated assets and check block coverage, references, materials, textures, seams, and expected files."},
    StageDescriptor{PipelineStage::package_output, "package-output", "Write a mod-manager-ready output and optional archive package."},
};

} // namespace

std::span<const StageDescriptor> default_pipeline() noexcept {
    return kPipeline;
}

std::string_view stage_name(PipelineStage stage) noexcept {
    for (const auto& descriptor : kPipeline) {
        if (descriptor.stage == stage) {
            return descriptor.id;
        }
    }
    return "unknown";
}

} // namespace f4lod
