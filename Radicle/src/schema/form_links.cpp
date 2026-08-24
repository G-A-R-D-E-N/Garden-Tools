#include "fo4/schema/form_links.hpp"

#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/schema/vmad.hpp"
#include "fo4/signature.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <map>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace fo4 {
namespace {

struct CtdaFunction {
  std::uint16_t index;
  std::uint8_t flags;
};

constexpr CtdaFunction k_ctda_functions[] = {
#include "fo4/schema/ctda_functions.inc"
};

constexpr std::uint32_t k_formid_subrecords[] = {
#include "fo4/schema/formid_subrecords.inc"
};

struct StructuredFormIdSlot {
  std::uint32_t signature;
  std::uint16_t offset;
  std::uint16_t min_size;
};

constexpr StructuredFormIdSlot k_structured_formids[] = {
#include "fo4/schema/structured_formids.inc"
};

constexpr std::uint8_t k_param1_formid = 1;
constexpr std::uint8_t k_param2_formid = 2;
constexpr std::uint8_t k_ctda_use_global = 0x04;
constexpr std::size_t k_ctda_type = 0;
constexpr std::size_t k_ctda_compare = 4;
constexpr std::size_t k_ctda_function = 8;
constexpr std::size_t k_ctda_param1 = 12;
constexpr std::size_t k_ctda_param2 = 16;
constexpr std::size_t k_ctda_run_on = 20;
constexpr std::size_t k_ctda_reference = 24;
constexpr std::uint32_t k_run_on_reference = 2;

constexpr auto k_navm = Signature::from_string("NAVM");
constexpr auto k_navi = Signature::from_string("NAVI");
constexpr auto k_nvnm = Signature::from_string("NVNM");
constexpr auto k_nvmi = Signature::from_string("NVMI");
constexpr auto k_nvpp = Signature::from_string("NVPP");
constexpr auto k_nvsi = Signature::from_string("NVSI");
constexpr auto k_mnam = Signature::from_string("MNAM");
constexpr auto k_proj = Signature::from_string("PROJ");
constexpr auto k_expl = Signature::from_string("EXPL");
constexpr auto k_hazd = Signature::from_string("HAZD");
constexpr auto k_qust = Signature::from_string("QUST");
constexpr auto k_race = Signature::from_string("RACE");
constexpr auto k_obts = Signature::from_string("OBTS");
constexpr auto k_sraf = Signature::from_string("SRAF");
constexpr auto k_alco = Signature::from_string("ALCO");
constexpr auto k_alfr = Signature::from_string("ALFR");
constexpr auto k_anam = Signature::from_string("ANAM");
constexpr auto k_cnam = Signature::from_string("CNAM");
constexpr auto k_dnam = Signature::from_string("DNAM");
constexpr auto k_fnam = Signature::from_string("FNAM");
constexpr auto k_nam2 = Signature::from_string("NAM2");
constexpr auto k_nnam = Signature::from_string("NNAM");
constexpr auto k_snam = Signature::from_string("SNAM");

std::uint16_t read_u16(std::span<const std::byte> data, std::size_t offset) {
  std::uint16_t value = 0;
  std::memcpy(&value, data.data() + offset, 2);
  return value;
}

std::uint32_t read_u32(std::span<const std::byte> data, std::size_t offset) {
  std::uint32_t value = 0;
  std::memcpy(&value, data.data() + offset, 4);
  return value;
}

void write_u32(std::span<std::byte> data, std::size_t offset, std::uint32_t value) {
  std::memcpy(data.data() + offset, &value, 4);
}

const CtdaFunction* find_ctda_function(std::uint16_t index) {
  const auto begin = std::begin(k_ctda_functions);
  const auto end = std::end(k_ctda_functions);
  const auto it = std::lower_bound(begin, end, index,
                                   [](const CtdaFunction& row, std::uint16_t value) {
                                     return row.index < value;
                                   });
  if (it == end || it->index != index) {
    return nullptr;
  }
  return &*it;
}

bool is_listed_formid_signature(Signature signature) {
  const auto packed = signature.packed();
  const auto begin = std::begin(k_formid_subrecords);
  const auto end = std::end(k_formid_subrecords);
  return std::find(begin, end, packed) != end;
}

bool is_formid_array_signature(Signature signature) {
  return signature == sig_kwda || signature == Signature::from_string("SPLO") ||
         signature == sig_xlkr;
}

bool record_specific_formid_array(const Record& record, const Subrecord& sub) {
  // xEdit FO4 NAVM: ONAM is Base Objects, an array of FormIDs. PNAM and NNAM
  // beside it are arrays of u16 vertex indexes and must not be scanned as IDs.
  return record.header.signature == k_navm && sub.signature == sig_onam;
}

bool qust_non_formid_signature(Signature signature) {
  // xEdit FO4 QUST reuses several signatures that are FormIDs in other record
  // types. Within QUST they are scalar/string data and must never be sent
  // through the global signature-only FormID table:
  //   DNAM General quest data
  //   NAM2 stage note
  //   CNAM stage log-entry text
  //   FNAM objective/alias flags
  //   ANAM next alias ID
  //   NNAM objective text / quest description
  //   SNAM SWF filename
  return signature == k_dnam || signature == k_nam2 || signature == k_cnam ||
         signature == k_fnam || signature == k_anam || signature == k_nnam ||
         signature == k_snam;
}

bool consume_counted_fixed(std::span<const std::byte> data,
                           std::size_t& cursor,
                           std::size_t element_size,
                           auto&& visit_element) {
  if (cursor > data.size() || data.size() - cursor < 4 || element_size == 0) {
    return false;
  }
  const auto count = static_cast<std::size_t>(read_u32(data, cursor));
  cursor += 4;
  if (count > (data.size() - cursor) / element_size) return false;
  for (std::size_t index = 0; index < count; ++index) {
    visit_element(cursor + index * element_size);
  }
  cursor += count * element_size;
  return true;
}

std::optional<std::vector<std::size_t>> nvnm_formid_offsets(
    std::span<const std::byte> data) {
  // xEdit FO4 wbNVNM (pinned 93cc0bc5):
  //   u32 version
  //   Pathing Cell { u32 CRC, FormID Parent World, union[coords|Parent Cell] }
  //   u32-counted vertices[12], triangles[21], edge-links[11], door-links[10],
  //   cover[8], cover-triangle-map[4], waypoints[18]
  //   Navmesh Grid { u32 divisor, vec2 grid-size, 2*vec3 bounds,
  //                  divisor^2 cells, each a u32-counted u16 triangle array }
  if (data.size() < 16) return std::nullopt;

  std::vector<std::size_t> offsets;
  offsets.push_back(8);  // Parent World
  if (read_u32(data, 8) == 0) offsets.push_back(12);  // Parent Cell union arm

  std::size_t cursor = 16;
  const auto skip = [](std::size_t) {};
  if (!consume_counted_fixed(data, cursor, 12, skip)) return std::nullopt;
  if (!consume_counted_fixed(data, cursor, 21, skip)) return std::nullopt;
  if (!consume_counted_fixed(data, cursor, 11, [&](std::size_t base) {
        offsets.push_back(base + 4);  // Edge Link.Navmesh
      })) {
    return std::nullopt;
  }
  if (!consume_counted_fixed(data, cursor, 10, [&](std::size_t base) {
        offsets.push_back(base + 6);  // Door Link.Door Ref
      })) {
    return std::nullopt;
  }
  if (!consume_counted_fixed(data, cursor, 8, skip)) return std::nullopt;
  if (!consume_counted_fixed(data, cursor, 4, skip)) return std::nullopt;
  if (!consume_counted_fixed(data, cursor, 18, skip)) return std::nullopt;

  // Grid header: divisor + vec2 + min/max vec3 = 36 bytes.
  if (cursor > data.size() || data.size() - cursor < 36) return std::nullopt;
  const auto divisor = static_cast<std::size_t>(read_u32(data, cursor));
  if (divisor > 12) return std::nullopt;  // mirrors wbNavmeshGridCounter
  cursor += 36;

  const auto cell_count = divisor * divisor;
  for (std::size_t cell = 0; cell < cell_count; ++cell) {
    if (!consume_counted_fixed(data, cursor, 2, skip)) return std::nullopt;
  }
  if (cursor != data.size()) return std::nullopt;
  return offsets;
}

std::optional<std::vector<std::size_t>> nvmi_formid_offsets(
    std::span<const std::byte> data) {
  // xEdit FO4 NVMI:
  //   FormID navmesh, u32 flags, vec3 approx-location, f32 preferred
  //   u32-counted edge navmeshes, preferred edge navmeshes,
  //   u32-counted door {u32 CRC, FormID ref},
  //   u8 has-island + optional bounds/triangle/vertex arrays,
  //   Pathing Cell {u32 CRC, FormID Parent World, union[coords|Parent Cell]}.
  if (data.size() < 24) return std::nullopt;

  std::vector<std::size_t> offsets{0};  // Navmesh
  std::size_t cursor = 24;
  if (!consume_counted_fixed(data, cursor, 4, [&](std::size_t base) {
        offsets.push_back(base);
      })) {
    return std::nullopt;
  }
  if (!consume_counted_fixed(data, cursor, 4, [&](std::size_t base) {
        offsets.push_back(base);
      })) {
    return std::nullopt;
  }
  if (!consume_counted_fixed(data, cursor, 8, [&](std::size_t base) {
        offsets.push_back(base + 4);  // Door Ref after CRC
      })) {
    return std::nullopt;
  }

  if (cursor >= data.size()) return std::nullopt;
  const auto has_island = std::to_integer<std::uint8_t>(data[cursor++]);
  if (has_island > 1) return std::nullopt;
  if (has_island != 0) {
    if (cursor > data.size() || data.size() - cursor < 24) return std::nullopt;
    cursor += 24;  // bounds
    const auto skip = [](std::size_t) {};
    if (!consume_counted_fixed(data, cursor, 6, skip)) return std::nullopt;
    if (!consume_counted_fixed(data, cursor, 12, skip)) return std::nullopt;
  }

  if (cursor > data.size() || data.size() - cursor != 12) return std::nullopt;
  offsets.push_back(cursor + 4);  // Parent World
  if (read_u32(data, cursor + 4) == 0) offsets.push_back(cursor + 8);  // Parent Cell
  cursor += 12;
  if (cursor != data.size()) return std::nullopt;
  return offsets;
}

std::optional<std::vector<std::size_t>> nvpp_formid_offsets(
    std::span<const std::byte> data) {
  // xEdit FO4 NVPP is two u32-counted structures:
  //   Paths: u32 path-count, then each path is u32 count + FormID[NAVM][]
  //   Road Marker Index: u32 count + {FormID NAVM, u32 index}[]
  std::vector<std::size_t> offsets;
  if (data.size() < 4) return std::nullopt;

  std::size_t cursor = 0;
  const auto path_count = static_cast<std::size_t>(read_u32(data, cursor));
  cursor += 4;
  // Every path consumes at least its four-byte nested count.
  if (path_count > (data.size() - cursor) / 4) return std::nullopt;
  for (std::size_t path = 0; path < path_count; ++path) {
    if (!consume_counted_fixed(data, cursor, 4, [&](std::size_t base) {
          offsets.push_back(base);
        })) {
      return std::nullopt;
    }
  }

  if (!consume_counted_fixed(data, cursor, 8, [&](std::size_t base) {
        offsets.push_back(base);  // Road Marker.Navmesh
      })) {
    return std::nullopt;
  }
  if (cursor != data.size()) return std::nullopt;
  return offsets;
}

std::optional<std::vector<std::size_t>> mnam_formid_offsets(
    std::span<const std::byte> data) {
  // xEdit FO4 NAVM.MNAM PreCut Map Entries: the outer array consumes entries
  // until the subrecord ends. Each entry is a FormID Reference followed by a
  // -2 array, i.e. a u16 count and that many u16 triangle indexes.
  std::vector<std::size_t> offsets;
  std::size_t cursor = 0;
  while (cursor < data.size()) {
    if (data.size() - cursor < 6) return std::nullopt;
    offsets.push_back(cursor);  // Reference
    cursor += 4;
    const auto triangle_count = static_cast<std::size_t>(read_u16(data, cursor));
    cursor += 2;
    if (triangle_count > (data.size() - cursor) / 2) return std::nullopt;
    cursor += triangle_count * 2;
  }
  return offsets;
}

std::optional<std::vector<std::size_t>> navmesh_mixed_formid_offsets(
    const Record& record, const Subrecord& sub) {
  if (record.header.signature == k_navm && sub.signature == k_nvnm) {
    return nvnm_formid_offsets(sub.data);
  }
  if (record.header.signature == k_navm && sub.signature == k_mnam) {
    return mnam_formid_offsets(sub.data);
  }
  if (record.header.signature == k_navi && sub.signature == k_nvmi) {
    return nvmi_formid_offsets(sub.data);
  }
  if (record.header.signature == k_navi && sub.signature == k_nvpp) {
    return nvpp_formid_offsets(sub.data);
  }
  if (record.header.signature == k_navi && sub.signature == k_nvsi) {
    if (sub.data.size() % 4 != 0) return std::nullopt;
    std::vector<std::size_t> offsets;
    offsets.reserve(sub.data.size() / 4);
    for (std::size_t offset = 0; offset < sub.data.size(); offset += 4) {
      offsets.push_back(offset);
    }
    return offsets;
  }
  return std::vector<std::size_t>{};
}

bool is_navmesh_mixed_payload(const Record& record, const Subrecord& sub) {
  return (record.header.signature == k_navm &&
          (sub.signature == k_nvnm || sub.signature == k_mnam)) ||
         (record.header.signature == k_navi &&
          (sub.signature == k_nvmi || sub.signature == k_nvpp || sub.signature == k_nvsi));
}

bool perk_epfd_is_formid(const Record& record, const Subrecord& sub) {
  if (record.header.signature != Signature::from_string("PERK") ||
      sub.signature != Signature::from_string("EPFD") || sub.data.size() != 4) {
    return false;
  }

  int function_type = -1;
  for (const auto& candidate : record.subrecords) {
    if (&candidate == &sub) {
      break;
    }
    if (candidate.signature == Signature::from_string("EPFT") && !candidate.data.empty()) {
      function_type = static_cast<int>(std::to_integer<std::uint8_t>(candidate.data.front()));
    }
  }

  // xEdit FO4 wbPERK: EPFT 3 = LVLI, 4 = Add Activate Choice/SPEL,
  // 5 = Select Spell/SPEL. Other EPFD variants are float/text/non-FormID.
  return function_type == 3 || function_type == 4 || function_type == 5;
}

bool visit_tagged_formid_slot(const Subrecord& sub, auto&& fn) {
  if (sub.data.size() < 8) {
    return false;
  }

  const auto type = read_u32(sub.data, 0);
  if (sub.signature == Signature::from_string("PTDT")) {
    if (type == 0 || type == 1) {
      fn(4);
    }
    return true;
  }

  if (sub.signature == Signature::from_string("PDTO")) {
    if (type == 0) {
      fn(4);
    }
    return true;
  }

  return false;
}

bool visit_structured_slots(Signature signature, std::size_t size, auto&& fn) {
  const auto packed = signature.packed();
  bool matched = false;
  for (const auto& slot : k_structured_formids) {
    if (slot.signature == packed && size >= slot.min_size) {
      fn(static_cast<std::size_t>(slot.offset));
      matched = true;
    }
  }
  return matched;
}

// FormID slots inside the PROJ/EXPL/HAZD runtime-struct payloads.
//
// DATA and DNAM mean different things in different records, so these are keyed
// on the record signature and can never go in the signature-only table.
//
// The field lists come from CommonLibF4's BGSProjectileData /
// BGSExplosionData / BGSHazardData, but the *disk* offsets are measured, not
// derived from those structs. Two places where the file disagrees with memory:
//
//   * PROJ DNAM is 93 bytes and ends [+88 u8 tracerFrequency]
//     [+89 u32 vatsProjectile]. The FormID is UNALIGNED and it is the last
//     field, the reverse of the in-memory order. An earlier note here claimed
//     +88 was tracerFrequency followed by heap padding; the files say
//     otherwise -- 25 vanilla and 453 corpus records carry a live PROJ FormID
//     at +89 that a master remap was silently corrupting.
//   * EXPL DATA serializes in a different field ORDER than BGSExplosionData
//     holds in memory: the spawn-projectile vector, spread and count are the
//     84-byte tail, not the middle. Only +20 spawnProjectile is a FormID
//     beyond the leading five.
//
// See docs/FORMAT_NOTES.md for the full measured field tables. Every slot
// below resolves to its stated record type in all 659 shipped PROJ/EXPL/HAZD
// records and all 2,109 corpus records, with zero contradictions.
std::optional<std::vector<std::size_t>> struct_payload_formid_offsets(const Record& record,
                                                                     const Subrecord& sub) {
  const auto signature = record.header.signature;
  const auto size = sub.data.size();
  // EXPL DATA also ships truncated at 52/56/60/64 bytes, so gate each slot on
  // its own extent rather than assuming the canonical 84.
  auto within = [&](std::initializer_list<std::size_t> slots) {
    std::vector<std::size_t> offsets;
    for (const auto offset : slots) {
      if (offset + 4 <= size) offsets.push_back(offset);
    }
    return offsets;
  };

  if (signature == k_expl && sub.signature == sig_data) {
    // LIGH SNDR SNDR IPDS placed-object PROJ
    return within({0, 4, 8, 12, 16, 20});
  }
  if (signature == k_proj && sub.signature == k_dnam) {
    // LIGH LIGH EXPL SNDR SNDR SNDR WEAP TXST COLL PROJ(unaligned, +89)
    return within({16, 20, 32, 36, 52, 56, 60, 80, 84, 89});
  }
  if (signature == k_hazd && sub.signature == k_dnam) {
    // SPEL/ENCH LIGH IPDS SNDR
    return within({24, 28, 32, 36});
  }
  return std::nullopt;
}

// Returns every FormID offset inside an OBTS body, or nullopt when the counts
// do not describe the payload exactly -- which is the only safe signal that
// the shape is not what we think it is.
std::optional<std::vector<std::size_t>> obts_formid_offsets(const Subrecord& sub) {
  constexpr std::size_t k_header = 0x12;
  if (sub.data.size() < k_header) return std::nullopt;

  const auto includes = read_u32(sub.data, 0x00);
  const auto properties = read_u32(sub.data, 0x04);
  const auto keywords = std::to_integer<std::uint8_t>(sub.data[0x0F]);

  const auto expected = k_header + std::size_t{keywords} * 4 +
                        std::size_t{includes} * 7 + std::size_t{properties} * 24;
  if (expected != sub.data.size()) return std::nullopt;

  std::vector<std::size_t> offsets;
  offsets.reserve(std::size_t{keywords} + includes);
  for (std::uint8_t i = 0; i < keywords; ++i) offsets.push_back(0x10 + std::size_t{i} * 4);
  const auto include_base = k_header + std::size_t{keywords} * 4;
  for (std::uint32_t i = 0; i < includes; ++i) {
    offsets.push_back(include_base + std::size_t{i} * 7);
  }
  return offsets;
}

void visit_link_offsets(const Record& record, const Subrecord& sub,
                        bool strict_remap, auto&& fn) {
  if (sub.signature == sig_xxxx || sub.data.empty()) {
    return;
  }
  // xEdit FO4 uses SRAF in two incompatible ways: INFO response Camera Path
  // is a FormID, while RACE subgraph SRAF is {u16 role, u16 perspective} and
  // terminates one subgraph. Never interpret the RACE flags as a FormID.
  if (record.header.signature == k_race && sub.signature == k_sraf) {
    return;
  }
  // QUST has the same signature-reuse problem on a larger scale. Classify the
  // proven QUST-only meanings before consulting the global signature table.
  // ALFR Specific Reference and ALCO Create Reference Object are FormLinks but
  // are not present in the generated wbFormIDCk signature list. The listed
  // signatures below are scalar/string data in QUST even though they are
  // FormIDs elsewhere.
  if (record.header.signature == k_qust) {
    if (sub.signature == k_alfr || sub.signature == k_alco) {
      if (sub.data.size() == 4) fn(0);
      return;
    }
    if (qust_non_formid_signature(sub.signature)) {
      return;
    }
  }
  // OBTS is the BGSMod::Template body: a variable-length header whose keyword
  // and include FormIDs sit at offsets derived from two counts, so it cannot
  // be expressed as a fixed structured slot.
  //
  //   0x00 u32 include count I      0x04 u32 property count P
  //   0x0F u8  keyword count K      0x10 K*4 keyword FormIDs
  //   0x10+4K  u8 min level, u8 alt levels
  //   0x12+4K  I*7  include rows { u32 OMOD FormID, u8, u8, u8 }
  //            P*24 property rows
  //
  // Layout validated on all 3,297 shipped OBTS bodies (ARMO, WEAP, NPC_ and
  // FURN): the computed size matches the payload exactly, zero mismatches,
  // and all 7,342 Fallout4.esm include FormIDs resolve to OMOD records.
  if (const auto offsets = struct_payload_formid_offsets(record, sub)) {
    for (const auto offset : *offsets) fn(offset);
    return;
  }
  if (sub.signature == k_obts) {
    const auto offsets = obts_formid_offsets(sub);
    if (!offsets.has_value()) {
      if (strict_remap) {
        throw FormIdError("cannot safely remap malformed OBTS template payload");
      }
      return;
    }
    for (const auto offset : *offsets) fn(offset);
    return;
  }
  if (is_navmesh_mixed_payload(record, sub)) {
    const auto offsets = navmesh_mixed_formid_offsets(record, sub);
    if (!offsets.has_value()) {
      if (strict_remap) {
        throw FormIdError(
            "cannot safely remap malformed NAVM/NAVI mixed FormID payload " +
            sub.signature.to_string());
      }
      return;
    }
    for (const auto offset : *offsets) fn(offset);
    return;
  }
  if (record.header.signature == sig_tes4 && sub.signature == sig_onam &&
      sub.data.size() % 4 == 0) {
    for (std::size_t i = 0; i < sub.data.size(); i += 4) {
      fn(i);
    }
    return;
  }
  if (record_specific_formid_array(record, sub) && sub.data.size() % 4 == 0) {
    for (std::size_t i = 0; i < sub.data.size(); i += 4) {
      fn(i);
    }
    return;
  }
  if (perk_epfd_is_formid(record, sub)) {
    fn(0);
    return;
  }
  if (is_formid_array_signature(sub.signature) && sub.data.size() % 4 == 0) {
    for (std::size_t i = 0; i < sub.data.size(); i += 4) {
      fn(i);
    }
    return;
  }
  if (is_listed_formid_signature(sub.signature) && sub.data.size() == 4) {
    fn(0);
    return;
  }
  if (visit_tagged_formid_slot(sub, fn)) {
    return;
  }
  if (visit_structured_slots(sub.signature, sub.data.size(), fn)) {
    return;
  }
  if (sub.signature == sig_vmad) {
    if (strict_remap) {
      auto offsets = vmad_formid_offsets_for_remap(sub.data, record.header.signature);
      if (!offsets.has_value()) {
        throw FormIdError(
            "cannot safely remap VMAD containing unsupported Variable/Variable Array property types");
      }
      for (const auto offset : *offsets) {
        fn(offset);
      }
    } else {
      for (const auto offset : vmad_formid_offsets(sub.data, record.header.signature)) {
        fn(offset);
      }
    }
    return;
  }
  if (sub.signature != sig_ctda || sub.data.size() < 16) {
    return;
  }
  if ((std::to_integer<std::uint8_t>(sub.data[k_ctda_type]) & k_ctda_use_global) != 0) {
    fn(k_ctda_compare);
  }
  const auto function = read_u16(sub.data, k_ctda_function);
  const auto* desc = find_ctda_function(function);
  if (desc != nullptr) {
    if ((desc->flags & k_param1_formid) != 0) {
      fn(k_ctda_param1);
    }
    if ((desc->flags & k_param2_formid) != 0 && sub.data.size() >= 20) {
      fn(k_ctda_param2);
    }
  }
  if (sub.data.size() >= 28 && read_u32(sub.data, k_ctda_run_on) == k_run_on_reference) {
    fn(k_ctda_reference);
  }
}

void remap_raw(std::span<std::byte> data, std::size_t offset,
               const PluginFormIdContext& old_context,
               const PluginFormIdContext& new_context) {
  if (offset + 4 > data.size()) {
    return;
  }
  const auto raw = RawFormId{read_u32(data, offset)};
  if (raw.is_null()) {
    return;
  }
  const auto key = resolve_plugin_form_id(raw, old_context);
  const auto encoded = encode_plugin_form_id(key, new_context);
  if (encoded != raw) {
    write_u32(data, offset, encoded.value());
  }
}
}  // namespace

void remap_record_form_links(Record& record,
                             const PluginFormIdContext& old_context,
                             const PluginFormIdContext& new_context) {
  for (auto& sub : record.subrecords) {
    auto bytes = std::span<std::byte>(sub.data);
    visit_link_offsets(record, sub, true, [&](std::size_t offset) {
      remap_raw(bytes, offset, old_context, new_context);
    });
  }
}

void remap_form_links(Plugin& plugin, const PluginFormIdContext& old_context,
                      const PluginFormIdContext& new_context) {
  visit_records(plugin, [&](Record& record) {
    remap_record_form_links(record, old_context, new_context);
  });
}

void rekey_form_links(Plugin& plugin,
                      const FormKey& old_key,
                      const FormKey& new_key) {
  auto next = plugin;
  const auto context = plugin_form_id_context(next);
  bool any_changed = false;

  visit_records(next, [&](Record& record) {
    bool changed = false;
    for (auto& sub : record.subrecords) {
      auto bytes = std::span<std::byte>(sub.data);
      visit_link_offsets(record, sub, true, [&](std::size_t offset) {
        if (offset + 4 > bytes.size()) return;
        const auto raw = RawFormId{read_u32(bytes, offset)};
        if (raw.is_null()) return;
        if (resolve_plugin_form_id(raw, context) != old_key) return;
        const auto replacement = encode_plugin_form_id(new_key, context);
        if (replacement == raw) return;
        write_u32(bytes, offset, replacement.value());
        changed = true;
      });
    }
    if (changed) {
      record.original_range.clear();
      record.decompressed_payload.clear();
      record.header.data_size = 0;
      record.uncompressed_size = 0;
      any_changed = true;
    }
  });

  if (any_changed) {
    next.original_range.clear();
  }
  plugin = std::move(next);
}

std::vector<FormLinkHit> collect_form_links(const Plugin& plugin) {
  return collect_form_links_detailed(plugin).links;
}

FormLinkCollection collect_form_links_detailed(const Plugin& plugin) {
  FormLinkCollection result;
  auto& hits = result.links;
  bool current_record_unsupported = false;
  const auto context = plugin_form_id_context(plugin);
  visit_records(plugin, [&](const Record& record) {
    current_record_unsupported = false;
    std::map<std::uint32_t, std::size_t> ordinals;
    for (const auto& sub : record.subrecords) {
      if (sub.signature == sig_vmad &&
          vmad_has_unsupported_properties(sub.data, record.header.signature)) {
        current_record_unsupported = true;
      }
      const auto ordinal = ordinals[sub.signature.packed()]++;
      visit_link_offsets(record, sub, false, [&](std::size_t offset) {
        if (offset + 4 > sub.data.size()) {
          return;
        }
        FormLinkHit hit;
        hit.record = record.form_key;
        hit.subrecord = sub.signature;
        hit.subrecord_ordinal = ordinal;
        hit.offset = offset;
        hit.raw = RawFormId{read_u32(sub.data, offset)};
        if (!hit.raw.is_null()) {
          try {
            hit.key = resolve_plugin_form_id(hit.raw, context);
          } catch (const FormIdError&) {
          }
        }
        hits.push_back(std::move(hit));
      });
    }
    if (current_record_unsupported) {
      ++result.records_with_unsupported_vmad;
    }
  });
  return result;
}

}  // namespace fo4
