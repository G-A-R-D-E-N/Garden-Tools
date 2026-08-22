#include "f4lod/commonwealth_calibration.hpp"

#include "f4lod/btr_writer.hpp"

#include <iomanip>
#include <sstream>

namespace f4lod {
namespace {

std::string json_escape(const std::string& value) {
    std::ostringstream out;
    for (const unsigned char c : value) {
        switch (c) {
        case '"': out << "\\\""; break;
        case '\\': out << "\\\\"; break;
        case '\b': out << "\\b"; break;
        case '\f': out << "\\f"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (c < 0x20U) {
                out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                    << static_cast<unsigned int>(c) << std::dec << std::setfill(' ');
            } else {
                out << static_cast<char>(c);
            }
            break;
        }
    }
    return out.str();
}

void json_string(std::ostringstream& out, const std::string& value) {
    out << '"' << json_escape(value) << '"';
}

void json_bool(std::ostringstream& out, bool value) {
    out << (value ? "true" : "false");
}

void json_float3(std::ostringstream& out, const float (&values)[3]) {
    out << '[' << values[0] << ',' << values[1] << ',' << values[2] << ']';
}

void json_float9(std::ostringstream& out, const float (&values)[9]) {
    out << '[';
    for (int i = 0; i < 9; ++i) {
        if (i != 0) out << ',';
        out << values[i];
    }
    out << ']';
}

void json_issues(std::ostringstream& out, const std::vector<std::string>& issues) {
    out << '[';
    for (std::size_t i = 0; i < issues.size(); ++i) {
        if (i != 0U) out << ',';
        json_string(out, issues[i]);
    }
    out << ']';
}

} // namespace

std::string format_commonwealth_calibration_text(
    const CommonwealthBtrCalibrationSet& calibration) {
    std::ostringstream out;
    out << "F4LODGen real Commonwealth BTR calibration\n"
        << "Archive: " << calibration.archive.string() << '\n'
        << "Valid: " << (calibration.valid ? "yes" : "no") << '\n';
    if (!calibration.error.empty()) out << "Error: " << calibration.error << '\n';

    for (const auto& level : calibration.levels) {
        out << "LOD " << level.lod_level
            << ": samples=" << level.samples.size()
            << " precision=" << terrain_btr_vertex_precision_name(level.position_precision)
            << " scale=" << level.scale
            << " flags=" << level.av_flags << '/' << level.av_flags2
            << " VF=" << static_cast<unsigned int>(level.vertex_flag3) << ','
            << static_cast<unsigned int>(level.vertex_flag4) << ','
            << static_cast<unsigned int>(level.vertex_flag5) << ','
            << static_cast<unsigned int>(level.vertex_flag8)
            << " compatible=" << (level.writer_compatible ? "yes" : "no") << '\n';
        if (!level.error.empty()) out << "  error: " << level.error << '\n';
        for (const auto& sample : level.samples) out << "  " << sample.game_path << '\n';
        for (const auto& issue : level.issues) out << "  issue: " << issue << '\n';
    }
    out << "Writer compatible: " << (calibration.writer_compatible ? "YES" : "NO") << '\n';
    return out.str();
}

std::string format_commonwealth_calibration_json(
    const CommonwealthBtrCalibrationSet& calibration) {
    std::ostringstream out;
    out << std::setprecision(9);
    out << '{';
    out << "\"schema\":\"f4lodgen.commonwealth-btr-calibration.v1\",";
    out << "\"valid\":"; json_bool(out, calibration.valid); out << ',';
    out << "\"writerCompatible\":"; json_bool(out, calibration.writer_compatible); out << ',';
    out << "\"error\":"; json_string(out, calibration.error); out << ',';
    out << "\"archive\":"; json_string(out, calibration.archive.generic_string()); out << ',';
    out << "\"levels\":[";

    for (std::size_t level_index = 0; level_index < calibration.levels.size(); ++level_index) {
        if (level_index != 0U) out << ',';
        const auto& level = calibration.levels[level_index];
        out << '{';
        out << "\"lodLevel\":" << level.lod_level << ',';
        out << "\"valid\":"; json_bool(out, level.valid); out << ',';
        out << "\"writerCompatible\":"; json_bool(out, level.writer_compatible); out << ',';
        out << "\"error\":"; json_string(out, level.error); out << ',';
        out << "\"encoding\":{";
        out << "\"positionPrecision\":";
        json_string(out, terrain_btr_vertex_precision_name(level.position_precision));
        out << ",\"scale\":" << level.scale
            << ",\"avFlags\":" << level.av_flags
            << ",\"avFlags2\":" << level.av_flags2
            << ",\"vertexFlag3\":" << static_cast<unsigned int>(level.vertex_flag3)
            << ",\"vertexFlag4\":" << static_cast<unsigned int>(level.vertex_flag4)
            << ",\"vertexFlag5\":" << static_cast<unsigned int>(level.vertex_flag5)
            << ",\"vertexFlag8\":" << static_cast<unsigned int>(level.vertex_flag8)
            << "},";
        out << "\"issues\":"; json_issues(out, level.issues); out << ',';
        out << "\"samples\":[";

        for (std::size_t sample_index = 0; sample_index < level.samples.size(); ++sample_index) {
            if (sample_index != 0U) out << ',';
            const auto& sample = level.samples[sample_index];
            const auto& c = sample.calibration;
            out << '{';
            out << "\"gamePath\":"; json_string(out, sample.game_path); out << ',';
            out << "\"valid\":"; json_bool(out, c.valid); out << ',';
            out << "\"writerCompatible\":"; json_bool(out, c.writer_compatible); out << ',';
            out << "\"issues\":"; json_issues(out, c.issues); out << ',';
            out << "\"positionPrecision\":";
            json_string(out, terrain_btr_vertex_precision_name(c.position_precision));
            out << ",\"shapeName\":"; json_string(out, c.shape_name);
            out << ",\"avFlags\":" << c.av_flags
                << ",\"avFlags2\":" << c.av_flags2
                << ",\"vertexFlag3\":" << static_cast<unsigned int>(c.vertex_flag3)
                << ",\"vertexFlag4\":" << static_cast<unsigned int>(c.vertex_flag4)
                << ",\"vertexFlag5\":" << static_cast<unsigned int>(c.vertex_flag5)
                << ",\"vertexFlag8\":" << static_cast<unsigned int>(c.vertex_flag8)
                << ",\"subindexEntries\":" << c.source_subindex_entries << ',';
            out << "\"transform\":{";
            out << "\"translation\":["
                << c.shape_transform.translation_x << ','
                << c.shape_transform.translation_y << ','
                << c.shape_transform.translation_z << "],";
            out << "\"rotation\":"; json_float9(out, c.shape_transform.rotation); out << ',';
            out << "\"scale\":" << c.shape_transform.scale << "},";
            out << "\"positionRange\":{";
            out << "\"present\":"; json_bool(out, c.has_position_range); out << ',';
            out << "\"min\":"; json_float3(out, c.position_min); out << ',';
            out << "\"max\":"; json_float3(out, c.position_max); out << "},";
            out << "\"uvRange\":{";
            out << "\"present\":"; json_bool(out, c.has_uv_range); out << ',';
            out << "\"min\":[" << c.uv_min[0] << ',' << c.uv_min[1] << "],";
            out << "\"max\":[" << c.uv_max[0] << ',' << c.uv_max[1] << "]},";
            out << "\"materialPresent\":[";
            json_bool(out, c.material_present[0]); out << ','; json_bool(out, c.material_present[1]);
            out << "]}";
        }

        out << "]}";
    }

    out << "]}\n";
    return out.str();
}

} // namespace f4lod
