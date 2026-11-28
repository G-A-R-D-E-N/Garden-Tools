#include "c_api_internal.hpp"
#include "fo4/c_api_cell.h"

#include <cstddef>
#include <string_view>

namespace {

using namespace radicle_c_detail;

// Constraint: rows and names are xEdit Core/wbDefinitionsFO4.pas @93cc0bc5 ACTI 5611-5633, DOOR 6477-6483, FURN 6848-6860, STAT 12045-12066; Deleted (5) and Ignored (12) are the rows wbFlagsList (Core/wbInterface.pas:5476-5495) adds to every list; rows must stay in ascending bit order.
constexpr radicle_placed_ref_flag_row k_common[] = {
    {5, "Deleted"},
    {12, "Ignored"},
};

constexpr radicle_placed_ref_flag_row k_acti[] = {
    {2, "Never Fades"},
    {4, "Non Occluder"},
    {5, "Deleted"},
    {6, "Unknown 6"},
    {7, "Heading Marker"},
    {8, "Must Update Anims"},
    {9, "Hidden From Local Map"},
    {10, "Headtrack Marker"},
    {11, "Used as Platform"},
    {12, "Ignored"},
    {13, "Pack-In Use Only"},
    {15, "Has Distant LOD"},
    {16, "Random Anim Start"},
    {17, "Dangerous"},
    {20, "Ignore Object Interaction"},
    {23, "Is Marker"},
    {25, "Obstacle"},
    {26, "Navmesh - Filter"},
    {27, "Navmesh - Bounding Box"},
    {29, "Child Can Use"},
    {30, "Navmesh - Ground"},
};

constexpr radicle_placed_ref_flag_row k_door[] = {
    {4, "Non Occluder"},
    {5, "Deleted"},
    {12, "Ignored"},
    {15, "Has Distant LOD"},
    {16, "Random Anim Start"},
    {23, "Is Marker"},
};

constexpr radicle_placed_ref_flag_row k_furn[] = {
    {2, "Has Container"},
    {4, "Unknown 4"},
    {5, "Deleted"},
    {7, "Is Perch"},
    {12, "Ignored"},
    {13, "Unknown 13"},
    {15, "Has Distant LOD"},
    {16, "Random Anim Start"},
    {23, "Is Marker"},
    {25, "Power Armor"},
    {28, "Must Exit To Talk"},
    {29, "Child Can Use"},
};

constexpr radicle_placed_ref_flag_row k_stat[] = {
    {2, "Heading Marker"},
    {4, "Non Occluder"},
    {5, "Deleted"},
    {6, "Has Tree LOD"},
    {7, "Add-On LOD Object"},
    {9, "Hidden From Local Map"},
    {10, "Headtrack Marker"},
    {11, "Used as Platform"},
    {12, "Ignored"},
    {13, "Pack-In Use Only"},
    {15, "Has Distant LOD"},
    {17, "Uses HD LOD Texture"},
    {19, "Has Currents"},
    {23, "Is Marker"},
    {25, "Obstacle"},
    {26, "NavMesh - Filter"},
    {27, "NavMesh - Bounding Box"},
    {28, "Show In World Map (Sky Cell Only)"},
    {30, "NavMesh - Ground"},
};

// Constraint: names are xEdit Core/wbDefinitionsFO4.pas @93cc0bc5 wbXESP lines 4330-4336; the bits must be read as positions in a U8, not record-header bits.
constexpr radicle_placed_ref_flag_row k_enable_parent[] = {
    {0, "Set Enable State to Opposite of Parent"},
    {1, "Pop In"},
};

template <std::size_t N>
void give(const radicle_placed_ref_flag_row (&rows)[N], const radicle_placed_ref_flag_row** out_rows, std::size_t* out_count) {
  *out_rows = rows;
  *out_count = N;
}

}  // namespace

extern "C" radicle_status radicle_record_flag_table(const char* signature,
                                                    const radicle_placed_ref_flag_row** out_rows,
                                                    std::size_t* out_count) {
  if (signature == nullptr || out_rows == nullptr || out_count == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "signature, out_rows and out_count are required");
  }
  const std::string_view sig = signature;
  if (sig == "ACTI") give(k_acti, out_rows, out_count);
  else if (sig == "DOOR") give(k_door, out_rows, out_count);
  else if (sig == "FURN") give(k_furn, out_rows, out_count);
  else if (sig == "STAT") give(k_stat, out_rows, out_count);
  else give(k_common, out_rows, out_count);
  return RADICLE_STATUS_OK;
}

extern "C" radicle_status radicle_enable_parent_flag_table(const radicle_placed_ref_flag_row** out_rows,
                                                           std::size_t* out_count) {
  if (out_rows == nullptr || out_count == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_rows and out_count are required");
  }
  give(k_enable_parent, out_rows, out_count);
  return RADICLE_STATUS_OK;
}
