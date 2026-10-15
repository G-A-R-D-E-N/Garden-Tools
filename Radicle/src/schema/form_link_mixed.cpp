#include "fo4/schema/form_link_mixed.hpp"
#include "fo4/schema/behavior_layout.hpp"
#include "fo4/schema/form_link_records.hpp"

#include "fo4/forms/proj_expl_hazd.hpp"
#include "fo4/formid.hpp"
#include "fo4/signature.hpp"

#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <span>

namespace fo4 {

constexpr auto k_navm = Signature::from_string("NAVM");
constexpr auto k_navi = Signature::from_string("NAVI");
constexpr auto k_nvnm = Signature::from_string("NVNM");
constexpr auto k_nvmi = Signature::from_string("NVMI");
constexpr auto k_nvpp = Signature::from_string("NVPP");
constexpr auto k_nvsi = Signature::from_string("NVSI");
constexpr auto k_mnam = Signature::from_string("MNAM");
constexpr auto k_dnam = Signature::from_string("DNAM");
constexpr auto k_proj = Signature::from_string("PROJ");
constexpr auto k_expl = Signature::from_string("EXPL");
constexpr auto k_hazd = Signature::from_string("HAZD");
constexpr auto k_fnam = Signature::from_string("FNAM");
constexpr auto k_pnam = Signature::from_string("PNAM");
constexpr auto k_tnam = Signature::from_string("TNAM");
constexpr auto k_omod = Signature::from_string("OMOD");
constexpr auto k_equp = Signature::from_string("EQUP");
constexpr auto k_musc = Signature::from_string("MUSC");
constexpr auto k_fstp = Signature::from_string("FSTP");
constexpr auto k_fsts = Signature::from_string("FSTS");
constexpr auto k_data = Signature::from_string("DATA");
constexpr auto k_epft = Signature::from_string("EPFT");
constexpr auto k_epfd = Signature::from_string("EPFD");
constexpr auto k_prke = Signature::from_string("PRKE");
constexpr auto k_plvd = Signature::from_string("PLVD");
constexpr auto k_otft = Signature::from_string("OTFT");
constexpr auto k_inam = Signature::from_string("INAM");
constexpr auto k_coll = Signature::from_string("COLL");
constexpr auto k_cnam = Signature::from_string("CNAM");
constexpr auto k_race = Signature::from_string("RACE");
constexpr auto k_hclf = Signature::from_string("HCLF");
constexpr auto k_cell = Signature::from_string("CELL");
constexpr auto k_xclr = Signature::from_string("XCLR");
constexpr auto k_cobj = Signature::from_string("COBJ");
constexpr auto k_arma = Signature::from_string("ARMA");
constexpr auto k_armo = Signature::from_string("ARMO");
constexpr auto k_modl = Signature::from_string("MODL");
constexpr auto k_npc_ = Signature::from_string("NPC_");
constexpr auto k_tpta = Signature::from_string("TPTA");
constexpr auto k_ovis = Signature::from_string("OVIS");
constexpr auto k_indx = Signature::from_string("INDX");
constexpr auto k_book = Signature::from_string("BOOK");
constexpr auto k_mgef = Signature::from_string("MGEF");

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
  if (data.size() < 16) return std::nullopt;

  std::vector<std::size_t> offsets;
  offsets.push_back(8);
  if (read_u32(data, 8) == 0) offsets.push_back(12);

  std::size_t cursor = 16;
  const auto skip = [](std::size_t) {};
  if (!consume_counted_fixed(data, cursor, 12, skip)) return std::nullopt;
  if (!consume_counted_fixed(data, cursor, 21, skip)) return std::nullopt;
  if (!consume_counted_fixed(data, cursor, 11, [&](std::size_t base) {
        offsets.push_back(base + 4);
      })) {
    return std::nullopt;
  }
  if (!consume_counted_fixed(data, cursor, 10, [&](std::size_t base) {
        offsets.push_back(base + 6);
      })) {
    return std::nullopt;
  }
  if (!consume_counted_fixed(data, cursor, 8, skip)) return std::nullopt;
  if (!consume_counted_fixed(data, cursor, 4, skip)) return std::nullopt;
  if (!consume_counted_fixed(data, cursor, 18, skip)) return std::nullopt;

  if (cursor > data.size() || data.size() - cursor < 36) return std::nullopt;
  const auto divisor = static_cast<std::size_t>(read_u32(data, cursor));
  if (divisor > 12) return std::nullopt;
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
  if (data.size() < 24) return std::nullopt;

  std::vector<std::size_t> offsets{0};
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
        offsets.push_back(base + 4);
      })) {
    return std::nullopt;
  }

  if (cursor >= data.size()) return std::nullopt;
  const auto has_island = std::to_integer<std::uint8_t>(data[cursor++]);
  if (has_island > 1) return std::nullopt;
  if (has_island != 0) {
    if (cursor > data.size() || data.size() - cursor < 24) return std::nullopt;
    cursor += 24;
    const auto skip = [](std::size_t) {};
    if (!consume_counted_fixed(data, cursor, 6, skip)) return std::nullopt;
    if (!consume_counted_fixed(data, cursor, 12, skip)) return std::nullopt;
  }

  if (cursor > data.size() || data.size() - cursor != 12) return std::nullopt;
  offsets.push_back(cursor + 4);
  if (read_u32(data, cursor + 4) == 0) offsets.push_back(cursor + 8);
  cursor += 12;
  if (cursor != data.size()) return std::nullopt;
  return offsets;
}

std::optional<std::vector<std::size_t>> nvpp_formid_offsets(
    std::span<const std::byte> data) {
  std::vector<std::size_t> offsets;
  if (data.size() < 4) return std::nullopt;

  std::size_t cursor = 0;
  const auto path_count = static_cast<std::size_t>(read_u32(data, cursor));
  cursor += 4;
  if (path_count > (data.size() - cursor) / 4) return std::nullopt;
  for (std::size_t path = 0; path < path_count; ++path) {
    if (!consume_counted_fixed(data, cursor, 4, [&](std::size_t base) {
          offsets.push_back(base);
        })) {
      return std::nullopt;
    }
  }

  if (!consume_counted_fixed(data, cursor, 8, [&](std::size_t base) {
        offsets.push_back(base);
      })) {
    return std::nullopt;
  }
  if (cursor != data.size()) return std::nullopt;
  return offsets;
}

std::optional<std::vector<std::size_t>> mnam_formid_offsets(
    std::span<const std::byte> data) {
  std::vector<std::size_t> offsets;
  std::size_t cursor = 0;
  while (cursor < data.size()) {
    if (data.size() - cursor < 6) return std::nullopt;
    offsets.push_back(cursor);
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
    if (record.header.form_version < 128 || sub.data.size() < 4 || read_u32(sub.data, 0) != 15) {
      return std::nullopt;
    }
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

std::optional<std::vector<std::size_t>> perk_formid_offsets(const Record& record,
                                                           const Subrecord& sub) {
  if (record.header.signature != sig_perk) return std::nullopt;
  if (sub.signature != k_epfd && sub.signature != sig_data) return std::nullopt;

  int function_type = -1;
  const Subrecord* prke = nullptr;
  const Subrecord* effect_data = nullptr;
  for (const auto& candidate : record.subrecords) {
    if (candidate.signature == k_epft && !candidate.data.empty()) {
      function_type = std::to_integer<int>(candidate.data.front());
    } else if (candidate.signature == k_prke) {
      prke = &candidate;
      effect_data = nullptr;
      function_type = -1;
    } else if (candidate.signature == sig_data && prke != nullptr && effect_data == nullptr) {
      effect_data = &candidate;
    }
    if (&candidate == &sub) break;
  }
  const int effect_type = prke != nullptr && !prke->data.empty()
                              ? std::to_integer<int>(prke->data.front())
                              : -1;
  const auto size = sub.data.size();

  if (sub.signature == sig_data) {
    const bool quest_stage = effect_type == 0 && size == 6;
    const bool ability = effect_type == 1 && size == 4;
    if (effect_data != &sub || !(quest_stage || ability)) return std::nullopt;
    return std::vector<std::size_t>{0};
  }

  const auto entry_function = effect_type == 2 && effect_data != nullptr &&
                                      effect_data->data.size() >= 2
                                  ? std::to_integer<int>(effect_data->data[1])
                                  : -1;
  const bool actor_value_entry = entry_function == 5 || entry_function == 12 ||
                                 entry_function == 13 || entry_function == 14;
  const bool actor_value_formid =
      size >= 8 && (function_type == 8 || (function_type == 2 && actor_value_entry));
  const bool epft_formid =
      size == 4 && (function_type == 3 || function_type == 4 || function_type == 5);
  if (!actor_value_formid && !epft_formid) return std::nullopt;
  return std::vector<std::size_t>{0};
}
std::optional<std::vector<std::size_t>> struct_payload_formid_offsets(const Record& record,
                                                                     const Subrecord& sub,
                                                                     bool strict_remap) {
  const auto signature = record.header.signature;
  const auto size = sub.data.size();
  auto malformed = [&]() -> std::optional<std::vector<std::size_t>> {
    if (strict_remap) throw FormIdError("cannot safely remap malformed structured payload");
    return std::vector<std::size_t>{};
  };
  auto within = [&](std::initializer_list<std::size_t> slots) {
    std::vector<std::size_t> offsets;
    for (const auto offset : slots) {
      if (offset + 4 <= size) offsets.push_back(offset);
    }
    return offsets;
  };

  if (signature == k_expl && sub.signature == sig_data) {
    if (size != 52 && size != 56 && size != 60 && size != 64 &&
        size != forms::k_expl_data_size) {
      return malformed();
    }
    return within({0, 4, 8, 12, 16, 20});
  }
  if (signature == k_proj && sub.signature == k_dnam) {
    if (size != forms::k_proj_dnam_size) return malformed();
    return within({16, 20, 32, 36, 52, 56, 60, 80, 84, 89});
  }
  if (signature == k_hazd && sub.signature == k_dnam) {
    if (size != forms::k_hazd_dnam_size) return malformed();
    return within({24, 28, 32, 36});
  }
  return std::nullopt;
}

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
  // xEdit wbOMODDataPropertyValue1Decider, wbDefinitionsFO4.pas:2382-2406
  const auto property_base = include_base + std::size_t{includes} * 7;
  for (std::uint32_t i = 0; i < properties; ++i) {
    const auto row = property_base + std::size_t{i} * 24;
    const auto value_type = std::to_integer<std::uint8_t>(sub.data[row]);
    if (value_type == 4 || value_type == 6) offsets.push_back(row + 12);
  }
  return offsets;
}

std::optional<std::vector<std::size_t>> packed_formid_array_offsets(const Record& record,
                                                                   const Subrecord& sub,
                                                                   bool strict_remap) {
  const auto rec = record.header.signature;
  const auto sig = sub.signature;
  if (!((rec == k_omod && (sig == k_mnam || sig == k_fnam)) ||
        (rec == k_equp && sig == k_pnam) || (rec == k_musc && sig == k_tnam) ||
        ((rec == k_fstp || rec == k_fsts) && sig == k_data) ||
        (rec == k_otft && sig == k_inam) || (rec == k_coll && sig == k_cnam) ||
        (rec == k_race && sig == k_hclf) || (rec == k_cell && sig == k_xclr) ||
        (rec == k_cobj && sig == k_fnam) || (rec == k_armo && sig == k_modl) ||
        (rec == k_arma && sig == k_modl) || (rec == k_npc_ && sig == k_tpta) ||
        (rec == k_ovis && sig == k_indx))) {
    return std::nullopt;
  }
  if (sub.data.size() % 4 != 0) {
    if (strict_remap) {
      throw FormIdError("cannot safely remap malformed packed FormID array " + rec.to_string() +
                        " " + sig.to_string());
    }
    return std::vector<std::size_t>{};
  }
  std::vector<std::size_t> offsets;
  offsets.reserve(sub.data.size() / 4);
  for (std::size_t offset = 0; offset < sub.data.size(); offset += 4) {
    offsets.push_back(offset);
  }
  return offsets;
}

bool is_placed_reference_signature(Signature signature) noexcept {
  for (const auto* text : {"REFR", "ACHR", "PGRE", "PHZD", "PMIS", "PARW", "PBAR", "PBEA", "PCON", "PFLA"}) {
    if (signature == Signature::from_string(text)) return true;
  }
  return false;
}

std::optional<std::vector<std::size_t>> fixed_stride_formid_offsets(
    const Subrecord& sub, std::uint16_t form_version, std::span<const FixedStrideLayout> layouts,
    bool strict_remap) {
  for (const auto& layout : layouts) {
    if (sub.signature != Signature::from_string(layout.signature) ||
        form_version < layout.min_form_version) {
      continue;
    }
    const auto size = sub.data.size();
    const bool valid = layout.exact ? size == layout.stride || (layout.short_size != 0 && size == layout.short_size)
                                    : size % layout.stride == 0;
    if (!valid) {
      if (strict_remap) {
        throw FormIdError("cannot safely remap malformed " + sub.signature.to_string() + " payload");
      }
      return std::vector<std::size_t>{};
    }
    std::vector<std::size_t> offsets;
    for (std::size_t base = 0; base < sub.data.size(); base += layout.stride) {
      for (const auto slot : layout.slots) {
        if (base + slot + 4 <= size) offsets.push_back(base + slot);
      }
    }
    return offsets;
  }
  return std::nullopt;
}

const FixedStrideLayout k_placed_reference_layouts[] = {
    {"XPOD", 8, {0, 4}, false, 0},
    {"XORD", 4, {0}, false, 0},
    {"XLRT", 4, {0}, false, 0},
    {"XPWR", 8, {0}, true, 4},
    {"XAPR", 8, {0}, true, 0},
    {"XPLK", 8, {0}, true, 4},
    {"XWPN", 12, {0, 4, 8}, true, 0},
};

constexpr std::uint16_t k_damage_curve_form_version = 152;

const FixedStrideLayout k_repeated_struct_layouts[] = {
    {"PRPS", 8, {0}, false, 0},
    {"DAMC", 8, {0}, false, 0},
    {"DAMA", 12, {0, 8}, false, 0, k_damage_curve_form_version},
    {"DAMA", 8, {0}, false, 0},
};

std::optional<std::vector<std::size_t>> placed_reference_formid_offsets(const Record& record,
                                                                       const Subrecord& sub,
                                                                       bool strict_remap) {
  if (!is_placed_reference_signature(record.header.signature)) return std::nullopt;
  return fixed_stride_formid_offsets(sub, record.header.form_version, k_placed_reference_layouts,
                                     strict_remap);
}

const FixedStrideLayout k_faction_layouts[] = {
    {"XNAM", 12, {0}, true, 0},
};

std::optional<std::vector<std::size_t>> faction_plvd_formid_offsets(const Subrecord& sub,
                                                                   bool strict_remap) {
  const auto size = sub.data.size();
  if (size != 12 && size != 16) {
    if (strict_remap) throw FormIdError("cannot safely remap malformed FACT PLVD payload");
    return std::vector<std::size_t>{};
  }
  const auto type = read_u32(sub.data, 0);
  const bool form_id = type == 0 || type == 1 || type == 4 || type == 6;
  return form_id ? std::vector<std::size_t>{4} : std::vector<std::size_t>{};
}

std::optional<std::vector<std::size_t>> faction_formid_offsets(const Record& record,
                                                              const Subrecord& sub,
                                                              bool strict_remap) {
  if (record.header.signature != Signature::from_string("FACT")) return std::nullopt;
  if (sub.signature == k_plvd) return faction_plvd_formid_offsets(sub, strict_remap);
  return fixed_stride_formid_offsets(sub, record.header.form_version, k_faction_layouts,
                                     strict_remap);
}

const FixedStrideLayout k_npc_layouts[] = {
    {"SNAM", 5, {0}, true, 0},
    {"PRKR", 5, {0}, true, 0},
};

std::optional<std::vector<std::size_t>> npc_formid_offsets(const Record& record,
                                                          const Subrecord& sub,
                                                          bool strict_remap) {
  if (record.header.signature != Signature::from_string("NPC_")) return std::nullopt;
  return fixed_stride_formid_offsets(sub, record.header.form_version, k_npc_layouts, strict_remap);
}

const FixedStrideLayout k_leveled_list_layouts[] = {
    {"LLKC", 8, {0}, false, 0},
};

std::optional<std::vector<std::size_t>> leveled_list_formid_offsets(const Record& record,
                                                                   const Subrecord& sub,
                                                                   bool strict_remap) {
  const auto signature = record.header.signature;
  if (signature != Signature::from_string("LVLI") && signature != Signature::from_string("LVLN")) {
    return std::nullopt;
  }
  return fixed_stride_formid_offsets(sub, record.header.form_version, k_leveled_list_layouts,
                                     strict_remap);
}

const FixedStrideLayout k_weapon_layouts[] = {
    {"DNAM", 132, {0, 40, 44, 73, 77, 81, 85, 89, 93, 97, 101}, true, 0},
    {"FNAM", 41, {29}, true, 0},
    {"CRDT", 12, {8}, true, 0},
};

std::optional<std::vector<std::size_t>> weapon_formid_offsets(const Record& record,
                                                             const Subrecord& sub,
                                                             bool strict_remap) {
  if (record.header.signature != Signature::from_string("WEAP")) return std::nullopt;
  return fixed_stride_formid_offsets(sub, record.header.form_version, k_weapon_layouts,
                                     strict_remap);
}

struct RecordStrideLayout {
  const char* record;
  FixedStrideLayout layout;
};

const RecordStrideLayout k_record_stride_layouts[] = {
    {"KSSM", {"RNAM", 8, {4}, false, 0}},
    {"MGEF", {"SNDD", 8, {4}, false, 0}},
    {"SCSN", {"CNAM", 8, {0}, false, 0}},
    {"COBJ", {"FVPA", 8, {0}, false, 0}},
    {"MISC", {"CVPA", 8, {0}, false, 0}},
    {"FURN", {"SNAM", 24, {16}, false, 0, 125}},
    {"FURN", {"SNAM", 20, {16}, false, 0}},
    {"TERM", {"SNAM", 24, {16}, false, 0, 125}},
    {"TERM", {"SNAM", 20, {16}, false, 0}},
    {"ALCH", {"ENIT", 20, {8, 16}, true, 0}},
    // xEdit wbDefinitionsFO4.pas:12313-12320
    {"DMGT", {"DNAM", 8, {0, 4}, false, 0, 78}},
    {"DMGT", {"DNAM", 4, {}, false, 0}},
    // xEdit wbDefinitionsFO4.pas:11011-11022
    {"RACE", {"RBPC", 4, {0}, false, 0, 78}},
    // xEdit wbDefinitionsFO4.pas:6519-6722
    {"EFSH", {"DNAM", 157, {108}, true, 0, 106}},
    {"EFSH", {"DNAM", 395, {241, 305}, true, 0}},
};

std::optional<std::vector<std::size_t>> record_stride_form_id_offsets(const Record& record,
                                                                    const Subrecord& sub,
                                                                    bool strict_remap) {
  for (const auto& entry : k_record_stride_layouts) {
    if (record.header.signature != Signature::from_string(entry.record)) continue;
    auto offsets = fixed_stride_formid_offsets(
        sub, record.header.form_version,
        std::span<const FixedStrideLayout>(&entry.layout, 1), strict_remap);
    if (offsets) return offsets;
  }
  return std::nullopt;
}

std::optional<std::vector<std::size_t>> book_teaches_formid_offsets(const Record& record,
                                                                   const Subrecord& sub,
                                                                   bool strict_remap) {
  if (record.header.signature != k_book || sub.signature != k_dnam) return std::nullopt;
  if (sub.data.size() != 13) {
    if (strict_remap) throw FormIdError("cannot safely remap malformed BOOK DNAM payload");
    return std::vector<std::size_t>{};
  }
  // xEdit wbDefinitionsFO4.pas:1618-1636 (decider), :5918-5923 (Teaches union)
  const auto flags = std::to_integer<std::uint8_t>(sub.data[0]);
  const bool form_id = (flags & 0x15) != 0;
  return form_id ? std::vector<std::size_t>{1} : std::vector<std::size_t>{};
}

std::optional<std::vector<std::size_t>> omod_data_layout_offsets(const Subrecord& sub) {
  const auto& data = sub.data;
  if (data.size() < 24) return std::nullopt;

  const auto includes = read_u32(data, 0);
  const auto properties = read_u32(data, 4);
  std::vector<std::size_t> offsets{16};
  std::size_t cursor = 20;
  if (!consume_counted_fixed(data, cursor, 4, [&](std::size_t base) {
        offsets.push_back(base);
      })) {
    return std::nullopt;
  }
  if (!consume_counted_fixed(data, cursor, 8, [](std::size_t) {})) return std::nullopt;
  if (cursor > data.size() || (data.size() - cursor) / 7 < includes) return std::nullopt;
  for (std::size_t include = 0; include < includes; ++include) {
    offsets.push_back(cursor + include * 7);
  }
  cursor += std::size_t{includes} * 7;
  if (cursor > data.size() || data.size() - cursor != std::size_t{properties} * 24) {
    return std::nullopt;
  }
  // xEdit wbOMODDataPropertyValue1Decider, wbDefinitionsFO4.pas:2382-2406
  for (std::size_t row = cursor; row < data.size(); row += 24) {
    const auto value_type = std::to_integer<std::uint8_t>(data[row]);
    if (value_type == 4 || value_type == 6) offsets.push_back(row + 12);
  }
  return offsets;
}

std::optional<std::vector<std::size_t>> omod_data_formid_offsets(const Record& record,
                                                                const Subrecord& sub,
                                                                bool strict_remap) {
  if (record.header.signature != k_omod || sub.signature != k_data) return std::nullopt;
  // xEdit wbDefinitionsFO4.pas:12510-12537
  if (auto offsets = omod_data_layout_offsets(sub)) return offsets;
  if (strict_remap) throw FormIdError("cannot safely remap malformed OMOD DATA payload");
  return std::vector<std::size_t>{};
}

bool mgef_assoc_item_is_formid(std::uint32_t archetype) {
  switch (archetype) {
    case 12: case 17: case 18: case 25: case 34: case 35:
    case 36: case 39: case 40: case 45: case 46:
      return true;
    default:
      return false;
  }
}

// Must not own MGEF DATA: owning drops the schema formlink rows (form_link_schema.cpp).
std::optional<std::vector<std::size_t>> mgef_assoc_item_formid_offsets(const Record& record,
                                                                      const Subrecord& sub) {
  if (record.header.signature != k_mgef || sub.signature != k_data || sub.data.size() != 152) {
    return std::nullopt;
  }
  // xEdit wbDefinitionsFO4.pas:1367-1389 (decider), :10096-10110 (Assoc. Item), :10124 (Archetype)
  if (!mgef_assoc_item_is_formid(read_u32(sub.data, 64))) return std::vector<std::size_t>{};
  return std::vector<std::size_t>{8};
}

std::optional<std::vector<std::size_t>> mixed_formid_offsets(const Record& record,
                                                            const Subrecord& sub,
                                                            bool strict_remap) {
  if (auto behavior = behavior_formid_offsets(record, sub, strict_remap)) return behavior;
  if (auto book = book_teaches_formid_offsets(record, sub, strict_remap)) return book;
  if (auto omod = omod_data_formid_offsets(record, sub, strict_remap)) return omod;
  if (auto perk = perk_formid_offsets(record, sub)) return perk;
  if (auto packed = packed_formid_array_offsets(record, sub, strict_remap)) return packed;
  if (auto repeated = fixed_stride_formid_offsets(sub, record.header.form_version,
                                                  k_repeated_struct_layouts, strict_remap)) {
    return repeated;
  }
  if (auto record_stride = record_stride_form_id_offsets(record, sub, strict_remap)) {
    return record_stride;
  }
  if (auto faction = faction_formid_offsets(record, sub, strict_remap)) return faction;
  if (auto weapon = weapon_formid_offsets(record, sub, strict_remap)) return weapon;
  if (auto npc = npc_formid_offsets(record, sub, strict_remap)) return npc;
  if (auto leveled = leveled_list_formid_offsets(record, sub, strict_remap)) return leveled;
  if (auto records = record_struct_formid_offsets(record, sub, strict_remap)) return records;
  return placed_reference_formid_offsets(record, sub, strict_remap);
}

}
