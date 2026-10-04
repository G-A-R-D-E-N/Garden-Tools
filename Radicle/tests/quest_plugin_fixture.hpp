#pragma once

#include "fo4/formid.hpp"

#include "plugin_bytes.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::test {

inline void wstring(ByteWriter& out, const std::string& text) {
  out.u16(static_cast<std::uint16_t>(text.size()));
  out.raw(text);
}

/* A VMAD property: name, type, flags, then the payload for that type. */
inline void int_property(ByteWriter& out, const std::string& name, std::int32_t value,
                         std::uint8_t flags = 0) {
  wstring(out, name);
  out.u8(3);
  out.u8(flags);
  out.i32(value);
}

inline void float_property(ByteWriter& out, const std::string& name, float value,
                           std::uint8_t flags = 0) {
  wstring(out, name);
  out.u8(4);
  out.u8(flags);
  out.f32(value);
}

inline void bool_property(ByteWriter& out, const std::string& name, bool value,
                          std::uint8_t flags = 0) {
  wstring(out, name);
  out.u8(5);
  out.u8(flags);
  out.u8(value ? 1 : 0);
}

inline void string_property(ByteWriter& out, const std::string& name, const std::string& value,
                            std::uint8_t flags = 0) {
  wstring(out, name);
  out.u8(2);
  out.u8(flags);
  wstring(out, value);
}

inline void int_array_property(ByteWriter& out, const std::string& name,
                               const std::vector<std::int32_t>& values, std::uint8_t flags = 0) {
  wstring(out, name);
  out.u8(13);
  out.u8(flags);
  out.u32(static_cast<std::uint32_t>(values.size()));
  for (const auto value : values) {
    out.i32(value);
  }
}

inline void object_property(ByteWriter& out,
                            const std::string& name,
                            std::uint16_t alias,
                            std::uint32_t form_id,
                            std::uint8_t flags = 0) {
  wstring(out, name);
  out.u8(1);
  out.u8(flags);
  out.u16(0);
  out.u16(alias);
  out.u32(form_id);
}

/* The leading VMAD script list: version 5, object format 2, one script whose properties cover
   every value kind this reader decodes. */
inline std::vector<std::byte> vmad_scripts(std::int32_t count = 7) {
  ByteWriter out;
  out.u16(5);
  out.u16(2);
  out.u16(1);
  wstring(out, "QuestScript");
  out.u8(0);
  out.u16(5);
  int_property(out, "Count", count);
  float_property(out, "Ratio", 1.5f, 1);
  bool_property(out, "Enabled", true);
  string_property(out, "Label", "Garden\tPatch");
  int_array_property(out, "Stages", {1, 2, 3});
  return out.bytes;
}

/* The quest extras that follow it: fragment script, one stage fragment, one alias script. */
inline std::vector<std::byte> vmad_extras() {
  ByteWriter out;
  out.u8(0);
  out.u16(1);
  wstring(out, "QF_TestQuest");
  out.u8(0);
  out.u16(1);
  int_property(out, "Stage", 10);

  out.u16(10);
  out.u16(0);
  out.u32(0);
  out.u8(0);
  wstring(out, "QF_TestQuest");
  wstring(out, "Fragment_Stage_0010");

  out.u16(1);
  out.u16(0);
  out.u16(5);
  out.u32(0);
  out.u16(0);
  out.u16(2);
  out.u16(1);
  wstring(out, "AliasScript");
  out.u8(0);
  out.u16(1);
  object_property(out, "Target", 5, 0x00012345);
  return out.bytes;
}

inline std::vector<std::byte> vmad_subrecord(std::int32_t count = 7) {
  ByteWriter out;
  const auto scripts = vmad_scripts(count);
  const auto extras = vmad_extras();
  out.raw(scripts);
  out.raw(extras);
  return out.bytes;
}

/* A synthetic plugin with one QUST record carrying DNAM flags and that VMAD. */
inline std::vector<std::byte> make_quest_plugin(std::uint16_t dnam_flags, std::int32_t count = 7) {
  ByteWriter out;
  const auto tes4 = begin_record(out, "TES4", 0, 0);
  ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(2);
  hedr.u32(0x900);
  write_subrecord(out, "HEDR", hedr.bytes);
  end_record(out, tes4);

  const auto grup = begin_group(out, "QUST", 0);
  const auto rec = begin_record(out, "QUST", 0, 0x00000800);
  write_subrecord(out, "EDID", std::string_view{"TestQuest\0", 10});
  ByteWriter dnam;
  dnam.u16(dnam_flags);
  dnam.u8(0);
  dnam.u8(0);
  dnam.u32(0);
  dnam.u32(0);
  write_subrecord(out, "DNAM", dnam.bytes);
  write_subrecord(out, "VMAD", vmad_subrecord(count));
  end_record(out, rec);
  end_group(out, grup);
  return out.bytes;
}

inline fo4::FormKey quest_key() { return fo4::FormKey::parse("000800:Quests.esp"); }

}  // namespace fo4::test
