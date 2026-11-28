#include "fo4/error.hpp"
#include "fo4/nif/hknp_packfile.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

fo4::nif::HknpPackfile tailed_packfile() {
  fo4::nif::HknpPackfile pack;
  pack.classes = {{0x00C0FFEEu, "hclClothData"}, {0x0BADF00Du, "hkaSkeleton"}};
  fo4::nif::HknpPackObject root;
  root.class_name = "hclClothData";
  root.bytes.assign(32, std::byte{0x11});
  root.local.push_back({0x10, 0x18});
  fo4::nif::HknpPackObject skeleton;
  skeleton.class_name = "hkaSkeleton";
  skeleton.bytes.assign(16, std::byte{0x22});
  pack.objects = {root, skeleton};
  pack.header_tail.assign(16, std::byte{0x5A});
  pack.header_tail[0] = std::byte{0x14};
  return pack;
}

}  // namespace

// Detects: a cloth packfile whose 16-byte header tail is dropped, or whose section table is read at 0x40 instead of after the tail; the tail length must be the u16 at 0x3E and the rebuild must return the same bytes.
TEST(HknpPackfileHeader, CarriesAHeaderTailAndRebuildsTheSameBytes) {
  const auto bytes = fo4::nif::write_hknp_packfile(tailed_packfile());
  ASSERT_GE(bytes.size(), 0x50U);
  EXPECT_EQ(std::to_integer<unsigned>(bytes[0x3E]), 0x10U);
  EXPECT_EQ(std::to_integer<unsigned>(bytes[0x3F]), 0U);
  EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(bytes.data()) + 0x50, 14), "__classnames__");
  const auto pack = fo4::nif::read_hknp_packfile(bytes);
  EXPECT_EQ(pack.header_tail, tailed_packfile().header_tail);
  ASSERT_EQ(pack.objects.size(), 2U);
  EXPECT_EQ(pack.objects[1].class_name, "hkaSkeleton");
  EXPECT_EQ(fo4::nif::write_hknp_packfile(pack), bytes);
}

// Detects: a file cut inside its header tail or inside the section table that follows it, which would be read past the end; every such prefix must raise fo4::parse_error.
TEST(HknpPackfileHeader, RejectsEveryPrefixThatEndsInsideTheTailOrSectionTable) {
  const auto bytes = fo4::nif::write_hknp_packfile(tailed_packfile());
  for (std::size_t length = 0x40; length < 0x110; ++length) {
    EXPECT_THROW(fo4::nif::read_hknp_packfile(std::span<const std::byte>(bytes.data(), length)), fo4::parse_error)
        << "prefix of " << length << " bytes";
  }
}
