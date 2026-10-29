#pragma once

#include "../../Radicle/tests/plugin_bytes.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <string_view>
#include <vector>

namespace fo4::test::packtree {

inline std::vector<std::byte> z(std::string_view text) {
  std::vector<std::byte> out;
  for (const char ch : text) out.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  out.push_back(std::byte{0});
  return out;
}

inline std::vector<std::byte> d(std::uint32_t value) {
  std::vector<std::byte> out(4, std::byte{0});
  std::memcpy(out.data(), &value, 4);
  return out;
}

inline std::vector<std::byte> u8(std::uint8_t value) { return {std::byte{value}}; }

inline void header(ByteWriter& out) {
  const auto tes4 = begin_record(out, "TES4", 0, 0);
  ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x900);
  write_subrecord(out, "HEDR", hedr.bytes);
  end_record(out, tes4);
}

inline void pku(ByteWriter& out, std::uint32_t count, std::uint32_t template_form = 0) {
  ByteWriter body;
  body.u32(count);
  body.u32(template_form);
  body.u32(1);
  write_subrecord(out, "PKCU", body.bytes);
}

inline void prcb(ByteWriter& out, std::uint32_t children) {
  ByteWriter body;
  body.u32(children);
  body.u32(0);
  write_subrecord(out, "PRCB", body.bytes);
}

inline void input_bool(ByteWriter& out) {
  write_subrecord(out, "ANAM", z("Bool"));
  write_subrecord(out, "CNAM", u8(0));
}

inline void input_location_input_id(ByteWriter& out, std::uint32_t target) {
  write_subrecord(out, "ANAM", z("Location"));
  ByteWriter body;
  body.u32(11);
  body.u32(target);
  body.u32(0);
  body.u32(0);
  write_subrecord(out, "PLDT", body.bytes);
}

inline void unam(ByteWriter& out, std::uint8_t id) { write_subrecord(out, "UNAM", u8(id)); }

inline void xnam(ByteWriter& out, std::uint8_t id) { write_subrecord(out, "XNAM", u8(id)); }

inline void leaf(ByteWriter& out, std::string_view procedure, std::initializer_list<std::uint8_t> ids) {
  write_subrecord(out, "ANAM", z("Procedure"));
  write_subrecord(out, "CITC", d(0));
  write_subrecord(out, "PNAM", z(procedure));
  for (const auto id : ids) write_subrecord(out, "PKC2", u8(id));
}

inline void leaf_without_payload(ByteWriter& out, std::string_view procedure) {
  leaf(out, procedure, {});
}

inline void leaf_with_unknown_payload(ByteWriter& out, std::string_view procedure) {
  write_subrecord(out, "ANAM", z("Procedure"));
  write_subrecord(out, "CITC", d(0));
  write_subrecord(out, "PNAM", z(procedure));
  write_subrecord(out, "ZZZZ", std::vector<std::byte>{});
}

inline std::vector<std::byte> plugin() {
  ByteWriter out;
  header(out);
  const auto pack = begin_group(out, "PACK", 0);

  const auto nested = begin_record(out, "PACK", 0, 0x800);
  pku(out, 0);
  write_subrecord(out, "ANAM", z("Sequence"));
  write_subrecord(out, "CITC", d(0));
  prcb(out, 1);
  write_subrecord(out, "ANAM", z("Stacked"));
  write_subrecord(out, "CITC", d(0));
  prcb(out, 2);
  leaf_without_payload(out, "Activate");
  leaf_without_payload(out, "Wander");
  write_subrecord(out, "POBA", std::string_view{});
  end_record(out, nested);

  const auto undetermined = begin_record(out, "PACK", 0, 0x801);
  pku(out, 0);
  write_subrecord(out, "ANAM", z("Sequence"));
  write_subrecord(out, "CITC", d(0));
  prcb(out, 2);
  leaf_without_payload(out, "Activate");
  leaf_with_unknown_payload(out, "Wait");
  write_subrecord(out, "POBA", std::string_view{});
  end_record(out, undetermined);

  const auto shared = begin_record(out, "PACK", 0, 0x804);
  pku(out, 2);
  input_bool(out);
  input_bool(out);
  unam(out, 1);
  unam(out, 3);
  xnam(out, 4);
  write_subrecord(out, "ANAM", z("Sequence"));
  write_subrecord(out, "CITC", d(0));
  prcb(out, 2);
  leaf(out, "Activate", {1});
  leaf(out, "Wander", {3, 1});
  write_subrecord(out, "POBA", std::string_view{});
  end_record(out, shared);

  const auto exclusive = begin_record(out, "PACK", 0, 0x805);
  pku(out, 2);
  input_bool(out);
  input_bool(out);
  unam(out, 7);
  unam(out, 9);
  xnam(out, 11);
  write_subrecord(out, "ANAM", z("Sequence"));
  write_subrecord(out, "CITC", d(0));
  prcb(out, 2);
  leaf(out, "Activate", {7});
  leaf(out, "Wander", {9});
  write_subrecord(out, "POBA", std::string_view{});
  end_record(out, exclusive);

  const auto cross_referenced = begin_record(out, "PACK", 0, 0x806);
  pku(out, 2);
  input_location_input_id(out, 7);
  input_bool(out);
  unam(out, 5);
  unam(out, 7);
  write_subrecord(out, "ANAM", z("Sequence"));
  write_subrecord(out, "CITC", d(0));
  prcb(out, 2);
  leaf(out, "Activate", {7});
  leaf(out, "Wander", {5});
  write_subrecord(out, "POBA", std::string_view{});
  end_record(out, cross_referenced);

  const auto templated = begin_record(out, "PACK", 0, 0x807);
  pku(out, 0, 0x800);
  write_subrecord(out, "ANAM", z("Sequence"));
  write_subrecord(out, "CITC", d(0));
  prcb(out, 1);
  leaf_without_payload(out, "Activate");
  write_subrecord(out, "POBA", std::string_view{});
  end_record(out, templated);

  const auto duplicate_ids = begin_record(out, "PACK", 0, 0x808);
  pku(out, 2);
  input_bool(out);
  input_bool(out);
  unam(out, 1);
  unam(out, 1);
  write_subrecord(out, "ANAM", z("Sequence"));
  write_subrecord(out, "CITC", d(0));
  prcb(out, 1);
  leaf(out, "Activate", {1});
  write_subrecord(out, "POBA", std::string_view{});
  end_record(out, duplicate_ids);

  const auto undecodable = begin_record(out, "PACK", 0, 0x803);
  pku(out, 0);
  write_subrecord(out, "ANAM", z("Sequence"));
  write_subrecord(out, "CITC", d(0));
  prcb(out, 2);
  leaf_without_payload(out, "Activate");
  write_subrecord(out, "POBA", std::string_view{});
  end_record(out, undecodable);

  end_group(out, pack);

  const auto misc = begin_group(out, "MISC", 0);
  const auto other = begin_record(out, "MISC", 0, 0x802);
  write_subrecord(out, "EDID", z("Other"));
  end_record(out, other);
  end_group(out, misc);
  return out.bytes;
}

}  // namespace fo4::test::packtree
