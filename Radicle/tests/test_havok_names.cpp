#include "fo4/nif/havok_names.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <stdexcept>
#include <vector>

// Detects: a material CRC that ignores the lowercase rule or changes its init or final XOR, so names no longer hash to the values the game stores.
TEST(HavokNames, MaterialCrcMatchesTheGameValuesCaseInsensitively) {
  EXPECT_EQ(fo4::nif::havok_material_crc("MaterialWoodCrate"), 341181474U);
  EXPECT_EQ(fo4::nif::havok_material_crc("metal"), 0x064003D4U);
  EXPECT_EQ(fo4::nif::havok_material_crc("Metal"), 0x064003D4U);
}

// Detects: a layer table that stores names at the row position instead of the row's index, so layer 1 reads back as the wrong name.
TEST(HavokNames, LayerTableStoresEachNameAtItsIndex) {
  const std::vector<fo4::nif::HavokLayerRow> rows{{1, "L_STATIC"}, {0, "L_UNIDENTIFIED"}, {2, "L_ANIMSTATIC"}};
  const auto names = fo4::nif::build_havok_layer_names(rows);
  ASSERT_EQ(names.size(), 3U);
  EXPECT_EQ(names[0], "L_UNIDENTIFIED");
  EXPECT_EQ(names[1], "L_STATIC");
  EXPECT_EQ(names[2], "L_ANIMSTATIC");
}

// Detects: a layer table that accepts a gap or a duplicate index, which would leave a layer unnamed or name two layers from one COLL row.
TEST(HavokNames, LayerTableRejectsAGapOrADuplicateIndex) {
  const std::vector<fo4::nif::HavokLayerRow> gap{{0, "a"}, {2, "b"}};
  const std::vector<fo4::nif::HavokLayerRow> duplicate{{0, "a"}, {0, "b"}};
  EXPECT_THROW(fo4::nif::build_havok_layer_names(gap), std::invalid_argument);
  EXPECT_THROW(fo4::nif::build_havok_layer_names(duplicate), std::invalid_argument);
}

// Detects: a layer table that accepts more than 256 rows, which would name a layer index no packed filter byte can hold.
TEST(HavokNames, LayerTableRejectsMoreThan256Layers) {
  std::vector<fo4::nif::HavokLayerRow> rows;
  for (std::uint32_t i = 0; i < 257; ++i) rows.push_back({i, "layer"});
  EXPECT_THROW(fo4::nif::build_havok_layer_names(rows), std::invalid_argument);
}
