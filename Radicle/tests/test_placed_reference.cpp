#include "fo4/forms/placed_reference.hpp"

#include "fo4/plugin/reader.hpp"
#include "fo4/signature.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

namespace {

std::vector<std::byte> u32_bytes(std::uint32_t value) {
  return {
      std::byte{static_cast<std::uint8_t>(value)},
      std::byte{static_cast<std::uint8_t>(value >> 8)},
      std::byte{static_cast<std::uint8_t>(value >> 16)},
      std::byte{static_cast<std::uint8_t>(value >> 24)},
  };
}

std::vector<std::byte> f32_bytes(float value) {
  return u32_bytes(std::bit_cast<std::uint32_t>(value));
}

void append_f32(std::vector<std::byte>& out, float value) {
  const auto bytes = f32_bytes(value);
  out.insert(out.end(), bytes.begin(), bytes.end());
}

std::vector<std::byte> transform_bytes(float base) {
  std::vector<std::byte> out;
  for (int i = 0; i < 6; ++i) append_f32(out, base + static_cast<float>(i));
  return out;
}

std::vector<std::byte> zstring(std::string_view value) {
  std::vector<std::byte> out;
  out.reserve(value.size() + 1);
  for (const unsigned char ch : value) out.push_back(static_cast<std::byte>(ch));
  out.push_back(std::byte{0});
  return out;
}

std::array<std::byte, 4> group_label(std::uint32_t value) {
  const auto bytes = u32_bytes(value);
  return {bytes[0], bytes[1], bytes[2], bytes[3]};
}

fo4::Group group(fo4::GroupType type, std::uint32_t label = 0) {
  fo4::Group out;
  out.header.group_type = static_cast<std::int32_t>(type);
  out.header.label = group_label(label);
  return out;
}

fo4::Record placed_record(fo4::Signature signature,
                          const fo4::FormKey& key,
                          const fo4::FormKey& base,
                          const fo4::Plugin& plugin,
                          float transformBase,
                          float scale,
                          bool deleted = false) {
  fo4::Record record;
  record.header.signature = signature;
  record.form_key = key;
  record.header.form_id = fo4::encode_plugin_form_id(key, fo4::plugin_form_id_context(plugin));
  if (deleted) record.header.flags = fo4::RecordFlags{fo4::RecordFlags::deleted};

  record.subrecords.push_back({
      fo4::sig_name,
      u32_bytes(fo4::encode_plugin_form_id(base, fo4::plugin_form_id_context(plugin)).value()),
      false});
  record.subrecords.push_back({fo4::sig_edid, zstring("Placed"), false});
  record.subrecords.push_back({fo4::sig_data, transform_bytes(transformBase), false});
  record.subrecords.push_back({
      fo4::Signature::from_string("XSCL"),
      f32_bytes(scale),
      false});
  return record;
}

fo4::Plugin fixture() {
  fo4::Plugin plugin;
  plugin.key = fo4::PluginKey::from_filename("Patch.esp");
  plugin.header.next_form_id = 0x900;

  const fo4::FormKey cell{plugin.key, fo4::LocalFormId{0x800}};
  const fo4::FormKey base{plugin.key, fo4::LocalFormId{0x801}};
  const fo4::FormKey refr{plugin.key, fo4::LocalFormId{0x802}};
  const fo4::FormKey achr{plugin.key, fo4::LocalFormId{0x803}};

  fo4::Group top = group(fo4::GroupType::top);
  const auto label = fo4::encode_plugin_form_id(cell, fo4::plugin_form_id_context(plugin)).value();
  fo4::Group children = group(fo4::GroupType::cell_children, label);
  fo4::Group persistent = group(fo4::GroupType::cell_persistent_children, label);
  fo4::Group temporary = group(fo4::GroupType::cell_temporary_children, label);
  persistent.children.emplace_back(placed_record(
      fo4::Signature::from_string("REFR"), refr, base, plugin, 1.0f, 1.25f));
  temporary.children.emplace_back(placed_record(
      fo4::Signature::from_string("ACHR"), achr, base, plugin, 10.0f, 0.75f, true));
  children.children.emplace_back(std::move(persistent));
  children.children.emplace_back(std::move(temporary));
  top.children.emplace_back(std::move(children));
  plugin.groups.push_back(std::move(top));
  return plugin;
}

}  // namespace

TEST(PlacedReference, EnumeratesPersistentAndTemporaryCellChildren) {
  auto plugin = fixture();
  const fo4::FormKey cell{plugin.key, fo4::LocalFormId{0x800}};

  const auto refs = fo4::forms::placed_references_for_cell(plugin, cell);
  ASSERT_EQ(refs.size(), 2u);

  EXPECT_EQ(refs[0].kind, fo4::forms::PlacedReferenceKind::object);
  EXPECT_TRUE(refs[0].persistent);
  EXPECT_FALSE(refs[0].deleted);
  ASSERT_TRUE(refs[0].base.has_value());
  EXPECT_EQ(refs[0].base->to_string(), "000801:Patch.esp");
  EXPECT_EQ(refs[0].editor_id, "Placed");
  EXPECT_FLOAT_EQ(refs[0].transform.x, 1.0f);
  EXPECT_FLOAT_EQ(refs[0].transform.rz, 6.0f);
  EXPECT_FLOAT_EQ(refs[0].scale, 1.25f);

  EXPECT_EQ(refs[1].kind, fo4::forms::PlacedReferenceKind::npc);
  EXPECT_FALSE(refs[1].persistent);
  EXPECT_TRUE(refs[1].deleted);
  EXPECT_FLOAT_EQ(refs[1].transform.x, 10.0f);
  EXPECT_FLOAT_EQ(refs[1].transform.rz, 15.0f);
  EXPECT_FLOAT_EQ(refs[1].scale, 0.75f);
}

TEST(PlacedReference, TransformMutationChangesOnlyDataAndInvalidatesOriginalBytes) {
  auto plugin = fixture();
  const fo4::FormKey refr{plugin.key, fo4::LocalFormId{0x802}};

  auto* before = const_cast<fo4::Record*>(fo4::find_record(plugin, refr));
  ASSERT_NE(before, nullptr);
  const auto originalName = before->subrecords[0].data;
  const auto originalScale = before->subrecords[3].data;
  before->original_range = {nullptr, 4, 12};
  plugin.original_range = {nullptr, 8, 20};

  ASSERT_TRUE(fo4::forms::set_placed_reference_transform(
      plugin,
      refr,
      {.x = 100.0f, .y = 200.0f, .z = 300.0f,
       .rx = 1.5f, .ry = 2.5f, .rz = 3.5f}));

  const auto* after = fo4::find_record(plugin, refr);
  ASSERT_NE(after, nullptr);
  const auto decoded = fo4::forms::decode_placed_reference(plugin, *after);
  ASSERT_TRUE(decoded.has_value());
  EXPECT_FLOAT_EQ(decoded->transform.x, 100.0f);
  EXPECT_FLOAT_EQ(decoded->transform.y, 200.0f);
  EXPECT_FLOAT_EQ(decoded->transform.z, 300.0f);
  EXPECT_FLOAT_EQ(decoded->transform.rx, 1.5f);
  EXPECT_FLOAT_EQ(decoded->transform.ry, 2.5f);
  EXPECT_FLOAT_EQ(decoded->transform.rz, 3.5f);
  EXPECT_FLOAT_EQ(decoded->scale, 1.25f);
  EXPECT_EQ(after->subrecords[0].data, originalName);
  EXPECT_EQ(after->subrecords[3].data, originalScale);
  EXPECT_TRUE(after->original_range.empty());
  EXPECT_TRUE(plugin.original_range.empty());
}

TEST(PlacedReference, RejectsMalformedDataTransactionally) {
  auto plugin = fixture();
  const fo4::FormKey refr{plugin.key, fo4::LocalFormId{0x802}};
  auto* record = const_cast<fo4::Record*>(fo4::find_record(plugin, refr));
  ASSERT_NE(record, nullptr);
  record->subrecords[2].data.resize(20);
  const auto before = record->subrecords[2].data;

  EXPECT_THROW(
      fo4::forms::set_placed_reference_transform(
          plugin, refr, {.x = 9.0f}),
      fo4::forms::PlacedReferenceError);

  const auto* after = fo4::find_record(plugin, refr);
  ASSERT_NE(after, nullptr);
  EXPECT_EQ(after->subrecords[2].data, before);
}
