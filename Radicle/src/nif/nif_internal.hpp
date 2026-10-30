#pragma once

#include "fo4/binary/cursor.hpp"
#include "fo4/nif/nif.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace fo4::nif::detail {

struct AvHeader {
  std::string name;
  Transform transform;
};

// Invariant: every NiObjectNET-derived block starts with this prefix, in nif.xml field order.
struct NetPrefix {
  std::string name;
  std::size_t name_at{0};
  std::size_t end{0};
};

binary::Cursor block_cursor(const NifFile& nif, std::uint32_t block);
AvHeader read_av_header(binary::Cursor& cursor, const NifFile& nif);
std::string read_string_ref(binary::Cursor& cursor, const NifFile& nif);
NetPrefix read_net_prefix(binary::Cursor& cursor, const NifFile& nif);
std::vector<std::string> read_texture_set(const NifFile& nif, std::uint32_t block);

}  // namespace fo4::nif::detail
