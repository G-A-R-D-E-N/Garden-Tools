#pragma once

#include "fo4/binary/cursor.hpp"
#include "fo4/nif/nif.hpp"

#include <cstdint>
#include <string>

namespace fo4::nif::detail {

struct AvHeader {
  std::string name;
  Transform transform;
};

binary::Cursor block_cursor(const NifFile& nif, std::uint32_t block);
AvHeader read_av_header(binary::Cursor& cursor, const NifFile& nif);

}  // namespace fo4::nif::detail
