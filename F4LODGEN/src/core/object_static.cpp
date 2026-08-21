#include "f4lod/object_static.hpp"

#include <algorithm>
#include <cstddef>
#include <string_view>

namespace f4lod {
namespace {

std::string read_fixed_forward_string(
    const std::vector<unsigned char>& data,
    std::size_t offset,
    std::size_t width) {
    if (offset >= data.size()) return {};
    const auto end = std::min(data.size(), offset + width);
    auto cursor = offset;
    while (cursor < end && data[cursor] != 0U) ++cursor;
    return std::string(
        reinterpret_cast<const char*>(data.data() + offset),
        cursor - offset);
}

bool resolve_one_asset(
    const std::string& path,
    const AssetResolver& resolver,
    ObjectLodResolvedAsset& out,
    std::string& error) {
    out.requested_path = path;
    if (path.empty()) return true;
    out.resolution = resolver.resolve(path);
    if (!out.resolution.valid) {
        error = "Could not resolve object LOD asset path " + path + ": " + out.resolution.error;
        return false;
    }
    return true;
}

} // namespace

bool ObjectLodResolvedAsset::available() const noexcept {
    return resolution.valid && resolution.winner() != nullptr;
}

ObjectLodStaticMetadata decode_object_lod_static_payload(
    const WinningRecord& record,
    const RecordPayload& payload) {
    ObjectLodStaticMetadata result;
    result.key = record.key;
    result.flags = record.flags;
    result.visible_when_distant =
        (record.flags & kFo4StaticHasDistantLodFlag) != 0U;

    if (record.type != "STAT") {
        result.error = "Object LOD static metadata decoder requires a STAT record.";
        return result;
    }
    if (!payload.valid) {
        result.error = payload.error.empty()
            ? "Could not read STAT record payload."
            : payload.error;
        return result;
    }

    if (const auto* edid = find_subrecord(payload, "EDID")) {
        result.editor_id = read_zstring(*edid);
    }
    if (const auto* modl = find_subrecord(payload, "MODL")) {
        result.full_model = read_zstring(*modl);
    }

    if (const auto* mnam = find_subrecord(payload, "MNAM")) {
        constexpr std::size_t expected =
            kFo4StaticLodPathSlotSize * kFo4StaticLodPathSlotCount;
        if (mnam->data.size() < expected) {
            result.error = "STAT MNAM Distant LOD payload is shorter than four 260-byte mesh slots.";
            return result;
        }
        for (std::size_t i = 0; i < kFo4StaticLodPathSlotCount; ++i) {
            result.authored_lod_models[i] = read_fixed_forward_string(
                mnam->data,
                i * kFo4StaticLodPathSlotSize,
                kFo4StaticLodPathSlotSize);
        }
    }

    result.valid = true;
    return result;
}

ObjectLodStaticMetadata decode_object_lod_static(
    const WinningRecord& record) {
    const auto payload = read_record_payload(record);
    return decode_object_lod_static_payload(record, payload);
}

ObjectLodStaticAssetSet resolve_object_lod_static_assets(
    const ObjectLodStaticMetadata& metadata,
    const AssetResolver& resolver) {
    ObjectLodStaticAssetSet result;
    result.metadata = metadata;
    if (!metadata.valid) {
        result.error = metadata.error.empty()
            ? "Object LOD static metadata is invalid."
            : metadata.error;
        return result;
    }

    if (!resolve_one_asset(
            metadata.full_model,
            resolver,
            result.full_model,
            result.error)) {
        return result;
    }

    for (std::size_t i = 0; i < metadata.authored_lod_models.size(); ++i) {
        if (!resolve_one_asset(
                metadata.authored_lod_models[i],
                resolver,
                result.authored_lod_models[i],
                result.error)) {
            return result;
        }
    }

    result.valid = true;
    return result;
}

} // namespace f4lod
