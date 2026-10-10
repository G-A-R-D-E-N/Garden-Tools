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

inline void string_array_property(ByteWriter& out, const std::string& name,
                                  const std::vector<std::string>& values, std::uint8_t flags = 0) {
  wstring(out, name);
  out.u8(12);
  out.u8(flags);
  out.u32(static_cast<std::uint32_t>(values.size()));
  for (const auto& value : values) {
    wstring(out, value);
  }
}

inline void object_array_property(ByteWriter& out, const std::string& name,
                                  const std::vector<std::uint32_t>& form_ids,
                                  std::uint8_t flags = 0) {
  wstring(out, name);
  out.u8(11);
  out.u8(flags);
  out.u32(static_cast<std::uint32_t>(form_ids.size()));
  for (const auto form_id : form_ids) {
    out.u16(0);
    out.u16(0xFFFF);
    out.u32(form_id);
  }
}

inline void struct_array_property(ByteWriter& out, const std::string& name, std::uint32_t count) {
  wstring(out, name);
  out.u8(17);
  out.u8(0);
  out.u32(count);
  for (std::uint32_t index = 0; index < count; ++index) {
    out.u32(0);
  }
}

inline void struct_property(ByteWriter& out, const std::string& name, std::uint32_t member_count,
                            std::uint8_t flags = 0) {
  wstring(out, name);
  out.u8(7);
  out.u8(flags);
  out.u32(member_count);
}

inline void struct_array_header(ByteWriter& out, const std::string& name, std::uint32_t count,
                                std::uint8_t flags = 0) {
  wstring(out, name);
  out.u8(17);
  out.u8(flags);
  out.u32(count);
}

/* The leading VMAD script list: version 5, object format 2, one script whose properties cover
   every value kind this reader decodes, plus an embedded NUL and an empty string. The
   embedded value is built through the 12-byte constructor: a bare literal would itself stop
   at the NUL and the fixture would write 6 bytes instead of 12. */
inline std::vector<std::byte> vmad_scripts(std::int32_t count = 7) {
  ByteWriter out;
  out.u16(5);
  out.u16(2);
  out.u16(1);
  wstring(out, "QuestScript");
  out.u8(0);
  out.u16(7);
  int_property(out, "Count", count);
  float_property(out, "Ratio", 1.5f, 1);
  bool_property(out, "Enabled", true);
  string_property(out, "Label", "Garden\tPatch");
  int_array_property(out, "Stages", {1, 2, 3});
  string_property(out, "Embedded", std::string("Garden\0Patch", 12));
  string_property(out, "Empty", std::string());
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

/* The script list and extras with an int, string and object array on the script, the fragment
   script and an alias script, plus a struct array that the reader does not decode into elements. */
inline std::vector<std::byte> array_vmad_subrecord() {
  ByteWriter out;
  out.u16(5);
  out.u16(2);
  out.u16(1);
  wstring(out, "ArrayScript");
  out.u8(0);
  out.u16(4);
  int_array_property(out, "Ints", {4, 5, 6});
  string_array_property(out, "Words", {"Alpha", "", "Garden"});
  object_array_property(out, "Targets", {0x805, 0x806});
  struct_array_property(out, "Structs", 1);

  out.u8(0);
  out.u16(0);
  wstring(out, "QF_Arrays");
  out.u8(0);
  out.u16(3);
  int_array_property(out, "FragInts", {9});
  string_array_property(out, "FragWords", {"Stage"});
  object_array_property(out, "FragTargets", {0x807});

  out.u16(1);
  out.u16(0);
  out.u16(5);
  out.u32(0);
  out.u16(0);
  out.u16(2);
  out.u16(1);
  wstring(out, "AliasArrays");
  out.u8(0);
  out.u16(1);
  object_array_property(out, "AliasTargets", {0x808});
  return out.bytes;
}

inline std::vector<std::byte> struct_vmad_subrecord() {
  ByteWriter out;
  out.u16(5);
  out.u16(2);
  out.u16(1);
  wstring(out, "StructScript");
  out.u8(0);
  out.u16(2);
  struct_property(out, "Point", 3);
  int_property(out, "X", 4);
  string_property(out, "Label", "Alpha");
  object_property(out, "Target", 0xFFFF, 0x805);
  struct_array_header(out, "Pairs", 2);
  out.u32(1);
  int_property(out, "Left", 1);
  out.u32(2);
  bool_property(out, "Flag", true);
  float_property(out, "Scale", 2.5f);

  out.u8(0);
  out.u16(0);
  wstring(out, "QF_Structs");
  out.u8(0);
  out.u16(1);
  struct_property(out, "FragPoint", 1);
  int_property(out, "Stage", 9);

  out.u16(1);
  out.u16(0);
  out.u16(5);
  out.u32(0);
  out.u16(0);
  out.u16(2);
  out.u16(1);
  wstring(out, "AliasStructs");
  out.u8(0);
  out.u16(1);
  struct_property(out, "AliasPoint", 1);
  object_property(out, "Target", 5, 0x806);
  return out.bytes;
}

/* A synthetic plugin with one QUST record carrying DNAM flags and the given VMAD subrecord. */
inline std::vector<std::byte> make_quest_plugin_with_vmad(std::uint16_t dnam_flags,
                                                          const std::vector<std::byte>& vmad) {
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
  write_subrecord(out, "VMAD", vmad);
  end_record(out, rec);
  end_group(out, grup);
  return out.bytes;
}

inline std::vector<std::byte> make_quest_plugin(std::uint16_t dnam_flags, std::int32_t count = 7) {
  return make_quest_plugin_with_vmad(dnam_flags, vmad_subrecord(count));
}

inline fo4::FormKey quest_key() { return fo4::FormKey::parse("000800:Quests.esp"); }

}
