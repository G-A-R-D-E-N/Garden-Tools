#pragma once

#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/vmad.hpp"
#include "plugin_bytes.hpp"

namespace fo4::test::vmad {

inline void name(ByteWriter& out, std::string_view text) {
  out.u16(static_cast<std::uint16_t>(text.size()));
  out.raw(text);
}

inline std::vector<std::byte> payload(std::uint16_t format = 2) {
  ByteWriter out;
  out.u16(6); out.u16(format); out.u16(1);
  name(out, "Script"); out.u8(3); out.u16(12);
  for (std::uint8_t type = 1; type <= 5; ++type) {
    for (int array = 0; array < 2; ++array) {
      name(out, std::to_string(type) + (array ? "Array" : "Value"));
      out.u8(static_cast<std::uint8_t>(type + (array ? 10 : 0))); out.u8(7);
      if (array) out.u32(2);
      for (int i = 0; i < (array ? 2 : 1); ++i) {
        if (type == 1) {
          if (format == 1) out.u32(0x801);
          out.u16(format == 2 ? 0 : 0xFFFF);
          out.u16(format == 2 ? 0xFFFF : 0);
          if (format == 2) out.u32(0x801);
        } else if (type == 2) {
          name(out, i == 0 ? "old" : "second");
        } else if (type == 3) {
          out.i32(-10 - i);
        } else if (type == 4) {
          out.f32(1.5f + static_cast<float>(i));
        } else {
          out.u8(static_cast<std::uint8_t>(i));
        }
      }
    }
  }
  name(out, "EmptyStruct"); out.u8(7); out.u8(1); out.u32(0);
  name(out, "EmptyVariableArray"); out.u8(16); out.u8(1); out.u32(0);
  return out.bytes;
}

inline std::vector<std::byte> plugin_bytes(std::vector<std::byte> vmad_data = payload(),
                                           std::string_view signature = "REFR", bool duplicate = false) {
  ByteWriter out;
  auto record = begin_record(out, "TES4", 0, 0);
  ByteWriter hedr; hedr.f32(1); hedr.u32(3); hedr.u32(0x900);
  write_subrecord(out, "HEDR", hedr.bytes); end_record(out, record);
  auto group = begin_group(out, signature, 0);
  record = begin_record(out, signature, 0, 0x800);
  write_subrecord(out, "VMAD", vmad_data);
  if (duplicate) write_subrecord(out, "VMAD", vmad_data);
  write_subrecord(out, "ZZZZ", "untouched");
  end_record(out, record); end_group(out, group);
  group = begin_group(out, "MISC", 0);
  record = begin_record(out, "MISC", 0, 0x801);
  write_subrecord(out, "ZZZZ", "other record");
  end_record(out, record); end_group(out, group);
  group = begin_group(out, "ZZZZ", 0);
  record = begin_record(out, "ZZZZ", 0, 0x802);
  end_record(out, record); end_group(out, group);
  return out.bytes;
}

inline std::vector<std::byte> plugin_bytes_without_vmad(std::string_view signature = "MISC",
                                                        bool editor_id = true) {
  ByteWriter out;
  auto record = begin_record(out, "TES4", 0, 0);
  ByteWriter hedr; hedr.f32(1); hedr.u32(3); hedr.u32(0x900);
  write_subrecord(out, "HEDR", hedr.bytes); end_record(out, record);
  auto group = begin_group(out, signature, 0);
  record = begin_record(out, signature, 0, 0x800);
  if (editor_id) write_subrecord(out, "EDID", "TestRecord");
  write_subrecord(out, "ZZZZ", "kept");
  end_record(out, record); end_group(out, group);
  return out.bytes;
}

inline FormKey key(std::uint32_t local = 0x800, std::string_view owner = "Vmad.esp") {
  return {PluginKey::from_filename(owner), LocalFormId{local}};
}

inline const Subrecord& subrecord(const Plugin& plugin, std::string_view owner = "Vmad.esp") {
  const auto* record = find_record(plugin, key(0x800, owner));
  if (record == nullptr) throw std::runtime_error("fixture record missing");
  for (const auto& sub : record->subrecords) {
    if (sub.signature.to_string() == "VMAD") return sub;
  }
  throw std::runtime_error("fixture VMAD missing");
}

}
