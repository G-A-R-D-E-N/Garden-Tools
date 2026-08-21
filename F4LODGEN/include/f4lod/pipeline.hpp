#pragma once

#include <span>
#include <string_view>

namespace f4lod {

enum class PipelineStage {
    discover_game_data,
    resolve_load_order,
    build_record_graph,
    preflight,
    generate_terrain,
    resolve_object_lod,
    generate_missing_lod_models,
    build_lod_textures,
    generate_object_lod,
    validate_output,
    package_output,
};

struct StageDescriptor {
    PipelineStage stage;
    std::string_view id;
    std::string_view description;
};

[[nodiscard]] std::span<const StageDescriptor> default_pipeline() noexcept;
[[nodiscard]] std::string_view stage_name(PipelineStage stage) noexcept;

} // namespace f4lod
