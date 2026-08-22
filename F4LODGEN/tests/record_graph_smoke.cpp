#include "f4lod/record_graph.hpp"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

void append_u16(std::vector<unsigned char>& out, std::uint16_t value) {
    out.push_back(static_cast<unsigned char>(value & 0xFFU));
    out.push_back(static_cast<unsigned char>((value >> 8U) & 0xFFU));
}

void append_u32(std::vector<unsigned char>& out, std::uint32_t value) {
    out.push_back(static_cast<unsigned char>(value & 0xFFU));
    out.push_back(static_cast<unsigned char>((value >> 8U) & 0xFFU));
    out.push_back(static_cast<unsigned char>((value >> 16U) & 0xFFU));
    out.push_back(static_cast<unsigned char>((value >> 24U) & 0xFFU));
}

void append_signature(std::vector<unsigned char>& out, std::string_view signature) {
    assert(signature.size() == 4);
    out.insert(out.end(), signature.begin(), signature.end());
}

void append_subrecord(
    std::vector<unsigned char>& out,
    std::string_view type,
    const std::vector<unsigned char>& data) {
    append_signature(out, type);
    append_u16(out, static_cast<std::uint16_t>(data.size()));
    out.insert(out.end(), data.begin(), data.end());
}

void append_record(
    std::vector<unsigned char>& out,
    std::string_view type,
    std::uint32_t form_id,
    const std::vector<unsigned char>& data = {}) {
    append_signature(out, type);
    append_u32(out, static_cast<std::uint32_t>(data.size()));
    append_u32(out, 0);
    append_u32(out, form_id);
    append_u32(out, 0);
    append_u16(out, 131);
    append_u16(out, 0);
    out.insert(out.end(), data.begin(), data.end());
}

void append_group_header(std::vector<unsigned char>& out, std::uint32_t group_size) {
    append_signature(out, "GRUP");
    append_u32(out, group_size);
    append_signature(out, "WRLD");
    append_u32(out, 0);
    append_u32(out, 0);
    append_u32(out, 0);
}

void write_plugin(
    const std::filesystem::path& path,
    const std::vector<std::string>& masters,
    const std::vector<std::pair<std::string, std::uint32_t>>& records) {
    std::vector<unsigned char> tes4_data;
    append_subrecord(tes4_data, "HEDR", std::vector<unsigned char>(12, 0));
    for (const auto& master_name : masters) {
        std::vector<unsigned char> master(master_name.begin(), master_name.end());
        master.push_back(0);
        append_subrecord(tes4_data, "MAST", master);
    }

    std::vector<unsigned char> bytes;
    append_record(bytes, "TES4", 0, tes4_data);

    if (!records.empty()) {
        constexpr std::uint32_t record_header_size = 24;
        append_group_header(
            bytes,
            record_header_size + static_cast<std::uint32_t>(records.size()) * record_header_size);
        for (const auto& [type, form_id] : records) append_record(bytes, type, form_id);
    }

    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

} // namespace

int main() {
    const auto root = std::filesystem::temp_directory_path() / "f4lodgen-record-graph-smoke";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    const auto a = root / "A.esp";
    const auto b = root / "B.esp";
    write_plugin(
        a,
        {"Fallout4.esm"},
        {
            {"STAT", 0x01000100U},
            {"WRLD", 0x01000200U},
        });
    write_plugin(
        b,
        {"Fallout4.esm", "A.esp"},
        {
            {"STAT", 0x01000100U},
            {"REFR", 0x02000300U},
        });

    const std::vector<f4lod::RecordGraphPlugin> load_order{
        {0, "A.esp", a},
        {1, "B.esp", b},
    };
    const auto graph = f4lod::build_lod_record_graph(load_order);

    assert(graph.valid);
    assert(graph.diagnostics.empty());
    assert(graph.counts.candidates == 4);
    assert(graph.counts.overrides == 1);
    assert(graph.counts.winners == 3);
    assert(graph.counts.worldspaces == 1);
    assert(graph.counts.references == 1);
    assert(graph.counts.statics == 1);

    assert(graph.sources_low_to_high.size() == 2U);
    assert(graph.sources_low_to_high[0].name == "A.esp");
    assert(graph.sources_low_to_high[0].masters.size() == 1U);
    assert(graph.sources_low_to_high[0].masters[0] == "Fallout4.esm");
    assert(graph.sources_low_to_high[1].name == "B.esp");
    assert(graph.sources_low_to_high[1].masters.size() == 2U);
    assert(graph.sources_low_to_high[1].masters[1] == "A.esp");
    assert(f4lod::find_record_graph_source(graph, "b.ESP") != nullptr);

    f4lod::FormKey resolved;
    std::string resolution_error;
    // B.esp master index 1 points at A.esp.
    assert(f4lod::resolve_record_form_id(graph, "B.esp", 0x01000123U, resolved, resolution_error));
    assert(resolved.plugin == "A.esp" && resolved.id == 0x123U);
    // B.esp own-file index equals the declared master count (2).
    assert(f4lod::resolve_record_form_id(graph, "B.esp", 0x02000456U, resolved, resolution_error));
    assert(resolved.plugin == "B.esp" && resolved.id == 0x456U);
    assert(!f4lod::resolve_record_form_id(graph, "B.esp", 0x03000001U, resolved, resolution_error));
    assert(!resolution_error.empty());

    assert(graph.candidates_low_to_high.size() == 4U);
    assert(graph.candidates_low_to_high[0].source_plugin == "A.esp");
    assert(graph.candidates_low_to_high[1].source_plugin == "A.esp");
    assert(graph.candidates_low_to_high[2].source_plugin == "B.esp");
    assert(graph.candidates_low_to_high[3].source_plugin == "B.esp");

    bool found_overridden_static = false;
    f4lod::FormKey overridden_static_key;
    for (const auto& winner : graph.winners) {
        if (winner.type == "STAT" && winner.key.plugin == "A.esp" && winner.key.id == 0x100U) {
            found_overridden_static = true;
            overridden_static_key = winner.key;
            assert(winner.source_plugin == "B.esp");
            assert(winner.source_load_order_index == 1);
            assert(f4lod::format_form_key(winner.key) == "000100:A.esp");
        }
    }
    assert(found_overridden_static);
    assert(f4lod::find_winning_record(graph, overridden_static_key, "STAT") != nullptr);
    assert(f4lod::find_winning_record(graph, overridden_static_key, "LAND") == nullptr);

    const auto* chain = f4lod::find_override_chain(graph, overridden_static_key);
    assert(chain != nullptr);
    assert(chain->type == "STAT");
    assert(chain->records_low_to_high.size() == 2U);
    assert(chain->records_low_to_high[0].source_plugin == "A.esp");
    assert(chain->records_low_to_high[0].source_load_order_index == 0U);
    assert(chain->records_low_to_high[1].source_plugin == "B.esp");
    assert(chain->records_low_to_high[1].source_load_order_index == 1U);
    assert(chain->records_low_to_high.back().source_plugin == "B.esp");

    const auto invalid = root / "Invalid.esp";
    write_plugin(invalid, {"Fallout4.esm"}, {{"LAND", 0x02000400U}});
    const std::vector<f4lod::RecordGraphPlugin> invalid_load_order{
        {0, "Invalid.esp", invalid},
    };
    const auto invalid_graph = f4lod::build_lod_record_graph(invalid_load_order);
    assert(!invalid_graph.valid);
    assert(!invalid_graph.diagnostics.empty());
    assert(invalid_graph.diagnostics[0].code == "invalid-record-master-index");
    assert(invalid_graph.candidates_low_to_high.empty());

    std::filesystem::remove_all(root);
    return 0;
}
