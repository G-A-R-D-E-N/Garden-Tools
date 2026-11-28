#include "c_api_internal.hpp"
#include "fo4/c_api_cell.h"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string_view>

namespace {

using namespace radicle_c_detail;

struct Table {
  const radicle_placed_ref_flag_row* rows;
  std::size_t count;
};

template <std::size_t N>
constexpr Table table_of(const radicle_placed_ref_flag_row (&rows)[N]) {
  return Table{rows, N};
}

// Constraint: rows and names are xEdit Core/wbDefinitionsFO4.pas @93cc0bc5 lines 11347-11446 (REFR), 4483-4491 (ACHR) and 3200-3210 with 5944-5951 (projectile types), in ascending bit order.
constexpr radicle_placed_ref_flag_row k_default[] = {
    {4, "Ground Piece"},
    {8, "LOD Respects Enable State"},
    {10, "Persistent"},
    {11, "Initially Disabled"},
    {16, "Is Full LOD"},
    {26, "Filter (Collision Geometry)"},
    {27, "Bounding Box (Collision Geometry)"},
    {28, "Reflected By Auto Water"},
    {30, "Ground"},
    {31, "Multibound"},
};

constexpr radicle_placed_ref_flag_row k_static_like[] = {
    {4, "Ground Piece"},
    {8, "LOD Respects Enable State"},
    {9, "Hidden From Local Map"},
    {10, "Persistent"},
    {11, "Initially Disabled"},
    {15, "Visible when distant"},
    {16, "Is Full LOD"},
    {26, "Filter (Collision Geometry)"},
    {27, "Bounding Box (Collision Geometry)"},
    {28, "Reflected By Auto Water"},
    {29, "Don't Havok Settle"},
    {30, "No Respawn"},
    {31, "Multibound"},
};

constexpr radicle_placed_ref_flag_row k_container_like[] = {
    {4, "Ground Piece"},
    {10, "Persistent"},
    {11, "Initially Disabled"},
    {16, "Is Full LOD"},
    {25, "No AI Acquire"},
    {26, "Filter (Collision Geometry)"},
    {27, "Bounding Box (Collision Geometry)"},
    {29, "Don't Havok Settle"},
    {30, "Ground"},
    {31, "Multibound"},
};

constexpr radicle_placed_ref_flag_row k_door[] = {
    {2, "Minimal Use Door"},
    {6, "Hidden From Local Map"},
    {8, "Inaccessible"},
    {10, "Persistent"},
    {11, "Initially Disabled"},
    {16, "Is Full LOD"},
    {26, "Filter (Collision Geometry)"},
    {27, "Bounding Box (Collision Geometry)"},
    {29, "Don't Havok Settle"},
    {30, "No Respawn"},
    {31, "Multibound"},
};

constexpr radicle_placed_ref_flag_row k_light[] = {
    {8, "Doesn't Light Water"},
    {9, "Casts Shadows"},
    {10, "Persistent"},
    {11, "Initially Disabled"},
    {16, "Never Fades"},
    {17, "Doesn't Light Landscape"},
    {25, "No AI Acquire"},
    {28, "Reflected By Auto Water"},
    {29, "Don't Havok Settle"},
    {30, "No Respawn"},
    {31, "Multibound"},
};

constexpr radicle_placed_ref_flag_row k_movable_static[] = {
    {4, "Ground Piece"},
    {9, "Motion Blur"},
    {10, "Persistent"},
    {11, "Initially Disabled"},
    {16, "Is Full LOD"},
    {26, "Filter (Collision Geometry)"},
    {27, "Bounding Box (Collision Geometry)"},
    {28, "Reflected By Auto Water"},
    {29, "Don't Havok Settle"},
    {30, "No Respawn"},
    {31, "Multibound"},
};

constexpr radicle_placed_ref_flag_row k_addon_node[] = {
    {10, "Persistent"},
    {11, "Initially Disabled"},
    {16, "Is Full LOD"},
    {28, "Reflected By Auto Water"},
    {29, "Don't Havok Settle"},
    {30, "No Respawn"},
    {31, "Multibound"},
};

constexpr radicle_placed_ref_flag_row k_item_like[] = {
    {4, "Ground Piece"},
    {10, "Persistent"},
    {11, "Initially Disabled"},
    {16, "Is Full LOD"},
    {25, "No AI Acquire"},
    {28, "Reflected By Auto Water"},
    {29, "Don't Havok Settle"},
    {30, "No Respawn"},
    {31, "Multibound"},
};

constexpr radicle_placed_ref_flag_row k_placed_npc[] = {
    {9, "Starts Dead"},
    {10, "Persistent"},
    {11, "Initially Disabled"},
    {13, "Starts Unconscious"},
    {25, "No AI Acquire"},
    {29, "Don't Havok Settle"},
};

constexpr radicle_placed_ref_flag_row k_placed_projectile[] = {
    {7, "Turn Off Fire"},
    {10, "Persistent"},
    {11, "Initially Disabled"},
    {28, "Reflected By Auto Water"},
    {29, "Don't Havok Settle"},
    {30, "No Respawn"},
};

bool is_one_of(std::string_view signature, std::initializer_list<std::string_view> group) {
  for (const auto candidate : group) {
    if (signature == candidate) return true;
  }
  return false;
}

// Constraint: the dispatch must match wbREFRRecordFlagsDecider (wbDefinitionsFO4.pas:2515-2561); a base in no group selects the default table.
Table table_for(std::string_view placed, std::string_view base) {
  if (placed == "ACHR") return table_of(k_placed_npc);
  if (is_one_of(placed, {"PARW", "PBAR", "PBEA", "PCON", "PFLA", "PGRE", "PHZD", "PMIS"})) return table_of(k_placed_projectile);
  if (is_one_of(base, {"ACTI", "STAT", "SCOL", "TREE"})) return table_of(k_static_like);
  if (is_one_of(base, {"CONT", "TERM"})) return table_of(k_container_like);
  if (base == "DOOR") return table_of(k_door);
  if (base == "LIGH") return table_of(k_light);
  if (base == "MSTT") return table_of(k_movable_static);
  if (base == "ADDN") return table_of(k_addon_node);
  if (is_one_of(base, {"ALCH", "BOOK", "SCRL", "AMMO", "ARMO", "INGR", "KEYM", "MISC", "FURN", "WEAP"})) {
    return table_of(k_item_like);
  }
  return table_of(k_default);
}

}  // namespace

extern "C" radicle_status radicle_placed_ref_flag_table(const char* placed_signature,
                                                        const char* base_signature,
                                                        const radicle_placed_ref_flag_row** out_rows,
                                                        std::size_t* out_count) {
  if (placed_signature == nullptr || out_rows == nullptr || out_count == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "placed_signature, out_rows and out_count are required");
  }
  const auto table = table_for(placed_signature, base_signature == nullptr ? std::string_view{} : base_signature);
  *out_rows = table.rows;
  *out_count = table.count;
  return RADICLE_STATUS_OK;
}
