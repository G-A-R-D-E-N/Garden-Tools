#include "fo4/environment/environment.hpp"
#include "fo4/environment/world.hpp"
#include "fo4/forms/cell.hpp"
#include "fo4/forms/cell_edit.hpp"
#include "fo4/forms/placed_reference.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/signature.hpp"
#include "byte_helpers.hpp"
#include "cell_edit_fixture.hpp"
#include "plugin_bytes.hpp"
#include "temp_data_dir.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace fo4::test::cell_edit;

}  // namespace

TEST_F(CellEditTest, EditCellLightingReadsBackThroughDecodeCell) {
  const auto env = base_and_targets(dir_);
  const auto cell = fo4::FormKey::parse("000802:Base.esm");
  auto patch = props_patch();

  fo4::forms::CellLightingEdit lighting;
  lighting.ambient = std::array<std::uint8_t, 3>{10, 20, 30};
  lighting.directional = std::array<std::uint8_t, 3>{40, 50, 60};
  lighting.fog_near_color = std::array<std::uint8_t, 3>{70, 80, 90};
  lighting.fog_near = 1.5f;
  lighting.fog_far = 2500.0f;
  lighting.directional_rotation_xy = 45;
  lighting.directional_rotation_z = -90;
  lighting.directional_fade = 0.25f;
  lighting.fog_clip_distance = 3000.0f;
  lighting.fog_power = 2.0f;
  fo4::forms::CellEdit edit;
  edit.lighting = lighting;

  const auto result = fo4::forms::edit_cell(patch, env, cell, edit);
  ASSERT_EQ(result.status, fo4::forms::CellEditStatus::edited);
  EXPECT_EQ(result.copied_from, "Base.esm");

  const auto reread = reread_patched(dir_, patch);
  const auto* ref = reread.winner(cell);
  ASSERT_NE(ref, nullptr);
  const auto data = fo4::env::decode_cell(reread, *ref);
  ASSERT_TRUE(data.lighting.has_value());
  const auto& light = *data.lighting;
  EXPECT_EQ(light.ambient.red, 10);
  EXPECT_EQ(light.ambient.blue, 30);
  EXPECT_EQ(light.directional.green, 50);
  EXPECT_EQ(light.fog_near_color.blue, 90);
  EXPECT_FLOAT_EQ(light.fog_near, 1.5f);
  EXPECT_FLOAT_EQ(light.fog_far, 2500.0f);
  EXPECT_EQ(light.directional_rotation_xy, 45);
  EXPECT_EQ(light.directional_rotation_z, -90);
  EXPECT_FLOAT_EQ(light.directional_fade, 0.25f);
  EXPECT_FLOAT_EQ(light.fog_clip_distance, 3000.0f);
  EXPECT_FLOAT_EQ(light.fog_power, 2.0f);
}

TEST_F(CellEditTest, EditWaterAndWaterHeightReadsBackThroughResolveWater) {
  const auto env = base_and_targets(dir_);
  const auto cell = fo4::FormKey::parse("000802:Base.esm");
  const auto water_key = fo4::FormKey::parse("000900:Targets.esm");
  auto patch = props_patch();

  fo4::forms::CellEdit edit;
  edit.water = std::optional<fo4::FormKey>{water_key};
  edit.water_height = 12.0f;
  ASSERT_EQ(fo4::forms::edit_cell(patch, env, cell, edit).status, fo4::forms::CellEditStatus::edited);

  const auto reread = reread_patched(dir_, patch);
  const auto* ref = reread.winner(cell);
  ASSERT_NE(ref, nullptr);
  const auto water = fo4::env::resolve_water(reread, *ref);
  EXPECT_EQ(water.water, water_key);
  EXPECT_FALSE(water.uses_default_water);
  ASSERT_TRUE(water.height.has_value());
  EXPECT_FLOAT_EQ(*water.height, 12.0f);
}

TEST_F(CellEditTest, EditLinksRegionsAndOwnerReadBackThroughDecodeCell) {
  const auto env = base_and_targets(dir_);
  const auto cell = fo4::FormKey::parse("000802:Base.esm");
  const auto location = fo4::FormKey::parse("000903:Targets.esm");
  const auto image_space = fo4::FormKey::parse("000901:Targets.esm");
  const auto encounter_zone = fo4::FormKey::parse("000902:Targets.esm");
  const auto region = fo4::FormKey::parse("000905:Targets.esm");
  const auto second_region = fo4::FormKey::parse("000907:Targets.esm");
  const auto owner = fo4::FormKey::parse("000906:Targets.esm");
  auto patch = props_patch();

  fo4::forms::CellEdit edit;
  edit.location = std::optional<fo4::FormKey>{location};
  edit.image_space = std::optional<fo4::FormKey>{image_space};
  edit.encounter_zone = std::optional<fo4::FormKey>{encounter_zone};
  edit.regions = std::vector<fo4::FormKey>{region, second_region};
  edit.owner = std::optional<fo4::FormKey>{owner};
  edit.owner_rank = 3;
  ASSERT_EQ(fo4::forms::edit_cell(patch, env, cell, edit).status, fo4::forms::CellEditStatus::edited);

  const auto reread = reread_patched(dir_, patch);
  const auto* ref = reread.winner(cell);
  ASSERT_NE(ref, nullptr);
  const auto data = fo4::env::decode_cell(reread, *ref);
  EXPECT_EQ(data.location, location);
  EXPECT_EQ(data.image_space, image_space);
  EXPECT_EQ(data.encounter_zone, encounter_zone);
  ASSERT_EQ(data.regions.size(), 2u);
  EXPECT_EQ(data.regions[0], region);
  EXPECT_EQ(data.regions[1], second_region);
  EXPECT_EQ(data.owner, owner);
  EXPECT_EQ(data.owner_rank, 3);
}

TEST_F(CellEditTest, EditCreatesMissingSubrecordsInSchemaOrder) {
  const auto env = base_and_targets(dir_);
  const auto cell = fo4::FormKey::parse("000802:Base.esm");
  auto patch = props_patch();

  fo4::forms::CellLightingEdit lighting;
  lighting.ambient = std::array<std::uint8_t, 3>{1, 2, 3};
  fo4::forms::CellEdit edit;
  edit.lighting = lighting;
  edit.lighting_template = std::optional<fo4::FormKey>{fo4::FormKey::parse("000904:Targets.esm")};
  edit.water_height = 4.0f;
  edit.location = std::optional<fo4::FormKey>{fo4::FormKey::parse("000903:Targets.esm")};
  ASSERT_EQ(fo4::forms::edit_cell(patch, env, cell, edit).status, fo4::forms::CellEditStatus::edited);

  const auto* record = fo4::find_record(patch, cell);
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(signatures_of(*record), (std::vector<std::string>{"DATA", "XCLC", "XCLL", "LTMP", "XCLW", "XLCN"}));

  const auto reread = reread_patched(dir_, patch);
  const auto* ref = reread.winner(cell);
  ASSERT_NE(ref, nullptr);
  const auto decoded = fo4::env::decode_cell(reread, *ref).lighting;
  ASSERT_TRUE(decoded.has_value());
  EXPECT_FLOAT_EQ(decoded->directional_fade, 1.0f);
  EXPECT_FLOAT_EQ(decoded->fog_power, 1.0f);
}

TEST_F(CellEditTest, ClearingALinkRemovesItsSubrecord) {
  const auto env = base_and_targets(dir_);
  const auto cell = fo4::FormKey::parse("000802:Base.esm");
  auto patch = props_patch();

  fo4::forms::CellEdit set;
  set.image_space = std::optional<fo4::FormKey>{fo4::FormKey::parse("000901:Targets.esm")};
  set.owner = std::optional<fo4::FormKey>{fo4::FormKey::parse("000906:Targets.esm")};
  set.owner_rank = 3;
  set.regions = std::vector<fo4::FormKey>{fo4::FormKey::parse("000905:Targets.esm")};
  ASSERT_EQ(fo4::forms::edit_cell(patch, env, cell, set).status, fo4::forms::CellEditStatus::edited);

  fo4::forms::CellEdit clear;
  clear.image_space = std::optional<fo4::FormKey>{};
  clear.owner = std::optional<fo4::FormKey>{};
  clear.regions = std::vector<fo4::FormKey>{};
  ASSERT_EQ(fo4::forms::edit_cell(patch, env, cell, clear).status, fo4::forms::CellEditStatus::edited);

  const auto* record = fo4::find_record(patch, cell);
  ASSERT_NE(record, nullptr);
  EXPECT_FALSE(has_subrecord(*record, "XCIM"));
  EXPECT_FALSE(has_subrecord(*record, "XOWN"));
  EXPECT_FALSE(has_subrecord(*record, "XRNK"));
  EXPECT_FALSE(has_subrecord(*record, "XCLR"));
  const auto reread = reread_patched(dir_, patch);
  const auto* ref = reread.winner(cell);
  ASSERT_NE(ref, nullptr);
  EXPECT_FALSE(fo4::env::decode_cell(reread, *ref).image_space.has_value());
}

TEST_F(CellEditTest, EditOfMasterCellKeepsItsChildrenAndSetsFlags) {
  const auto env = base_and_targets(dir_);
  const auto cell = fo4::FormKey::parse("000802:Base.esm");
  const auto children_before = env.cell_children(cell).size();
  EXPECT_GT(children_before, 0u);
  auto patch = props_patch();

  fo4::forms::CellEdit edit;
  edit.flags = std::uint16_t{0x0082};
  ASSERT_EQ(fo4::forms::edit_cell(patch, env, cell, edit).status, fo4::forms::CellEditStatus::edited);

  const auto reread = reread_patched(dir_, patch);
  EXPECT_EQ(reread.cell_children(cell).size(), children_before);
  const auto* ref = reread.winner(cell);
  ASSERT_NE(ref, nullptr);
  EXPECT_EQ(fo4::env::decode_cell(reread, *ref).flags, std::optional<std::uint16_t>{0x0082});
}

TEST_F(CellEditTest, RefusedCellEditLeavesPatchUnchanged) {
  const auto env = base_and_targets(dir_);
  const auto cell = fo4::FormKey::parse("000802:Base.esm");
  auto patch = props_patch();
  const auto before = fo4::write_plugin(patch);

  fo4::forms::CellEdit wrong_type;
  wrong_type.water = std::optional<fo4::FormKey>{fo4::FormKey::parse("000903:Targets.esm")};
  EXPECT_EQ(fo4::forms::edit_cell(patch, env, cell, wrong_type).status,
            fo4::forms::CellEditStatus::target_type_not_allowed);

  fo4::forms::CellEdit interior;
  interior.flags = std::uint16_t{0x0003};
  EXPECT_EQ(fo4::forms::edit_cell(patch, env, cell, interior).status,
            fo4::forms::CellEditStatus::interior_flag_locked);

  fo4::forms::CellEdit not_finite;
  not_finite.water_height = std::numeric_limits<float>::quiet_NaN();
  EXPECT_EQ(fo4::forms::edit_cell(patch, env, cell, not_finite).status,
            fo4::forms::CellEditStatus::invalid_value);

  fo4::forms::CellEdit missing;
  missing.flags = std::uint16_t{0x0002};
  EXPECT_EQ(fo4::forms::edit_cell(patch, env, fo4::FormKey::parse("000999:Base.esm"), missing).status,
            fo4::forms::CellEditStatus::cell_missing);
  EXPECT_TRUE(fo4::write_plugin(patch) == before);
}

TEST_F(CellEditTest, CellPropertyDescriptorsCoverEveryEditableMember) {
  const std::vector<std::string> members = {"flags", "lighting", "lighting_template", "water_height", "water", "image_space",
                                            "encounter_zone", "location", "acoustic_space", "music_type", "sky_weather_region",
                                            "god_rays", "lock_list", "owner", "owner_rank", "regions"};
  const auto normalized = [](std::string text) {
    std::erase(text, '_');
    for (auto& ch : text) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return text;
  };
  const auto descriptors = fo4::forms::cell_property_descriptors();
  for (const auto& member : members) {
    const bool found = std::any_of(descriptors.begin(), descriptors.end(), [&](const auto& descriptor) {
      return normalized(descriptor.name).find(normalized(member)) != std::string::npos;
    });
    EXPECT_TRUE(found) << member;
  }
  for (const auto& descriptor : descriptors) {
    if (descriptor.kind == fo4::forms::CellPropertyKind::link) {
      EXPECT_FALSE(descriptor.targets.empty());
    }
  }
}

TEST_F(CellEditTest, InteriorCellEditKeepsLightingTailAndWritesFlags) {
  const auto env = base_and_targets(dir_);
  const auto cell = fo4::FormKey::parse("000804:Base.esm");
  auto patch = props_patch();
  fo4::forms::CellLightingEdit lighting;
  lighting.ambient = std::array<std::uint8_t, 3>{9, 8, 7};
  fo4::forms::CellEdit edit;
  edit.lighting = lighting;
  edit.flags = std::uint16_t{0x0003};
  ASSERT_EQ(fo4::forms::edit_cell(patch, env, cell, edit).status, fo4::forms::CellEditStatus::edited);
  const auto* record = fo4::find_record(patch, cell);
  ASSERT_NE(record, nullptr);
  const auto* xcll = subrecord_of(*record, "XCLL");
  const auto* data = subrecord_of(*record, "DATA");
  ASSERT_TRUE(xcll != nullptr && data != nullptr);
  ASSERT_EQ(xcll->data.size(), 72u);
  EXPECT_EQ(xcll->data[0], std::byte{9});
  EXPECT_TRUE(std::all_of(xcll->data.begin() + 40, xcll->data.end(),
                          [](std::byte value) { return value == std::byte{0xAB}; }));
  EXPECT_EQ(data->data, (std::vector<std::byte>{std::byte{0x03}, std::byte{0}}));
}

TEST_F(CellEditTest, ShortLightingSubrecordIsRefusedAndPatchUnchanged) {
  const auto env = base_and_targets(dir_);
  auto patch = props_patch();
  const auto before = fo4::write_plugin(patch);
  fo4::forms::CellLightingEdit lighting;
  lighting.ambient = std::array<std::uint8_t, 3>{1, 2, 3};
  fo4::forms::CellEdit edit;
  edit.lighting = lighting;
  EXPECT_EQ(fo4::forms::edit_cell(patch, env, fo4::FormKey::parse("000805:Base.esm"), edit).status,
            fo4::forms::CellEditStatus::malformed);
  EXPECT_TRUE(fo4::write_plugin(patch) == before);
}

TEST_F(CellEditTest, UnlistedOwnerAndPartialApplyLeavePatchUnchanged) {
  const auto env = base_and_targets(dir_);
  const auto cell = fo4::FormKey::parse("000802:Base.esm");
  auto patch = props_patch();
  const auto before = fo4::write_plugin(patch);
  fo4::forms::CellEdit edit;
  edit.flags = std::uint16_t{0x0082};
  edit.water_height = 3.0f;
  edit.location = std::optional<fo4::FormKey>{fo4::FormKey::parse("000903:Unrelated.esm")};
  EXPECT_EQ(fo4::forms::edit_cell(patch, env, cell, edit).status, fo4::forms::CellEditStatus::target_not_master);
  EXPECT_TRUE(fo4::write_plugin(patch) == before);
  edit.location = std::optional<fo4::FormKey>{fo4::FormKey::parse("000905:Targets.esm")};
  EXPECT_EQ(fo4::forms::edit_cell(patch, env, cell, edit).status, fo4::forms::CellEditStatus::target_type_not_allowed);
  EXPECT_TRUE(fo4::write_plugin(patch) == before);
}
