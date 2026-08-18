#pragma once

#include "f4lod/assets.hpp"
#include "f4lod/record_data.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace f4lod {

inline constexpr std::uint32_t kFo4StaticHasDistantLodFlag = 0x00008000U;
inline constexpr std::size_t kFo4StaticLodPathSlotSize = 260U;
inline constexpr std::size_t kFo4StaticLodPathSlotCount = 4U;

struct ObjectLodStaticMetadata {
    bool valid{};
    std::string error;
    FormKey key;
    std::uint32_t flags{};
    bool visible_when_distant{};
    std::string editor_id;
    std::string full_model;
    std::array<std::string, kFo4StaticLodPathSlotCount> authored_lod_models;
};

struct ObjectLodResolvedAsset {
    std::string requested_path;
    AssetResolution resolution;

    [[nodiscard]] bool available() const noexcept;
};

struct ObjectLodStaticAssetSet {
    bool valid{};
    std::string error;
    ObjectLodStaticMetadata metadata;
    ObjectLodResolvedAsset full_model;
    std::array<ObjectLodResolvedAsset, kFo4StaticLodPathSlotCount> authored_lod_models;
};

[[nodiscard]] ObjectLodStaticMetadata decode_object_lod_static_payload(
    const WinningRecord& record,
    const RecordPayload& payload);

[[nodiscard]] ObjectLodStaticMetadata decode_object_lod_static(
    const WinningRecord& record);

[[nodiscard]] ObjectLodStaticAssetSet resolve_object_lod_static_assets(
    const ObjectLodStaticMetadata& metadata,
    const AssetResolver& resolver);

} // namespace f4lod
