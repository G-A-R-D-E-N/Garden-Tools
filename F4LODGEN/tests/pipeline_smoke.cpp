#include "f4lod/pipeline.hpp"

#include <cassert>
#include <string_view>

int main() {
    const auto stages = f4lod::default_pipeline();

    assert(stages.size() == 11);
    assert(stages.front().stage == f4lod::PipelineStage::discover_game_data);
    assert(stages.back().stage == f4lod::PipelineStage::package_output);
    assert(f4lod::stage_name(f4lod::PipelineStage::generate_terrain) == std::string_view{"generate-terrain"});
    assert(f4lod::stage_name(f4lod::PipelineStage::generate_missing_lod_models) == std::string_view{"generate-missing-lod-models"});

    return 0;
}
