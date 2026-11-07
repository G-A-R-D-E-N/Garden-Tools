#pragma once

#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/field_address.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/preflight.hpp"
#include "fo4/schema/record_constraints.hpp"
#include "fo4/schema/scalar.hpp"
#include "authoring_order.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace object_base_fixture {

using Bytes = std::vector<std::byte>;
using fo4::test::ByteWriter;

struct Sub {
  std::string_view signature;
  Bytes data;
};

struct Entry {
  std::uint32_t local_id;
  std::vector<Sub> subs;
};

struct Item {
  std::string_view signature;
  std::string_view key;
  std::string_view scalar_path;
  std::string_view scalar_value;
  std::string_view text_path;
  std::string_view link_path;
  std::uint32_t link_start;
  std::string_view link_target;
};

struct ModelTexture {
  std::uint32_t file_hash;
  std::string_view extension;
  std::uint32_t folder_hash;
};

inline constexpr ModelTexture k_door_textures[] = {{0x0A0B0C0D, "dds", 0x11121314},
                                            {0x1A1B1C1D, "dds", 0x21222324}};
inline constexpr std::uint32_t k_door_addon_nodes[] = {42};
inline constexpr ModelTexture k_cont_textures[] = {{0x31323334, "dds", 0x41424344},
                                            {0x51525354, "dds", 0x61626364}};
inline constexpr ModelTexture k_cont_materials[] = {{0x71727374, "bgsm", 0x81828384}};

inline constexpr Item k_items[] = {
    {"ACTI", "000801:Patch.esp", "ACTI\\BoundMaxX", "7", "ACTI\\Model", "ACTI\\LoopingSound",
     0x800, "000808:Patch.esp"},
    {"CONT", "000802:Patch.esp", "CONT\\Weight", "2.5", "CONT\\Model", "CONT\\OpenSound", 0x800,
     "000808:Patch.esp"},
    {"DOOR", "000803:Patch.esp", "DOOR\\Flags", "2", "DOOR\\Model", "DOOR\\OpenSound", 0x800,
     "000808:Patch.esp"},
    {"FURN", "000804:Patch.esp", "FURN\\BenchType", "7", "FURN\\Model", "FURN\\PreviewTransform",
     0x809, "00080A:Patch.esp"},
    {"MSTT", "000805:Patch.esp", "MSTT\\OnLocalMap", "0", "MSTT\\Model", "MSTT\\LoopingSound",
     0x800, "000808:Patch.esp"},
    {"TACT", "000806:Patch.esp", "TACT\\BoundMinX", "-4", "TACT\\Model", "TACT\\LoopingSound",
     0x800, "000808:Patch.esp"},
    {"TERM", "000807:Patch.esp", "TERM\\Flags", "4", "TERM\\Model", "TERM\\PreviewTransform",
     0x809, "00080A:Patch.esp"},
};

constexpr std::uint32_t raw_id(std::uint32_t local_id) { return 0x02000000u | local_id; }

inline Bytes zstr(std::string_view text) {
  ByteWriter out;
  out.raw(text);
  out.u8(0);
  return out.bytes;
}

inline Bytes zeros(std::size_t size) { return Bytes(size); }

inline Bytes u32_bytes(std::uint32_t value) {
  ByteWriter out;
  out.u32(value);
  return out.bytes;
}

inline Bytes u16_bytes(std::uint16_t value) {
  ByteWriter out;
  out.u16(value);
  return out.bytes;
}

inline Bytes f32_bytes(float value) {
  ByteWriter out;
  out.f32(value);
  return out.bytes;
}

inline Bytes u8_bytes(std::uint8_t value) {
  ByteWriter out;
  out.u8(value);
  return out.bytes;
}

inline Bytes container_data() {
  ByteWriter out;
  out.u8(1);
  out.f32(1.5f);
  return out.bytes;
}

inline std::uint32_t four_cc(std::string_view text) {
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < text.size(); ++i) {
    value |= std::uint32_t{static_cast<std::uint8_t>(text[i])} << (8 * i);
  }
  return value;
}

inline Bytes modt_data(const std::vector<ModelTexture>& textures,
                const std::vector<std::uint32_t>& addon_nodes,
                const std::vector<ModelTexture>& materials,
                std::uint32_t srgb = 0) {
  ByteWriter out;
  out.u32(4);
  out.u32(static_cast<std::uint32_t>(textures.size()));
  out.u32(static_cast<std::uint32_t>(addon_nodes.size()));
  out.u32(srgb);
  out.u32(static_cast<std::uint32_t>(materials.size()));
  for (const auto& entry : textures) {
    out.u32(entry.file_hash);
    out.sig(entry.extension);
    out.u32(entry.folder_hash);
  }
  for (const auto node : addon_nodes) {
    out.u32(node);
  }
  for (const auto& entry : materials) {
    out.u32(entry.file_hash);
    out.sig(entry.extension);
    out.u32(entry.folder_hash);
  }
  return out.bytes;
}

inline void write_group(ByteWriter& out, std::string_view label, const std::vector<Entry>& entries) {
  const auto group = fo4::test::begin_group(out, label, 0);
  for (const auto& entry : entries) {
    const auto record = fo4::test::begin_record(out, label, 0, raw_id(entry.local_id));
    for (const auto& sub : entry.subs) {
      fo4::test::write_subrecord(out, sub.signature, sub.data);
    }
    fo4::test::end_record(out, record);
  }
  fo4::test::end_group(out, group);
}

inline std::vector<std::byte> make_plugin() {
  ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(2);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Fallout4.esm\0", 13});
  fo4::test::write_subrecord(out, "DATA", zeros(8));
  fo4::test::write_subrecord(out, "MAST", std::string_view{"MagicMaster.esp\0", 16});
  fo4::test::write_subrecord(out, "DATA", zeros(8));
  fo4::test::end_record(out, tes4);

  write_group(out, "SNDR", {{0x800, {{"EDID", zstr("RadicleSoundA")}}},
                            {0x808, {{"EDID", zstr("RadicleSoundB")}}}});
  write_group(out, "TRNS", {{0x809, {{"EDID", zstr("RadicleTransformA")}}},
                            {0x80A, {{"EDID", zstr("RadicleTransformB")}}}});
  write_group(out, "ACTI", {{0x801, {{"EDID", zstr("RadicleActi")},
                                     {"OBND", zeros(12)},
                                     {"MODL", zstr("Light.nif")},
                                     {"SNAM", u32_bytes(raw_id(0x800))},
                                     {"FNAM", u16_bytes(1)}}}});
  write_group(out, "CONT", {{0x802, {{"EDID", zstr("RadicleCont")},
                                     {"OBND", zeros(12)},
                                     {"MODL", zstr("Crate.nif")},
                                     {"MODT", modt_data({k_cont_textures[0], k_cont_textures[1]}, {},
                                                        {k_cont_materials[0]}, 2)},
                                     {"MODC", f32_bytes(0.25f)},
                                     {"MODF", u8_bytes(2)},
                                     {"DATA", container_data()},
                                     {"SNAM", u32_bytes(raw_id(0x800))}}}});
  write_group(out, "DOOR", {{0x803, {{"EDID", zstr("RadicleDoor")},
                                     {"OBND", zeros(12)},
                                     {"MODL", zstr("Door.nif")},
                                     {"MODT", modt_data({k_door_textures[0], k_door_textures[1]},
                                                        {k_door_addon_nodes[0]}, {}, 1)},
                                     {"MODC", f32_bytes(2.5f)},
                                     {"MODF", u8_bytes(1)},
                                     {"SNAM", u32_bytes(raw_id(0x800))},
                                     {"FNAM", u8_bytes(1)}}}});
  write_group(out, "FURN", {{0x804, {{"EDID", zstr("RadicleFurn")},
                                     {"OBND", zeros(12)},
                                     {"PTRN", u32_bytes(raw_id(0x809))},
                                     {"MODL", zstr("Bench.nif")},
                                     {"PNAM", zeros(4)},
                                     {"FNAM", u16_bytes(2)},
                                     {"WBDT", u8_bytes(1)}}}});
  write_group(out, "MSTT", {{0x805, {{"EDID", zstr("RadicleMstt")},
                                     {"OBND", zeros(12)},
                                     {"MODL", zstr("Pylon.nif")},
                                     {"DATA", u8_bytes(1)},
                                     {"SNAM", u32_bytes(raw_id(0x800))}}}});
  write_group(out, "TACT", {{0x806, {{"EDID", zstr("RadicleTact")},
                                     {"OBND", zeros(12)},
                                     {"MODL", zstr("Talk.nif")},
                                     {"PNAM", zeros(4)},
                                     {"SNAM", u32_bytes(raw_id(0x800))},
                                     {"FNAM", zeros(2)}}}});
  write_group(out, "TERM", {{0x807, {{"EDID", zstr("RadicleTerm")},
                                     {"OBND", zeros(12)},
                                     {"PTRN", u32_bytes(raw_id(0x809))},
                                     {"MODL", zstr("Terminal.nif")},
                                     {"FNAM", u16_bytes(1)}}}});
  return out.bytes;
}

inline const fo4::Subrecord* find_subrecord(const fo4::Record& record, fo4::Signature signature) {
  for (const auto& sub : record.subrecords) {
    if (sub.signature == signature) return &sub;
  }
  return nullptr;
}

inline std::vector<std::string> decoded_slots(const fo4::Record& record, std::string_view path) {
  const auto field = fo4::schema::scalar_field_descriptor(record.header.signature, path);
  EXPECT_TRUE(field.has_value()) << path;
  if (!field) return {};
  const auto resolved = fo4::schema::resolve_field_slots(
      record, fo4::schema::scalar_field_shape(field->scalar), field->scalar.address);
  std::vector<std::string> values;
  for (const auto& slot : resolved.slots) {
    if (!slot.valid) continue;
    values.push_back(fo4::schema::decode_scalar(
        fo4::schema::scalar_slot_bytes(record.subrecords[slot.subrecord].data, slot, field->scalar),
        field->scalar));
  }
  return values;
}

inline std::vector<std::string> model_values(std::initializer_list<std::uint32_t> raw) {
  std::vector<std::string> values;
  for (const auto value : raw) {
    values.push_back(std::to_string(value));
  }
  return values;
}

inline void expect_no_model_info_issue(const fo4::Record& record) {
  for (const auto& issue : fo4::schema::preflight_record(record)) {
    EXPECT_NE(issue.severity, fo4::schema::PreflightSeverity::error) << issue.path << ' ' << issue.code;
    EXPECT_EQ(issue.path.find("ModelInfo"), std::string::npos) << issue.path << ' ' << issue.code;
  }
}

inline std::size_t rule_index(std::span<const fo4::schema::SubrecordAuthoringConstraint> rules,
                       std::string_view name) {
  const auto signature = fo4::Signature::from_string(name);
  for (std::size_t index = 0; index < rules.size(); ++index) {
    if (rules[index].signature == signature) return index;
  }
  return rules.size();
}

inline Bytes obts_header(std::uint32_t includes, std::uint32_t properties, std::uint8_t level_min,
                  std::uint8_t level_max, std::int16_t parent, std::uint8_t is_default,
                  std::uint8_t min_rank, std::uint8_t alt_levels, std::uint8_t keywords) {
  ByteWriter out;
  out.u32(includes);
  out.u32(properties);
  out.u8(level_min);
  out.u8(0);
  out.u8(level_max);
  out.u8(0);
  out.u16(static_cast<std::uint16_t>(parent));
  out.u8(is_default);
  out.u8(keywords);
  for (std::uint8_t i = 0; i < keywords; ++i) out.u32(0x00100000u + i);
  out.u8(min_rank);
  out.u8(alt_levels);
  return out.bytes;
}

inline std::string read_row(const fo4::Plugin& plugin, const fo4::FormKey& key, std::string_view path,
                     std::size_t index) {
  const auto* record = fo4::find_record(plugin, key);
  if (record == nullptr) return "no-record";
  const auto field = fo4::schema::scalar_field_descriptor(record->header.signature, path);
  if (!field) return "no-field";
  const auto resolved = fo4::schema::resolve_field_slots(
      *record, fo4::schema::scalar_field_shape(field->scalar), field->scalar.address);
  if (index >= resolved.slots.size() || !resolved.slots[index].valid) return "absent";
  const auto slot = resolved.slots[index];
  const auto bytes =
      fo4::schema::scalar_slot_bytes(record->subrecords[slot.subrecord].data, slot, field->scalar);
  if (bytes.empty()) return "absent";
  return fo4::schema::decode_scalar(bytes, field->scalar);
}

inline fo4::Plugin template_record_plugin(std::string_view signature, std::uint32_t local_id,
                                   const std::vector<Sub>& subs,
                                   std::uint16_t form_version = 131) {
  ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
  const auto group = fo4::test::begin_group(out, signature, 0);
  const auto record = fo4::test::begin_record(out, signature, 0, local_id, form_version);
  for (const auto& sub : subs) {
    fo4::test::write_subrecord(out, sub.signature, sub.data);
  }
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return fo4::read_plugin(out.bytes, "Patch.esp");
}

inline const fo4::Record* only_record(const fo4::Plugin& plugin, std::string_view signature) {
  const auto wanted = fo4::Signature::from_string(signature);
  const fo4::Record* found = nullptr;
  fo4::visit_records(plugin, [&](const fo4::Record& record) {
    if (record.header.signature == wanted) found = &record;
  });
  return found;
}

inline fo4::Plugin destructible_acti_plugin(const std::vector<Sub>& stages) {
  ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
  const auto group = fo4::test::begin_group(out, "ACTI", 0);
  const auto record = fo4::test::begin_record(out, "ACTI", 0, 0x820);
  fo4::test::write_subrecord(out, "EDID", zstr("RadicleActi"));
  fo4::test::write_subrecord(out, "OBND", zeros(12));
  for (const auto& sub : stages) {
    fo4::test::write_subrecord(out, sub.signature, sub.data);
  }
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return fo4::read_plugin(out.bytes, "Patch.esp");
}

inline const fo4::Record* only_acti(const fo4::Plugin& plugin) {
  return fo4::find_record(plugin, fo4::FormKey::parse("000820:Patch.esp"));
}

}  // namespace object_base_fixture
