#include "fo4/forms/placed_reference.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/signature.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace {

void write_header(fo4::test::ByteWriter& out,
                  std::initializer_list<std::string_view> masters) {
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  for (const auto master : masters) {
    std::vector<std::byte> name;
    name.reserve(master.size() + 1);
    for (const char ch : master) {
      name.push_back(std::byte{static_cast<std::uint8_t>(ch)});
    }
    name.push_back(std::byte{0});
    fo4::test::write_subrecord(out, "MAST", name);
    fo4::test::ByteWriter data;
    data.u32(0);
    data.u32(0);
    fo4::test::write_subrecord(out, "DATA", data.bytes);
  }
  fo4::test::end_record(out, tes4);
}

std::size_t begin_group_raw(fo4::test::ByteWriter& out,
                            std::uint32_t label,
                            fo4::GroupType type) {
  const auto offset = out.size();
  out.sig("GRUP");
  out.u32(0);
  out.u32(label);
  out.i32(static_cast<std::int32_t>(type));
  out.u16(0);
  out.u16(0);
  out.u16(0);
  out.u16(0);
  return offset;
}

void end_group_raw(fo4::test::ByteWriter& out, std::size_t offset) {
  out.patch_u32(offset + 4, static_cast<std::uint32_t>(out.size() - offset));
}

void write_transform(fo4::test::ByteWriter& data, float base) {
  for (int index = 0; index < 6; ++index) {
    data.f32(base + static_cast<float>(index));
  }
}

void write_cell(fo4::test::ByteWriter& out, std::uint32_t raw) {
  const auto cell = fo4::test::begin_record(out, "CELL", 0, raw);
  const std::vector<std::byte> data{std::byte{1}, std::byte{0}};
  fo4::test::write_subrecord(out, "DATA", data);
  fo4::test::end_record(out, cell);
}

void write_reference(fo4::test::ByteWriter& out,
                     std::uint32_t raw,
                     std::uint32_t base_raw,
                     float transform_base) {
  const auto refr = fo4::test::begin_record(out, "REFR", 0, raw);
  fo4::test::ByteWriter name;
  name.u32(base_raw);
  fo4::test::write_subrecord(out, "NAME", name.bytes);
  fo4::test::ByteWriter data;
  write_transform(data, transform_base);
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::ByteWriter scale;
  scale.f32(1.25f);
  fo4::test::write_subrecord(out, "XSCL", scale.bytes);
  fo4::test::end_record(out, refr);
}

std::vector<std::byte> interior_source() {
  fo4::test::ByteWriter out;
  write_header(out, {"Base.esm"});

  const auto top = fo4::test::begin_group(out, "CELL", 0);
  const auto block = begin_group_raw(out, 0, fo4::GroupType::interior_cell_block);
  const auto subblock = begin_group_raw(out, 0, fo4::GroupType::interior_cell_subblock);
  write_cell(out, 0x00000800u);
  const auto children = begin_group_raw(out, 0x00000800u, fo4::GroupType::cell_children);
  const auto temporary = begin_group_raw(out, 0x00000800u, fo4::GroupType::cell_temporary_children);
  write_reference(out, 0x00000801u, 0x00000802u, 10.0f);
  end_group_raw(out, temporary);
  end_group_raw(out, children);
  end_group_raw(out, subblock);
  end_group_raw(out, block);
  fo4::test::end_group(out, top);
  return out.bytes;
}

std::vector<std::byte> exterior_source() {
  fo4::test::ByteWriter out;
  write_header(out, {"Base.esm"});

  const auto top = fo4::test::begin_group(out, "WRLD", 0);
  const auto wrld = fo4::test::begin_record(out, "WRLD", 0, 0x00000900u);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"BaseWorld\0", 10});
  fo4::test::end_record(out, wrld);
  const auto world_children = begin_group_raw(out, 0x00000900u, fo4::GroupType::world_children);
  const auto block = begin_group_raw(out, 0, fo4::GroupType::exterior_cell_block);
  const auto subblock = begin_group_raw(out, 0, fo4::GroupType::exterior_cell_subblock);
  write_cell(out, 0x00000800u);
  const auto children = begin_group_raw(out, 0x00000800u, fo4::GroupType::cell_children);
  const auto persistent = begin_group_raw(out, 0x00000800u, fo4::GroupType::cell_persistent_children);
  write_reference(out, 0x00000801u, 0x00000802u, 20.0f);
  end_group_raw(out, persistent);
  end_group_raw(out, children);
  end_group_raw(out, subblock);
  end_group_raw(out, block);
  end_group_raw(out, world_children);
  fo4::test::end_group(out, top);
  return out.bytes;
}

std::vector<std::byte> target_with_other_master() {
  fo4::test::ByteWriter out;
  write_header(out, {"Other.esm"});
  return out.bytes;
}

std::vector<std::byte> target_with_interior_cell() {
  fo4::test::ByteWriter out;
  write_header(out, {"Base.esm"});
  const auto top = fo4::test::begin_group(out, "CELL", 0);
  const auto block = begin_group_raw(out, 0, fo4::GroupType::interior_cell_block);
  const auto subblock = begin_group_raw(out, 0, fo4::GroupType::interior_cell_subblock);
  write_cell(out, 0x00000800u);
  end_group_raw(out, subblock);
  end_group_raw(out, block);
  fo4::test::end_group(out, top);
  return out.bytes;
}

void write_actor(fo4::test::ByteWriter& out, std::uint32_t raw, std::uint32_t base_raw,
                 float transform_base) {
  const auto achr = fo4::test::begin_record(out, "ACHR", 0, raw);
  fo4::test::ByteWriter name;
  name.u32(base_raw);
  fo4::test::write_subrecord(out, "NAME", name.bytes);
  fo4::test::ByteWriter data;
  write_transform(data, transform_base);
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::ByteWriter scale;
  scale.f32(1.25f);
  fo4::test::write_subrecord(out, "XSCL", scale.bytes);
  fo4::test::end_record(out, achr);
}

std::vector<std::byte> exterior_actor_source() {
  fo4::test::ByteWriter out;
  write_header(out, {"Base.esm"});
  const auto top = fo4::test::begin_group(out, "WRLD", 0);
  const auto wrld = fo4::test::begin_record(out, "WRLD", 0, 0x00000900u);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"BaseWorld\0", 10});
  fo4::test::end_record(out, wrld);
  const auto world_children = begin_group_raw(out, 0x00000900u, fo4::GroupType::world_children);
  const auto block = begin_group_raw(out, 0, fo4::GroupType::exterior_cell_block);
  const auto subblock = begin_group_raw(out, 0, fo4::GroupType::exterior_cell_subblock);
  write_cell(out, 0x00000800u);
  const auto children = begin_group_raw(out, 0x00000800u, fo4::GroupType::cell_children);
  const auto persistent = begin_group_raw(out, 0x00000800u, fo4::GroupType::cell_persistent_children);
  write_actor(out, 0x00000801u, 0x00000802u, 20.0f);
  end_group_raw(out, persistent);
  end_group_raw(out, children);
  end_group_raw(out, subblock);
  end_group_raw(out, block);
  end_group_raw(out, world_children);
  fo4::test::end_group(out, top);
  return out.bytes;
}

std::vector<std::byte> target_with_exterior_cell() {
  fo4::test::ByteWriter out;
  write_header(out, {"Base.esm"});
  const auto top = fo4::test::begin_group(out, "WRLD", 0);
  const auto wrld = fo4::test::begin_record(out, "WRLD", 0, 0x00000900u);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"BaseWorld\0", 10});
  fo4::test::end_record(out, wrld);
  const auto world_children = begin_group_raw(out, 0x00000900u, fo4::GroupType::world_children);
  const auto block = begin_group_raw(out, 0, fo4::GroupType::exterior_cell_block);
  const auto subblock = begin_group_raw(out, 0, fo4::GroupType::exterior_cell_subblock);
  write_cell(out, 0x00000800u);
  end_group_raw(out, subblock);
  end_group_raw(out, block);
  end_group_raw(out, world_children);
  fo4::test::end_group(out, top);
  return out.bytes;
}

std::size_t count_records(const fo4::Plugin& plugin, const fo4::FormKey& key) {
  std::size_t count = 0;
  for (const auto* record : fo4::collect_records(plugin)) {
    if (record->form_key == key) ++count;
  }
  return count;
}

const fo4::Group* find_group(const fo4::Plugin& plugin,
                             fo4::GroupType type,
                             std::uint32_t label) {
  const fo4::Group* found = nullptr;
  const auto visit = [&](const auto& self, const fo4::Group& group) -> void {
    if (group.header.group_type == static_cast<std::int32_t>(type) &&
        group.header.label_as_u32() == label) {
      EXPECT_EQ(found, nullptr);
      found = &group;
    }
    for (const auto& child : group.children) {
      if (const auto* nested = std::get_if<fo4::Group>(&child); nested != nullptr) {
        self(self, *nested);
      }
    }
  };
  for (const auto& group : plugin.groups) visit(visit, group);
  return found;
}

void expect_base_master_second(const fo4::Plugin& plugin) {
  ASSERT_EQ(plugin.header.masters.size(), 2u);
  EXPECT_EQ(plugin.header.masters[0].key.filename(), "Other.esm");
  EXPECT_EQ(plugin.header.masters[1].key.filename(), "Base.esm");
}

void write_info_record(fo4::test::ByteWriter& out,
                       std::uint32_t raw,
                       bool external_pnam,
                       bool explicit_null_pnam) {
  const auto info = fo4::test::begin_record(out, "INFO", 0, raw);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"DialogueInfo\0", 13});
  if (external_pnam || explicit_null_pnam) {
    fo4::test::ByteWriter pnam;
    pnam.u32(external_pnam ? 0x00000803u : 0u);
    fo4::test::write_subrecord(out, "PNAM", pnam.bytes);
  }
  fo4::test::ByteWriter sound;
  sound.u32(0x01000810u);
  fo4::test::write_subrecord(out, "SNAM", sound.bytes);
  fo4::test::ByteWriter trda;
  trda.u32(0xFFFFFFFFu);
  trda.u8(1);
  trda.u32(0x01000810u);
  trda.u32(0);
  trda.u32(0);
  trda.u8(0);
  trda.u8(0);
  trda.u8(0);
  fo4::test::write_subrecord(out, "TRDA", trda.bytes);
  fo4::test::end_record(out, info);
}

std::vector<std::byte> nested_info_source(bool external_pnam = false,
                                           bool explicit_null_pnam = false,
                                           bool duplicate_topic = false,
                                           bool null_topic_label = false,
                                           bool wrong_dial_signature = false,
                                           bool with_external_predecessor = false) {
  fo4::test::ByteWriter out;
  write_header(out, {"Base.esm", "Sound.esm"});

  const auto quest_top = fo4::test::begin_group(out, "QUST", 0);
  const auto quest = fo4::test::begin_record(out, "QUST", 0, 0x00000800u);
  fo4::test::end_record(out, quest);
  const auto quest_children = begin_group_raw(
      out, 0x00000800u, fo4::GroupType::cell_visible_distant_children);
  const auto dial_top = fo4::test::begin_group(out, "DIAL", 0);
  const auto dial = fo4::test::begin_record(
      out, wrong_dial_signature ? "SCEN" : "DIAL", 0, 0x00000801u);
  fo4::test::ByteWriter qnam;
  qnam.u32(0x00000800u);
  fo4::test::write_subrecord(out, "QNAM", qnam.bytes);
  fo4::test::end_record(out, dial);
  const auto topic = begin_group_raw(
      out, null_topic_label ? 0u : 0x00000801u, fo4::GroupType::topic_children);
  if (with_external_predecessor) {
    const auto previous = fo4::test::begin_record(out, "INFO", 0, 0x00000803u);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"MasterInfo\0", 11});
    fo4::test::end_record(out, previous);
  }
  write_info_record(out, 0x02000802u, external_pnam, explicit_null_pnam);
  end_group_raw(out, topic);
  if (duplicate_topic) {
    const auto duplicate = begin_group_raw(
        out, 0x00000801u, fo4::GroupType::topic_children);
    end_group_raw(out, duplicate);
  }
  fo4::test::end_group(out, dial_top);
  end_group_raw(out, quest_children);
  fo4::test::end_group(out, quest_top);
  return out.bytes;
}

std::vector<std::byte> nested_info_target(bool with_topic,
                                           bool with_existing_info,
                                           bool with_quest = true,
                                           bool with_dial = true,
                                           bool with_external_predecessor = false,
                                           bool external_predecessor_wrong_topic = false) {
  fo4::test::ByteWriter out;
  write_header(out, {"Other.esm", "Base.esm"});
  if (!with_quest) return out.bytes;

  const auto quest_top = fo4::test::begin_group(out, "QUST", 0);
  const auto quest = fo4::test::begin_record(out, "QUST", 0, 0x01000800u);
  fo4::test::end_record(out, quest);
  const auto quest_children = begin_group_raw(
      out, 0x01000800u, fo4::GroupType::cell_visible_distant_children);
  if (!with_dial) {
    end_group_raw(out, quest_children);
    fo4::test::end_group(out, quest_top);
    return out.bytes;
  }
  const auto dial_top = fo4::test::begin_group(out, "DIAL", 0);
  const auto dial = fo4::test::begin_record(out, "DIAL", 0, 0x01000801u);
  fo4::test::ByteWriter qnam;
  qnam.u32(0x01000800u);
  fo4::test::write_subrecord(out, "QNAM", qnam.bytes);
  fo4::test::end_record(out, dial);
  if (with_topic) {
    const auto topic = begin_group_raw(
        out, 0x01000801u, fo4::GroupType::topic_children);
    if (with_external_predecessor) {
      const auto previous = fo4::test::begin_record(out, "INFO", 0, 0x01000803u);
      fo4::test::write_subrecord(out, "EDID", std::string_view{"MasterInfo\0", 11});
      fo4::test::end_record(out, previous);
    }
    if (with_existing_info) {
      const auto info = fo4::test::begin_record(out, "INFO", 0, 0x02000820u);
      fo4::test::write_subrecord(out, "EDID", std::string_view{"ExistingInfo\0", 13});
      fo4::test::ByteWriter scalar;
      scalar.u32(0x02000842u);
      fo4::test::write_subrecord(out, "NAM0", scalar.bytes);
      fo4::test::end_record(out, info);
    }
    end_group_raw(out, topic);
  }
  fo4::test::end_group(out, dial_top);
  end_group_raw(out, quest_children);
  fo4::test::end_group(out, quest_top);
  if (external_predecessor_wrong_topic) {
    const auto wrong_dial_top = fo4::test::begin_group(out, "DIAL", 0);
    const auto wrong_dial = fo4::test::begin_record(out, "DIAL", 0, 0x01000806u);
    fo4::test::end_record(out, wrong_dial);
    const auto wrong_topic = begin_group_raw(
        out, 0x01000806u, fo4::GroupType::topic_children);
    const auto previous = fo4::test::begin_record(out, "INFO", 0, 0x01000803u);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"MasterInfo\0", 11});
    fo4::test::end_record(out, previous);
    end_group_raw(out, wrong_topic);
    fo4::test::end_group(out, wrong_dial_top);
  }
  return out.bytes;
}

std::vector<std::byte> same_plugin_info_source(bool with_topic = true,
                                                bool duplicate_info = false) {
  fo4::test::ByteWriter out;
  write_header(out, {});
  const auto quest_top = fo4::test::begin_group(out, "QUST", 0);
  const auto quest = fo4::test::begin_record(out, "QUST", 0, 0x00000800u);
  fo4::test::end_record(out, quest);
  const auto quest_children = begin_group_raw(
      out, 0x00000800u, fo4::GroupType::cell_visible_distant_children);
  const auto dial_top = fo4::test::begin_group(out, "DIAL", 0);
  const auto dial = fo4::test::begin_record(out, "DIAL", 0, 0x00000801u);
  fo4::test::end_record(out, dial);
  if (with_topic) {
    const auto topic = begin_group_raw(
        out, 0x00000801u, fo4::GroupType::topic_children);
    const auto first = fo4::test::begin_record(out, "INFO", 0, 0x00000802u);
    fo4::test::end_record(out, first);
    const auto second = fo4::test::begin_record(out, "INFO", 0, 0x00000803u);
    fo4::test::ByteWriter pnam;
    pnam.u32(0x00000802u);
    fo4::test::write_subrecord(out, "PNAM", pnam.bytes);
    fo4::test::end_record(out, second);
    if (duplicate_info) {
      const auto duplicate = fo4::test::begin_record(out, "INFO", 0, 0x00000803u);
      fo4::test::end_record(out, duplicate);
    }
    end_group_raw(out, topic);
  }
  fo4::test::end_group(out, dial_top);
  end_group_raw(out, quest_children);
  fo4::test::end_group(out, quest_top);
  return out.bytes;
}

std::vector<fo4::FormKey> direct_info_keys(const fo4::Plugin& plugin,
                                           std::uint32_t label) {
  const auto* topic = find_group(plugin, fo4::GroupType::topic_children, label);
  EXPECT_NE(topic, nullptr);
  std::vector<fo4::FormKey> keys;
  if (topic == nullptr) return keys;
  for (const auto& child : topic->children) {
    const auto* record = std::get_if<fo4::Record>(&child);
    if (record != nullptr && record->header.signature == fo4::Signature::from_string("INFO")) {
      keys.push_back(record->form_key);
    }
  }
  return keys;
}

const fo4::Subrecord* find_subrecord(const fo4::Record& record,
                                     std::string_view signature) {
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature.to_string() == signature) return &subrecord;
  }
  return nullptr;
}

std::uint32_t read_u32(const fo4::Subrecord& subrecord, std::size_t offset) {
  std::uint32_t value = 0;
  std::memcpy(&value, subrecord.data.data() + offset, sizeof(value));
  return value;
}

}  // namespace

TEST(PlacedReferenceCopy, CopiesInteriorCellHierarchyAndRemapsCellLabels) {
  const auto source = fo4::read_plugin(interior_source(), "Source.esp");
  auto target = fo4::read_plugin(target_with_other_master(), "Patch.esp");
  const auto cell = fo4::FormKey::parse("000800:Base.esm");
  const auto reference = fo4::FormKey::parse("000801:Base.esm");

  EXPECT_EQ(
      fo4::forms::copy_placed_reference_as_override(target, source, reference),
      fo4::forms::PlacedReferenceCopyStatus::inserted);
  expect_base_master_second(target);
  EXPECT_NE(fo4::find_record(target, cell), nullptr);
  EXPECT_NE(fo4::find_record(target, reference), nullptr);
  EXPECT_NE(find_group(target, fo4::GroupType::cell_children, 0x01000800u), nullptr);
  EXPECT_NE(find_group(target, fo4::GroupType::cell_temporary_children, 0x01000800u), nullptr);

  const auto refs = fo4::forms::placed_references_for_cell(target, cell);
  ASSERT_EQ(refs.size(), 1u);
  ASSERT_TRUE(refs[0].base.has_value());
  EXPECT_EQ(refs[0].base->to_string(), "000802:Base.esm");
  EXPECT_FLOAT_EQ(refs[0].transform.x, 10.0f);
  EXPECT_FLOAT_EQ(refs[0].scale, 1.25f);

  auto reread = fo4::read_plugin(fo4::write_plugin(target), "Patch.esp");
  expect_base_master_second(reread);
  const auto reread_refs = fo4::forms::placed_references_for_cell(reread, cell);
  ASSERT_EQ(reread_refs.size(), 1u);
  EXPECT_EQ(reread_refs[0].record->form_key, reference);
  ASSERT_TRUE(reread_refs[0].base.has_value());
  EXPECT_EQ(reread_refs[0].base->to_string(), "000802:Base.esm");
}

TEST(PlacedReferenceCopy, CopiesExteriorWorldspaceAnchorAndRemapsWorldLabel) {
  const auto source = fo4::read_plugin(exterior_source(), "Source.esp");
  auto target = fo4::read_plugin(target_with_other_master(), "Patch.esp");
  const auto world = fo4::FormKey::parse("000900:Base.esm");
  const auto cell = fo4::FormKey::parse("000800:Base.esm");
  const auto reference = fo4::FormKey::parse("000801:Base.esm");

  EXPECT_EQ(
      fo4::forms::copy_placed_reference_as_override(target, source, reference),
      fo4::forms::PlacedReferenceCopyStatus::inserted);
  expect_base_master_second(target);
  EXPECT_NE(fo4::find_record(target, world), nullptr);
  EXPECT_NE(fo4::find_record(target, cell), nullptr);
  EXPECT_NE(fo4::find_record(target, reference), nullptr);
  EXPECT_NE(find_group(target, fo4::GroupType::world_children, 0x01000900u), nullptr);
  EXPECT_NE(find_group(target, fo4::GroupType::cell_children, 0x01000800u), nullptr);
  EXPECT_NE(find_group(target, fo4::GroupType::cell_persistent_children, 0x01000800u), nullptr);

  auto reread = fo4::read_plugin(fo4::write_plugin(target), "Patch.esp");
  expect_base_master_second(reread);
  EXPECT_NE(fo4::find_record(reread, world), nullptr);
  const auto refs = fo4::forms::placed_references_for_cell(reread, cell);
  ASSERT_EQ(refs.size(), 1u);
  EXPECT_TRUE(refs[0].persistent);
  EXPECT_EQ(refs[0].record->form_key, reference);
}

TEST(PlacedReferenceCopy, ExistingOverrideIsNoOpUnlessOverwriteIsRequested) {
  const auto source = fo4::read_plugin(interior_source(), "Source.esp");
  auto target = fo4::read_plugin(target_with_other_master(), "Patch.esp");
  const auto reference = fo4::FormKey::parse("000801:Base.esm");

  ASSERT_EQ(
      fo4::forms::copy_placed_reference_as_override(target, source, reference),
      fo4::forms::PlacedReferenceCopyStatus::inserted);
  const auto before = fo4::write_plugin(target);
  EXPECT_EQ(
      fo4::forms::copy_placed_reference_as_override(target, source, reference),
      fo4::forms::PlacedReferenceCopyStatus::already_exists);
  EXPECT_EQ(fo4::write_plugin(target), before);

  EXPECT_EQ(
      fo4::forms::copy_placed_reference_as_override(target, source, reference, true),
      fo4::forms::PlacedReferenceCopyStatus::replaced);
  EXPECT_NE(fo4::find_record(target, reference), nullptr);
}

TEST(PlacedReferenceCopyNew, CreatesBucketAndNewRecordInExistingInteriorCell) {
  const auto source = fo4::read_plugin(interior_source(), "Source.esp");
  auto target = fo4::read_plugin(target_with_interior_cell(), "Patch.esp");
  const auto cell = fo4::FormKey::parse("000800:Base.esm");
  const auto reference = fo4::FormKey::parse("000801:Base.esm");

  const auto result = fo4::forms::copy_placed_reference_as_new(target, source, reference);
  EXPECT_EQ(result.status, fo4::forms::PlacedReferenceCopyNewStatus::created);
  EXPECT_EQ(result.new_reference.owner().filename(), "Patch.esp");
  EXPECT_NE(result.new_reference, reference);
  EXPECT_NE(fo4::find_record(target, result.new_reference), nullptr);
  EXPECT_NE(find_group(target, fo4::GroupType::cell_temporary_children, 0x00000800u), nullptr);

  const auto refs = fo4::forms::placed_references_for_cell(target, cell);
  ASSERT_EQ(refs.size(), 1u);
  EXPECT_EQ(refs[0].record->form_key, result.new_reference);
  ASSERT_TRUE(refs[0].base.has_value());
  EXPECT_EQ(refs[0].base->to_string(), "000802:Base.esm");
  EXPECT_FLOAT_EQ(refs[0].scale, 1.25f);

  auto reread = fo4::read_plugin(fo4::write_plugin(target), "Patch.esp");
  const auto reread_refs = fo4::forms::placed_references_for_cell(reread, cell);
  ASSERT_EQ(reread_refs.size(), 1u);
  EXPECT_EQ(reread_refs[0].record->form_key, result.new_reference);
}

TEST(PlacedReferenceCopyNew, FailsClosedWhenParentCellMissing) {
  const auto source = fo4::read_plugin(interior_source(), "Source.esp");
  auto target = fo4::read_plugin(target_with_other_master(), "Patch.esp");
  const auto reference = fo4::FormKey::parse("000801:Base.esm");

  const auto result = fo4::forms::copy_placed_reference_as_new(target, source, reference);
  EXPECT_EQ(result.status, fo4::forms::PlacedReferenceCopyNewStatus::parent_cell_missing);
  EXPECT_EQ(fo4::find_record(target, fo4::FormKey::parse("000800:Base.esm")), nullptr);
}

TEST(PlacedReferenceCopyNew, PreservesExistingBucketReferences) {
  const auto source = fo4::read_plugin(interior_source(), "Source.esp");
  auto target = fo4::read_plugin(target_with_other_master(), "Patch.esp");
  const auto cell = fo4::FormKey::parse("000800:Base.esm");
  const auto reference = fo4::FormKey::parse("000801:Base.esm");
  ASSERT_EQ(fo4::forms::copy_placed_reference_as_override(target, source, reference),
            fo4::forms::PlacedReferenceCopyStatus::inserted);

  const auto result = fo4::forms::copy_placed_reference_as_new(target, source, reference);
  EXPECT_EQ(result.status, fo4::forms::PlacedReferenceCopyNewStatus::created);
  EXPECT_NE(result.new_reference, reference);

  const auto refs = fo4::forms::placed_references_for_cell(target, cell);
  ASSERT_EQ(refs.size(), 2u);
  bool has_override = false;
  bool has_new = false;
  for (const auto& r : refs) {
    if (r.record->form_key == reference) has_override = true;
    if (r.record->form_key == result.new_reference) has_new = true;
  }
  EXPECT_TRUE(has_override);
  EXPECT_TRUE(has_new);
}

TEST(PlacedReferenceCopyNew, ReportsMissingSource) {
  const auto source = fo4::read_plugin(interior_source(), "Source.esp");
  auto target = fo4::read_plugin(target_with_interior_cell(), "Patch.esp");
  const auto result = fo4::forms::copy_placed_reference_as_new(
      target, source, fo4::FormKey::parse("0009AA:Base.esm"));
  EXPECT_EQ(result.status, fo4::forms::PlacedReferenceCopyNewStatus::source_missing);
}

TEST(PlacedReferenceCopyNew, RejectsUnsupportedSignature) {
  const auto source = fo4::read_plugin(interior_source(), "Source.esp");
  auto target = fo4::read_plugin(target_with_interior_cell(), "Patch.esp");
  const auto result = fo4::forms::copy_placed_reference_as_new(
      target, source, fo4::FormKey::parse("000800:Base.esm"));
  EXPECT_EQ(result.status, fo4::forms::PlacedReferenceCopyNewStatus::unsupported_signature);
}

TEST(PlacedReferenceCopyNew, CreatesExteriorPersistentActorUnderExistingCell) {
  const auto source = fo4::read_plugin(exterior_actor_source(), "Source.esp");
  auto target = fo4::read_plugin(target_with_exterior_cell(), "Patch.esp");
  const auto world = fo4::FormKey::parse("000900:Base.esm");
  const auto cell = fo4::FormKey::parse("000800:Base.esm");
  const auto reference = fo4::FormKey::parse("000801:Base.esm");

  const auto result = fo4::forms::copy_placed_reference_as_new(target, source, reference);
  EXPECT_EQ(result.status, fo4::forms::PlacedReferenceCopyNewStatus::created);
  EXPECT_EQ(result.new_reference.owner().filename(), "Patch.esp");
  EXPECT_NE(result.new_reference, reference);

  EXPECT_EQ(count_records(target, world), 1u);
  EXPECT_EQ(count_records(target, cell), 1u);
  EXPECT_EQ(count_records(target, result.new_reference), 1u);
  EXPECT_NE(find_group(target, fo4::GroupType::cell_persistent_children, 0x00000800u), nullptr);

  const auto refs = fo4::forms::placed_references_for_cell(target, cell);
  ASSERT_EQ(refs.size(), 1u);
  EXPECT_EQ(refs[0].record->form_key, result.new_reference);
  EXPECT_EQ(refs[0].record->header.signature, fo4::Signature::from_string("ACHR"));
  EXPECT_TRUE(refs[0].persistent);
  ASSERT_TRUE(refs[0].base.has_value());
  EXPECT_EQ(refs[0].base->to_string(), "000802:Base.esm");

  auto reread = fo4::read_plugin(fo4::write_plugin(target), "Patch.esp");
  EXPECT_EQ(count_records(reread, world), 1u);
  EXPECT_EQ(count_records(reread, cell), 1u);
  const auto reread_refs = fo4::forms::placed_references_for_cell(reread, cell);
  ASSERT_EQ(reread_refs.size(), 1u);
  EXPECT_EQ(reread_refs[0].record->header.signature, fo4::Signature::from_string("ACHR"));
  EXPECT_EQ(reread_refs[0].record->form_key, result.new_reference);
}

TEST(InfoCopyNew, CopiesFullQuestDialogueContainmentAndRemapsTrda) {
  const auto source = fo4::read_plugin(nested_info_source(), "Source.esp");
  auto target = fo4::read_plugin(nested_info_target(true, true), "Patch.esp");
  const auto source_bytes = fo4::test::original_bytes(source);

  const auto result = fo4::forms::copy_info_as_new(
      target, source, fo4::FormKey::parse("000802:Source.esp"));
  EXPECT_EQ(result.status, fo4::forms::InfoCopyNewStatus::created);
  EXPECT_EQ(result.new_info, fo4::FormKey::parse("000900:Patch.esp"));
  ASSERT_EQ(target.header.masters.size(), 3u);
  EXPECT_EQ(target.header.masters[0].key.filename(), "Other.esm");
  EXPECT_EQ(target.header.masters[1].key.filename(), "Base.esm");
  EXPECT_EQ(target.header.masters[2].key.filename(), "Sound.esm");

  const auto existing_key = fo4::FormKey::parse("000820:Patch.esp");
  const auto* existing = fo4::find_record(target, existing_key);
  ASSERT_NE(existing, nullptr);
  const auto* existing_scalar = find_subrecord(*existing, "NAM0");
  ASSERT_NE(existing_scalar, nullptr);
  EXPECT_EQ(read_u32(*existing_scalar, 0), 0x02000842u);
  EXPECT_EQ(existing->header.form_id.value(), 0x03000820u);

  const auto* copied = fo4::find_record(target, result.new_info);
  ASSERT_NE(copied, nullptr);
  const auto* copied_trda = find_subrecord(*copied, "TRDA");
  ASSERT_NE(copied_trda, nullptr);
  ASSERT_EQ(copied_trda->data.size(), 20u);
  EXPECT_EQ(read_u32(*copied_trda, 0), 0xFFFFFFFFu);
  EXPECT_EQ(read_u32(*copied_trda, 5), 0x02000810u);
  const auto* copied_sound = find_subrecord(*copied, "SNAM");
  ASSERT_NE(copied_sound, nullptr);
  EXPECT_EQ(read_u32(*copied_sound, 0), 0x02000810u);

  const auto keys = direct_info_keys(target, 0x01000801u);
  ASSERT_EQ(keys.size(), 2u);
  EXPECT_EQ(keys[0], existing_key);
  EXPECT_EQ(keys[1], result.new_info);
  EXPECT_NE(find_group(target, fo4::GroupType::topic_children, 0x01000801u), nullptr);
  EXPECT_EQ(fo4::test::original_bytes(source), source_bytes);

  const auto reread = fo4::read_plugin(fo4::write_plugin(target), "Patch.esp");
  const auto* reread_info = fo4::find_record(reread, result.new_info);
  ASSERT_NE(reread_info, nullptr);
  const auto* reread_trda = find_subrecord(*reread_info, "TRDA");
  ASSERT_NE(reread_trda, nullptr);
  EXPECT_EQ(read_u32(*reread_trda, 0), 0xFFFFFFFFu);
  EXPECT_EQ(read_u32(*reread_trda, 5), 0x02000810u);
}

TEST(InfoCopyNew, FailsWithoutDestinationQuestAndLeavesBytesUnchanged) {
  const auto source = fo4::read_plugin(nested_info_source(), "Source.esp");
  auto target = fo4::read_plugin(nested_info_target(false, false, false), "Patch.esp");
  const auto before = fo4::write_plugin(target);

  const auto result = fo4::forms::copy_info_as_new(
      target, source, fo4::FormKey::parse("000802:Source.esp"));
  EXPECT_EQ(result.status, fo4::forms::InfoCopyNewStatus::parent_dial_missing);
  EXPECT_EQ(fo4::write_plugin(target), before);
}

TEST(InfoCopyNew, ExternalPnamUsesMasterAndLocalTopicHead) {
  const auto source = fo4::read_plugin(nested_info_source(true), "Source.esp");
  auto target = fo4::read_plugin(nested_info_target(true, true), "Patch.esp");
  const auto result = fo4::forms::copy_info_as_new(
      target, source, fo4::FormKey::parse("000802:Source.esp"));
  EXPECT_EQ(result.status, fo4::forms::InfoCopyNewStatus::created);

  const auto keys = direct_info_keys(target, 0x01000801u);
  ASSERT_EQ(keys.size(), 2u);
  EXPECT_EQ(keys[0], result.new_info);
  EXPECT_EQ(keys[1], fo4::FormKey::parse("000820:Patch.esp"));
  const auto* copied = fo4::find_record(target, result.new_info);
  ASSERT_NE(copied, nullptr);
  const auto* pnam = find_subrecord(*copied, "PNAM");
  ASSERT_NE(pnam, nullptr);
  EXPECT_EQ(read_u32(*pnam, 0), 0x01000803u);
}

TEST(InfoCopyNew, MasterOwnedPnamOverrideUsesPhysicalTopicPredecessor) {
  const auto source = fo4::read_plugin(
      nested_info_source(true, false, false, false, false, true), "Source.esp");
  auto target = fo4::read_plugin(
      nested_info_target(true, true, true, true, true), "Patch.esp");

  const auto result = fo4::forms::copy_info_as_new(
      target, source, fo4::FormKey::parse("000802:Source.esp"));
  EXPECT_EQ(result.status, fo4::forms::InfoCopyNewStatus::created);

  const auto keys = direct_info_keys(target, 0x01000801u);
  ASSERT_EQ(keys.size(), 3u);
  EXPECT_EQ(keys[0], fo4::FormKey::parse("000803:Base.esm"));
  EXPECT_EQ(keys[1], result.new_info);
  EXPECT_EQ(keys[2], fo4::FormKey::parse("000820:Patch.esp"));

  const auto* copied = fo4::find_record(target, result.new_info);
  ASSERT_NE(copied, nullptr);
  const auto* pnam = find_subrecord(*copied, "PNAM");
  ASSERT_NE(pnam, nullptr);
  EXPECT_EQ(read_u32(*pnam, 0), 0x01000803u);
  EXPECT_EQ(
      fo4::resolve_plugin_form_id(
          fo4::RawFormId{read_u32(*pnam, 0)}, fo4::plugin_form_id_context(target)),
      fo4::FormKey::parse("000803:Base.esm"));
}

TEST(InfoCopyNew, FailsWhenMasterOwnedPnamOverrideIsInAnotherTopic) {
  const auto source = fo4::read_plugin(nested_info_source(true), "Source.esp");
  auto target = fo4::read_plugin(
      nested_info_target(true, true, true, true, false, true), "Patch.esp");
  const auto before = fo4::write_plugin(target);

  EXPECT_THROW(
      fo4::forms::copy_info_as_new(
          target, source, fo4::FormKey::parse("000802:Source.esp")),
      fo4::forms::InfoCopyError);
  EXPECT_EQ(fo4::write_plugin(target), before);
}

TEST(InfoCopyNew, SamePluginDuplicateFollowsItsSource) {
  const auto source = fo4::read_plugin(same_plugin_info_source(), "Same.esp");
  auto target = fo4::read_plugin(same_plugin_info_source(), "Same.esp");
  const auto result = fo4::forms::copy_info_as_new(
      target, source, fo4::FormKey::parse("000803:Same.esp"));
  EXPECT_EQ(result.status, fo4::forms::InfoCopyNewStatus::created);
  EXPECT_EQ(result.new_info, fo4::FormKey::parse("000900:Same.esp"));

  const auto keys = direct_info_keys(target, 0x00000801u);
  ASSERT_EQ(keys.size(), 3u);
  EXPECT_EQ(keys[0], fo4::FormKey::parse("000802:Same.esp"));
  EXPECT_EQ(keys[1], fo4::FormKey::parse("000803:Same.esp"));
  EXPECT_EQ(keys[2], result.new_info);
}

TEST(InfoCopyNew, ExplicitNullPnamUsesLocalTopicHead) {
  const auto source = fo4::read_plugin(
      nested_info_source(false, true), "Source.esp");
  auto target = fo4::read_plugin(nested_info_target(true, true), "Patch.esp");
  const auto result = fo4::forms::copy_info_as_new(
      target, source, fo4::FormKey::parse("000802:Source.esp"));
  EXPECT_EQ(result.status, fo4::forms::InfoCopyNewStatus::created);

  const auto keys = direct_info_keys(target, 0x01000801u);
  ASSERT_EQ(keys.size(), 2u);
  EXPECT_EQ(keys[0], result.new_info);
  EXPECT_EQ(keys[1], fo4::FormKey::parse("000820:Patch.esp"));
  const auto* copied = fo4::find_record(target, result.new_info);
  ASSERT_NE(copied, nullptr);
  const auto* pnam = find_subrecord(*copied, "PNAM");
  ASSERT_NE(pnam, nullptr);
  EXPECT_EQ(read_u32(*pnam, 0), 0u);
}

TEST(InfoCopyNew, CreatesOnlyMissingTopicChildrenGroup) {
  const auto source = fo4::read_plugin(nested_info_source(), "Source.esp");
  auto target = fo4::read_plugin(nested_info_target(false, false), "Patch.esp");

  const auto result = fo4::forms::copy_info_as_new(
      target, source, fo4::FormKey::parse("000802:Source.esp"));
  EXPECT_EQ(result.status, fo4::forms::InfoCopyNewStatus::created);
  EXPECT_EQ(direct_info_keys(target, 0x01000801u),
            std::vector<fo4::FormKey>{result.new_info});
  EXPECT_NE(find_group(target, fo4::GroupType::topic_children, 0x01000801u), nullptr);
}

TEST(InfoCopyNew, FailsWithoutDestinationDialAndLeavesBytesUnchanged) {
  const auto source = fo4::read_plugin(nested_info_source(), "Source.esp");
  auto target = fo4::read_plugin(
      nested_info_target(false, false, true, false), "Patch.esp");
  const auto before = fo4::write_plugin(target);

  const auto result = fo4::forms::copy_info_as_new(
      target, source, fo4::FormKey::parse("000802:Source.esp"));
  EXPECT_EQ(result.status, fo4::forms::InfoCopyNewStatus::parent_dial_missing);
  EXPECT_EQ(fo4::write_plugin(target), before);
}

TEST(InfoCopyNew, RejectsAmbiguousTopicChildrenGroup) {
  const auto source = fo4::read_plugin(
      nested_info_source(false, false, true), "Source.esp");
  auto target = fo4::read_plugin(nested_info_target(true, false), "Patch.esp");

  EXPECT_THROW(
      fo4::forms::copy_info_as_new(
          target, source, fo4::FormKey::parse("000802:Source.esp")),
      fo4::forms::InfoCopyError);
}

TEST(InfoCopyNew, RejectsNullTopicLabel) {
  const auto source = fo4::read_plugin(
      nested_info_source(false, false, false, true), "Source.esp");
  auto target = fo4::read_plugin(nested_info_target(true, false), "Patch.esp");

  EXPECT_THROW(
      fo4::forms::copy_info_as_new(
          target, source, fo4::FormKey::parse("000802:Source.esp")),
      fo4::forms::InfoCopyError);
}

TEST(InfoCopyNew, RejectsWrongDialogueParentSignature) {
  const auto source = fo4::read_plugin(
      nested_info_source(false, false, false, false, true), "Source.esp");
  auto target = fo4::read_plugin(nested_info_target(true, false), "Patch.esp");

  EXPECT_THROW(
      fo4::forms::copy_info_as_new(
          target, source, fo4::FormKey::parse("000802:Source.esp")),
      fo4::forms::InfoCopyError);
}

TEST(InfoCopyNew, LeavesDestinationUnchangedWhenLocalPnamFailsAfterGroupCreation) {
  const auto source = fo4::read_plugin(same_plugin_info_source(), "Same.esp");
  auto target = fo4::read_plugin(same_plugin_info_source(false), "Same.esp");
  const auto before = fo4::write_plugin(target);

  EXPECT_THROW(
      fo4::forms::copy_info_as_new(
          target, source, fo4::FormKey::parse("000803:Same.esp")),
      fo4::forms::InfoCopyError);
  EXPECT_EQ(fo4::write_plugin(target), before);
  EXPECT_EQ(find_group(target, fo4::GroupType::topic_children, 0x00000801u), nullptr);
}

TEST(InfoCopyNew, RejectsDuplicateSourceInfoFormKey) {
  const auto source = fo4::read_plugin(
      same_plugin_info_source(true, true), "Same.esp");
  auto target = fo4::read_plugin(same_plugin_info_source(), "Same.esp");
  const auto before = fo4::write_plugin(target);

  EXPECT_THROW(
      fo4::forms::copy_info_as_new(
          target, source, fo4::FormKey::parse("000803:Same.esp")),
      fo4::forms::PlacedReferenceError);
  EXPECT_EQ(fo4::write_plugin(target), before);
}
