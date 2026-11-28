#include "fo4/c_api.h"
#include "fo4/c_api_cell.h"

#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace {

using Rows = std::vector<std::pair<std::uint32_t, std::string>>;

Rows table(const char* placed, const char* base) {
  const radicle_placed_ref_flag_row* rows = nullptr;
  std::size_t count = 0;
  EXPECT_EQ(radicle_placed_ref_flag_table(placed, base, &rows, &count), RADICLE_STATUS_OK);
  Rows out;
  for (std::size_t i = 0; i < count; ++i) out.emplace_back(rows[i].bit, rows[i].name);
  return out;
}

// Constraint: masks and names are copied from xEdit Core/wbDefinitionsFO4.pas @93cc0bc5 (REFR 11347-11446, ACHR 4483-4491, projectile types 3200-3210 and 5944-5951); the bit must equal the mask's single set bit.
Rows from_masks(const std::vector<std::pair<std::uint32_t, std::string>>& masks) {
  Rows out;
  for (const auto& [mask, name] : masks) {
    EXPECT_EQ(std::popcount(mask), 1) << name;
    out.emplace_back(static_cast<std::uint32_t>(std::countr_zero(mask)), name);
  }
  return out;
}

const Rows& xedit_default() {
  static const Rows rows = from_masks({
      {0x00000010, "Ground Piece"},
      {0x00000100, "LOD Respects Enable State"},
      {0x00000400, "Persistent"},
      {0x00000800, "Initially Disabled"},
      {0x00010000, "Is Full LOD"},
      {0x04000000, "Filter (Collision Geometry)"},
      {0x08000000, "Bounding Box (Collision Geometry)"},
      {0x10000000, "Reflected By Auto Water"},
      {0x40000000, "Ground"},
      {0x80000000, "Multibound"},
  });
  return rows;
}

const Rows& xedit_static_like() {
  static const Rows rows = from_masks({
      {0x00000010, "Ground Piece"},
      {0x00000100, "LOD Respects Enable State"},
      {0x00000200, "Hidden From Local Map"},
      {0x00000400, "Persistent"},
      {0x00000800, "Initially Disabled"},
      {0x00008000, "Visible when distant"},
      {0x00010000, "Is Full LOD"},
      {0x04000000, "Filter (Collision Geometry)"},
      {0x08000000, "Bounding Box (Collision Geometry)"},
      {0x10000000, "Reflected By Auto Water"},
      {0x20000000, "Don't Havok Settle"},
      {0x40000000, "No Respawn"},
      {0x80000000, "Multibound"},
  });
  return rows;
}

const Rows& xedit_container_like() {
  static const Rows rows = from_masks({
      {0x00000010, "Ground Piece"},
      {0x00000400, "Persistent"},
      {0x00000800, "Initially Disabled"},
      {0x00010000, "Is Full LOD"},
      {0x02000000, "No AI Acquire"},
      {0x04000000, "Filter (Collision Geometry)"},
      {0x08000000, "Bounding Box (Collision Geometry)"},
      {0x20000000, "Don't Havok Settle"},
      {0x40000000, "Ground"},
      {0x80000000, "Multibound"},
  });
  return rows;
}

const Rows& xedit_door() {
  static const Rows rows = from_masks({
      {0x00000004, "Minimal Use Door"},
      {0x00000040, "Hidden From Local Map"},
      {0x00000100, "Inaccessible"},
      {0x00000400, "Persistent"},
      {0x00000800, "Initially Disabled"},
      {0x00010000, "Is Full LOD"},
      {0x04000000, "Filter (Collision Geometry)"},
      {0x08000000, "Bounding Box (Collision Geometry)"},
      {0x20000000, "Don't Havok Settle"},
      {0x40000000, "No Respawn"},
      {0x80000000, "Multibound"},
  });
  return rows;
}

const Rows& xedit_light() {
  static const Rows rows = from_masks({
      {0x00000100, "Doesn't Light Water"},
      {0x00000200, "Casts Shadows"},
      {0x00000400, "Persistent"},
      {0x00000800, "Initially Disabled"},
      {0x00010000, "Never Fades"},
      {0x00020000, "Doesn't Light Landscape"},
      {0x02000000, "No AI Acquire"},
      {0x10000000, "Reflected By Auto Water"},
      {0x20000000, "Don't Havok Settle"},
      {0x40000000, "No Respawn"},
      {0x80000000, "Multibound"},
  });
  return rows;
}

const Rows& xedit_movable_static() {
  static const Rows rows = from_masks({
      {0x00000010, "Ground Piece"},
      {0x00000200, "Motion Blur"},
      {0x00000400, "Persistent"},
      {0x00000800, "Initially Disabled"},
      {0x00010000, "Is Full LOD"},
      {0x04000000, "Filter (Collision Geometry)"},
      {0x08000000, "Bounding Box (Collision Geometry)"},
      {0x10000000, "Reflected By Auto Water"},
      {0x20000000, "Don't Havok Settle"},
      {0x40000000, "No Respawn"},
      {0x80000000, "Multibound"},
  });
  return rows;
}

const Rows& xedit_addon_node() {
  static const Rows rows = from_masks({
      {0x00000400, "Persistent"},
      {0x00000800, "Initially Disabled"},
      {0x00010000, "Is Full LOD"},
      {0x10000000, "Reflected By Auto Water"},
      {0x20000000, "Don't Havok Settle"},
      {0x40000000, "No Respawn"},
      {0x80000000, "Multibound"},
  });
  return rows;
}

const Rows& xedit_item_like() {
  static const Rows rows = from_masks({
      {0x00000010, "Ground Piece"},
      {0x00000400, "Persistent"},
      {0x00000800, "Initially Disabled"},
      {0x00010000, "Is Full LOD"},
      {0x02000000, "No AI Acquire"},
      {0x10000000, "Reflected By Auto Water"},
      {0x20000000, "Don't Havok Settle"},
      {0x40000000, "No Respawn"},
      {0x80000000, "Multibound"},
  });
  return rows;
}

const Rows& xedit_placed_npc() {
  static const Rows rows = from_masks({
      {1u << 9, "Starts Dead"},
      {1u << 10, "Persistent"},
      {1u << 11, "Initially Disabled"},
      {1u << 13, "Starts Unconscious"},
      {1u << 25, "No AI Acquire"},
      {1u << 29, "Don't Havok Settle"},
  });
  return rows;
}

const Rows& xedit_placed_projectile() {
  static const Rows rows = from_masks({
      {0x00000080, "Turn Off Fire"},
      {0x00000400, "Persistent"},
      {0x00000800, "Initially Disabled"},
      {0x10000000, "Reflected By Auto Water"},
      {0x20000000, "Don't Havok Settle"},
      {0x40000000, "No Respawn"},
  });
  return rows;
}

TEST(PlacedRefFlagTable, EveryBaseGroupMatchesTheXeditTable) {
  for (const char* base : {"ACTI", "STAT", "SCOL", "TREE"}) EXPECT_EQ(table("REFR", base), xedit_static_like()) << base;
  for (const char* base : {"CONT", "TERM"}) EXPECT_EQ(table("REFR", base), xedit_container_like()) << base;
  EXPECT_EQ(table("REFR", "DOOR"), xedit_door());
  EXPECT_EQ(table("REFR", "LIGH"), xedit_light());
  EXPECT_EQ(table("REFR", "MSTT"), xedit_movable_static());
  EXPECT_EQ(table("REFR", "ADDN"), xedit_addon_node());
  for (const char* base : {"ALCH", "BOOK", "SCRL", "AMMO", "ARMO", "INGR", "KEYM", "MISC", "FURN", "WEAP"}) {
    EXPECT_EQ(table("REFR", base), xedit_item_like()) << base;
  }
}

TEST(PlacedRefFlagTable, UnmatchedOrMissingBaseSelectsTheDefaultTable) {
  for (const char* base : {"NOTE", "NPC_", "ZZZZ", "", "stat", "STATIC"}) {
    EXPECT_EQ(table("REFR", base), xedit_default()) << base;
  }
  EXPECT_EQ(table("REFR", nullptr), xedit_default());
}

TEST(PlacedRefFlagTable, PlacedNpcIgnoresTheBase) {
  for (const char* base : {"NPC_", "STAT", "DOOR", "ZZZZ"}) EXPECT_EQ(table("ACHR", base), xedit_placed_npc()) << base;
  EXPECT_EQ(table("ACHR", nullptr), xedit_placed_npc());
}

TEST(PlacedRefFlagTable, PlacedProjectileTypesShareOneTableAndIgnoreTheBase) {
  for (const char* placed : {"PARW", "PBAR", "PBEA", "PCON", "PFLA", "PGRE", "PHZD", "PMIS"}) {
    EXPECT_EQ(table(placed, "STAT"), xedit_placed_projectile()) << placed;
    EXPECT_EQ(table(placed, nullptr), xedit_placed_projectile()) << placed;
  }
}

TEST(PlacedRefFlagTable, AnUnknownPlacedSignatureFollowsTheBase) {
  EXPECT_EQ(table("ZZZZ", "STAT"), xedit_static_like());
  EXPECT_EQ(table("ZZZZ", nullptr), xedit_default());
}

TEST(PlacedRefFlagTable, TheSameBitNamesDifferentFlagsPerBase) {
  const auto bit9 = [](const Rows& rows) {
    for (const auto& [bit, name] : rows) {
      if (bit == 9) return name;
    }
    return std::string{};
  };
  EXPECT_EQ(bit9(table("REFR", "STAT")), "Hidden From Local Map");
  EXPECT_EQ(bit9(table("REFR", "MSTT")), "Motion Blur");
  EXPECT_EQ(bit9(table("REFR", "LIGH")), "Casts Shadows");
  EXPECT_EQ(bit9(table("REFR", "ZZZZ")), "");
}

TEST(PlacedRefFlagTable, RowsAreAscendingAndStable) {
  const radicle_placed_ref_flag_row* first = nullptr;
  const radicle_placed_ref_flag_row* second = nullptr;
  std::size_t count = 0;
  ASSERT_EQ(radicle_placed_ref_flag_table("REFR", "DOOR", &first, &count), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_placed_ref_flag_table("REFR", "DOOR", &second, &count), RADICLE_STATUS_OK);
  EXPECT_EQ(first, second);
  for (std::size_t i = 1; i < count; ++i) EXPECT_LT(first[i - 1].bit, first[i].bit);
}

TEST(PlacedRefFlagTable, NullArgumentsAreInvalid) {
  const radicle_placed_ref_flag_row* rows = nullptr;
  std::size_t count = 0;
  EXPECT_EQ(radicle_placed_ref_flag_table(nullptr, "STAT", &rows, &count), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_placed_ref_flag_table("REFR", "STAT", nullptr, &count), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_placed_ref_flag_table("REFR", "STAT", &rows, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
}

}  // namespace
