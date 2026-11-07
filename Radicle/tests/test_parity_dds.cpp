#include "parity/common.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstring>
#include <vector>

TEST(ParityDds, Dx10HeaderLongerThanTheFileIsNotAPayload) {
  std::vector<std::byte> dds(140);
  std::memcpy(dds.data(), "DDS ", 4);
  std::memcpy(dds.data() + 84, "DX10", 4);
  EXPECT_EQ(radicle_parity::dds_payload(dds), "-");
}
