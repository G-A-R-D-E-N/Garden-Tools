#include "f4lod/object_static.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

f4lod::SubrecordView zstring_subrecord(std::string type, std::string_view value) {
    f4lod::SubrecordView subrecord;
    subrecord.type = std::move(type);
    subrecord.data.assign(value.begin(), value.end());
    subrecord.data.push_back(0U);
    return subrecord;
}

void write_lod_slot(
    std::vector<unsigned char>& data,
    std::size_t slot,
    std::string_view value) {
    assert(slot < f4lod::kFo4StaticLodPathSlotCount);
    assert(value.size() < f4lod::kFo4StaticLodPathSlotSize);
    const auto offset = slot * f4lod::kFo4StaticLodPathSlotSize;
    std::copy(value.begin(), value.end(), data.begin() + static_cast<std::ptrdiff_t>(offset));
}

void touch(const std::filesystem::path& path) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file << "fixture";
}

} // namespace

int main() {
    f4lod::WinningRecord record;
    record.key = {"Fallout4.esm", 0x00012345U};
    record.type = "STAT";
    record.flags = f4lod::kFo4StaticHasDistantLodFlag;

    f4lod::RecordPayload payload;
    payload.valid = true;
    payload.subrecords.push_back(zstring_subrecord("EDID", "FixtureStatic"));
    payload.subrecords.push_back(zstring_subrecord("MODL", "meshes\\fixture\\full.nif"));

    f4lod::SubrecordView mnam;
    mnam.type = "MNAM";
    mnam.data.resize(
        f4lod::kFo4StaticLodPathSlotSize * f4lod::kFo4StaticLodPathSlotCount,
        0U);
    write_lod_slot(mnam.data, 0U, "meshes\\fixture\\lod0.nif");
    write_lod_slot(mnam.data, 1U, "meshes\\fixture\\lod1.nif");
    write_lod_slot(mnam.data, 2U, "meshes\\fixture\\lod2.nif");
    payload.subrecords.push_back(mnam);

    const auto metadata = f4lod::decode_object_lod_static_payload(record, payload);
    assert(metadata.valid);
    assert(metadata.key.plugin == "Fallout4.esm");
    assert(metadata.key.id == 0x00012345U);
    assert(metadata.visible_when_distant);
    assert(metadata.editor_id == "FixtureStatic");
    assert(metadata.full_model == "meshes\\fixture\\full.nif");
    assert(metadata.authored_lod_models[0] == "meshes\\fixture\\lod0.nif");
    assert(metadata.authored_lod_models[1] == "meshes\\fixture\\lod1.nif");
    assert(metadata.authored_lod_models[2] == "meshes\\fixture\\lod2.nif");
    assert(metadata.authored_lod_models[3].empty());

    auto truncated_payload = payload;
    truncated_payload.subrecords.back().data.resize(100U);
    const auto truncated = f4lod::decode_object_lod_static_payload(record, truncated_payload);
    assert(!truncated.valid);
    assert(truncated.error.find("four 260-byte") != std::string::npos);

    auto wrong_type = record;
    wrong_type.type = "MSTT";
    const auto rejected = f4lod::decode_object_lod_static_payload(wrong_type, payload);
    assert(!rejected.valid);

    const auto root = std::filesystem::temp_directory_path() / "f4lodgen-object-static-smoke";
    std::filesystem::remove_all(root);
    const auto data = root / "Data";
    touch(data / "meshes" / "fixture" / "full.nif");
    touch(data / "meshes" / "fixture" / "lod0.nif");
    touch(data / "meshes" / "fixture" / "lod2.nif");

    const f4lod::AssetResolver resolver(data, {});
    const auto assets = f4lod::resolve_object_lod_static_assets(metadata, resolver);
    assert(assets.valid);
    assert(assets.full_model.available());
    assert(assets.full_model.resolution.winner() != nullptr);
    assert(assets.full_model.resolution.winner()->kind == f4lod::AssetSourceKind::loose);
    assert(assets.authored_lod_models[0].available());
    assert(!assets.authored_lod_models[1].available());
    assert(assets.authored_lod_models[2].available());
    assert(assets.authored_lod_models[3].requested_path.empty());

    std::filesystem::remove_all(root);
    return 0;
}
