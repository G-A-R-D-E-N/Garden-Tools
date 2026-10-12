#include "fo4/schema/form_link_mixed.hpp"

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

  return function_type == 3 || function_type == 4 || function_type == 5;
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
  return offsets;
}

std::optional<std::vector<std::size_t>> packed_formid_array_offsets(const Record& record,
                                                                   const Subrecord& sub,
                                                                   bool strict_remap) {
  const auto rec = record.header.signature;
  const auto sig = sub.signature;
  if (!((rec == k_omod && (sig == k_mnam || sig == k_fnam)) ||
        (rec == k_equp && sig == k_pnam) || (rec == k_musc && sig == k_tnam) ||
        ((rec == k_fstp || rec == k_fsts) && sig == k_data))) {
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

struct FixedStrideLayout {
  const char* signature;
  std::size_t stride;
  std::initializer_list<std::size_t> slots;
  bool exact;
  std::size_t short_size;
  std::uint16_t min_form_version{0};
};

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

std::optional<std::vector<std::size_t>> mixed_formid_offsets(const Record& record,
                                                            const Subrecord& sub,
                                                            bool strict_remap) {
  if (auto packed = packed_formid_array_offsets(record, sub, strict_remap)) return packed;
  if (auto repeated = fixed_stride_formid_offsets(sub, record.header.form_version,
                                                  k_repeated_struct_layouts, strict_remap)) {
    return repeated;
  }
  return placed_reference_formid_offsets(record, sub, strict_remap);
}

}
