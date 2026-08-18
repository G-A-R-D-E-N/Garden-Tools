#include "f4lod/commonwealth_calibration.hpp"

#include <cassert>
#include <string>

int main() {
    f4lod::CommonwealthBtrCalibrationSet report;
    report.valid = true;
    report.writer_compatible = false;
    report.archive = "D:/Fallout 4/Data/Fallout4 - Meshes.ba2";

    f4lod::CommonwealthLevelCalibration level;
    level.valid = true;
    level.writer_compatible = false;
    level.lod_level = 4U;
    level.position_precision = f4lod::TerrainBtrVertexPrecision::half;
    level.av_flags = 14U;
    level.vertex_flag3 = 101U;
    level.scale = 4.0F;
    level.issues.push_back("fixture issue");

    f4lod::CommonwealthBtrSample sample;
    sample.game_path = "meshes/terrain/Commonwealth/Commonwealth.4.0.0.btr";
    sample.calibration.valid = true;
    sample.calibration.writer_compatible = false;
    sample.calibration.position_precision = f4lod::TerrainBtrVertexPrecision::half;
    sample.calibration.shape_name = "obj\"fixture";
    sample.calibration.av_flags = 14U;
    sample.calibration.vertex_flag3 = 101U;
    sample.calibration.source_subindex_entries = 2U;
    sample.calibration.shape_transform.translation_x = 1.0F;
    sample.calibration.shape_transform.translation_y = 2.0F;
    sample.calibration.shape_transform.translation_z = 3.0F;
    sample.calibration.shape_transform.scale = 4.0F;
    sample.calibration.has_position_range = true;
    sample.calibration.position_min[0] = -1.0F;
    sample.calibration.position_max[0] = 1.0F;
    sample.calibration.has_uv_range = true;
    sample.calibration.uv_min[0] = 0.0F;
    sample.calibration.uv_max[0] = 1.0F;
    sample.calibration.material_present[0] = true;
    sample.calibration.issues.push_back("subindex unsupported");
    level.samples.push_back(sample);
    report.levels.push_back(level);

    const std::string json = f4lod::format_commonwealth_calibration_json(report);
    assert(json.find("\"schema\":\"f4lodgen.commonwealth-btr-calibration.v1\"") != std::string::npos);
    assert(json.find("\"lodLevel\":4") != std::string::npos);
    assert(json.find("\"positionPrecision\":\"half\"") != std::string::npos);
    assert(json.find("obj\\\"fixture") != std::string::npos);
    assert(json.find("\"subindexEntries\":2") != std::string::npos);
    assert(json.find("\"transform\":{") != std::string::npos);
    assert(json.find("\"positionRange\":{") != std::string::npos);
    assert(json.find("\"uvRange\":{") != std::string::npos);
    assert(json.find("subindex unsupported") != std::string::npos);
    assert(json.back() == '\n');

    const std::string text = f4lod::format_commonwealth_calibration_text(report);
    assert(text.find("LOD 4") != std::string::npos);
    assert(text.find("Writer compatible: NO") != std::string::npos);

    return 0;
}
