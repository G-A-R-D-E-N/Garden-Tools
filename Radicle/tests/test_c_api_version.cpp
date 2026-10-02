#include "fo4/c_api.h"

#include <gtest/gtest.h>

TEST(CApiVersion, ReportsCompiledAbiVersion) {
  EXPECT_EQ(radicle_abi_version(), RADICLE_ABI_VERSION);
}
