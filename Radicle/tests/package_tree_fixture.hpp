#pragma once

#include "../../Radicle/tests/plugin_bytes.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
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

inline void header(ByteWriter& out) {
  const auto tes4 = begin_record(out, "TES4", 0, 0);
  ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x900);
  write_subrecord(out, "HEDR", hedr.bytes);
  end_record(out, tes4);
}

inline void pku(ByteWriter& out) {
  ByteWriter body;
  body.u32(0);
  body.u32(0);
  body.u32(1);
  write_subrecord(out, "PKCU", body.bytes);
}

inline void prcb(ByteWriter& out, std::uint32_t children) {
  ByteWriter body;
  body.u32(children);
  body.u32(0);
  write_subrecord(out, "PRCB", body.bytes);
}

inline void tree_leaf(ByteWriter& out, std::string_view procedure, bool with_pkc2 = false) {
  write_subrecord(out, "ANAM", z("Procedure"));
  write_subrecord(out, "CITC", d(0));
  write_subrecord(out, "PNAM", z(procedure));
  if (with_pkc2) write_subrecord(out, "PKC2", std::vector<std::byte>{std::byte{0}});
}

inline std::vector<std::byte> plugin() {
  ByteWriter out;
  header(out);
  const auto pack = begin_group(out, "PACK", 0);

  const auto nested = begin_record(out, "PACK", 0, 0x800);
  pku(out);
  write_subrecord(out, "ANAM", z("Sequence"));
  write_subrecord(out, "CITC", d(0));
  prcb(out, 1);
  write_subrecord(out, "ANAM", z("Stacked"));
  write_subrecord(out, "CITC", d(0));
  prcb(out, 2);
  tree_leaf(out, "Activate");
  tree_leaf(out, "Wander");
  write_subrecord(out, "POBA", {});
  end_record(out, nested);

  const auto referenced = begin_record(out, "PACK", 0, 0x801);
  pku(out);
  write_subrecord(out, "ANAM", z("Sequence"));
  write_subrecord(out, "CITC", d(0));
  prcb(out, 1);
  tree_leaf(out, "Wait", true);
  write_subrecord(out, "POBA", {});
  end_record(out, referenced);

  const auto undecodable = begin_record(out, "PACK", 0, 0x803);
  pku(out);
  write_subrecord(out, "ANAM", z("Sequence"));
  write_subrecord(out, "CITC", d(0));
  prcb(out, 2);
  tree_leaf(out, "Activate");
  write_subrecord(out, "POBA", {});
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
