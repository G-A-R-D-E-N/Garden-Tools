#include "fo4/schema/vmad.hpp"

#include "fo4/error.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <vector>

namespace fo4 {
namespace {

class Slice {
public:
  explicit Slice(std::span<const std::byte> data) : data_(data) {}

  std::size_t position() const noexcept { return pos_; }
  bool at_end() const noexcept { return pos_ >= data_.size(); }

  std::uint8_t u8() {
    require(1);
    return std::to_integer<std::uint8_t>(data_[pos_++]);
  }

  std::uint16_t u16() {
    const auto lo = static_cast<std::uint16_t>(u8());
    const auto hi = static_cast<std::uint16_t>(u8());
    return static_cast<std::uint16_t>(lo | (hi << 8));
  }

  std::int16_t i16() { return static_cast<std::int16_t>(u16()); }

  std::uint32_t u32() {
    const auto b0 = static_cast<std::uint32_t>(u8());
    const auto b1 = static_cast<std::uint32_t>(u8());
    const auto b2 = static_cast<std::uint32_t>(u8());
    const auto b3 = static_cast<std::uint32_t>(u8());
    return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
  }

  void skip(std::size_t count) {
    require(count);
    pos_ += count;
  }

  void skip_wstring() {
    const auto n = u16();
    skip(n);
  }

private:
  void require(std::size_t count) const {
    if (count > data_.size() - pos_) {
      throw parse_error(pos_, "truncated VMAD");
    }
  }

  std::span<const std::byte> data_;
  std::size_t pos_{0};
};

struct UnsupportedVmadProperty {};

struct VmadScan {
  std::vector<std::size_t> offsets;
  bool unsupported{false};
  bool failed{false};
  bool fully_consumed{false};
  bool trailing_data{false};
  std::int16_t object_format{0};
  std::uint16_t quest_fragment_count{0};
  std::uint16_t quest_alias_count{0};
  std::vector<QuestVmadFragmentBinding> quest_fragments;
  std::vector<QuestVmadAliasBinding> quest_aliases;
};

constexpr std::uint8_t k_type_object = 1;
constexpr std::uint8_t k_type_string = 2;
constexpr std::uint8_t k_type_int = 3;
constexpr std::uint8_t k_type_float = 4;
constexpr std::uint8_t k_type_bool = 5;
constexpr std::uint8_t k_type_variable = 6;
constexpr std::uint8_t k_type_struct = 7;
constexpr std::uint8_t k_type_object_array = 11;
constexpr std::uint8_t k_type_string_array = 12;
constexpr std::uint8_t k_type_int_array = 13;
constexpr std::uint8_t k_type_float_array = 14;
constexpr std::uint8_t k_type_bool_array = 15;
constexpr std::uint8_t k_type_variable_array = 16;
constexpr std::uint8_t k_type_struct_array = 17;

void walk_object(Slice& slice, std::int16_t object_format,
                 std::vector<std::size_t>& offsets) {
  if (object_format == 2) {
    slice.u16();
    slice.u16();
    offsets.push_back(slice.position());
    slice.u32();
  } else if (object_format == 1) {
    offsets.push_back(slice.position());
    slice.u32();
    slice.u16();
    slice.u16();
  } else {
    throw parse_error(slice.position(), "unknown VMAD object format");
  }
}

void walk_property(Slice& slice, std::int16_t object_format,
                   std::vector<std::size_t>& offsets);

void walk_properties(Slice& slice, std::int16_t object_format, std::uint32_t count,
                     std::vector<std::size_t>& offsets) {
  for (std::uint32_t i = 0; i < count; ++i) {
    walk_property(slice, object_format, offsets);
  }
}

void walk_property(Slice& slice, std::int16_t object_format,
                   std::vector<std::size_t>& offsets) {
  slice.skip_wstring();
  const auto type = slice.u8();
  slice.u8();  // flags
  switch (type) {
    case 0:
      break;
    case k_type_variable:
    case k_type_variable_array:
      // xEdit identifies type 16 as FO76 Variable Array and does not implement
      // it. Type 6 is likewise dynamically typed. Without a discriminator for
      // the nested value, consuming bytes here would risk losing VMAD sync.
      throw UnsupportedVmadProperty{};
    case k_type_object:
      walk_object(slice, object_format, offsets);
      break;
    case k_type_string:
      slice.skip_wstring();
      break;
    case k_type_int:
    case k_type_float:
      slice.u32();
      break;
    case k_type_bool:
      slice.u8();
      break;
    case k_type_struct:
      walk_properties(slice, object_format, slice.u32(), offsets);
      break;
    case k_type_object_array: {
      const auto n = slice.u32();
      for (std::uint32_t i = 0; i < n; ++i) {
        walk_object(slice, object_format, offsets);
      }
      break;
    }
    case k_type_string_array: {
      const auto n = slice.u32();
      for (std::uint32_t i = 0; i < n; ++i) {
        slice.skip_wstring();
      }
      break;
    }
    case k_type_int_array:
    case k_type_float_array: {
      const auto n = slice.u32();
      for (std::uint32_t i = 0; i < n; ++i) {
        slice.u32();
      }
      break;
    }
    case k_type_bool_array: {
      const auto n = slice.u32();
      slice.skip(n);
      break;
    }
    case k_type_struct_array: {
      const auto n = slice.u32();
      for (std::uint32_t i = 0; i < n; ++i) {
        walk_properties(slice, object_format, slice.u32(), offsets);
      }
      break;
    }
    default:
      throw parse_error(slice.position(), "unknown VMAD property type");
  }
}

void walk_script_entry(Slice& slice, std::int16_t object_format,
                       std::vector<std::size_t>& offsets) {
  const auto name_len = slice.u16();
  slice.skip(name_len);
  if (name_len == 0) {
    return;
  }
  slice.u8();
  walk_properties(slice, object_format, slice.u16(), offsets);
}

void walk_scripts(Slice& slice, std::int16_t object_format, std::uint16_t count,
                  std::vector<std::size_t>& offsets) {
  for (std::uint16_t i = 0; i < count; ++i) {
    walk_script_entry(slice, object_format, offsets);
  }
}

void walk_quest_extra(Slice& slice, std::int16_t object_format, VmadScan& result) {
  slice.u8();
  const auto fragment_count = slice.u16();
  const auto script_name_len = slice.u16();
  slice.skip(script_name_len);
  if (script_name_len != 0) {
    slice.u8();
    walk_properties(slice, object_format, slice.u16(), result.offsets);
  }
  result.quest_fragments.reserve(fragment_count);
  for (std::uint16_t i = 0; i < fragment_count; ++i) {
    QuestVmadFragmentBinding fragment;
    fragment.stage_offset = slice.position();
    fragment.stage = slice.u16();
    slice.u16();
    fragment.stage_index_offset = slice.position();
    fragment.stage_index = static_cast<std::int32_t>(slice.u32());
    slice.u8();
    slice.skip_wstring();
    slice.skip_wstring();
    result.quest_fragments.push_back(fragment);
  }
  const auto alias_count = slice.u16();
  result.quest_fragment_count = fragment_count;
  result.quest_alias_count = alias_count;
  result.quest_aliases.reserve(alias_count);
  for (std::uint16_t i = 0; i < alias_count; ++i) {
    QuestVmadAliasBinding alias;
    if (object_format == 2) {
      slice.u16();
      alias.alias_id_offset = slice.position();
      alias.alias_id = slice.i16();
      result.offsets.push_back(slice.position());
      slice.u32();
    } else if (object_format == 1) {
      result.offsets.push_back(slice.position());
      slice.u32();
      alias.alias_id_offset = slice.position();
      alias.alias_id = slice.i16();
      slice.u16();
    } else {
      throw parse_error(slice.position(), "unknown VMAD object format");
    }
    result.quest_aliases.push_back(alias);
    slice.i16();
    const auto alias_format = slice.i16();
    walk_scripts(slice, alias_format, slice.u16(), result.offsets);
  }
}

void walk_indexed_fragments(Slice& slice, std::int16_t object_format,
                            std::vector<std::size_t>& offsets) {
  slice.u8();
  walk_script_entry(slice, object_format, offsets);
  const auto n = slice.u16();
  for (std::uint16_t i = 0; i < n; ++i) {
    slice.u16();
    slice.u16();
    slice.u8();
    slice.skip_wstring();
    slice.skip_wstring();
  }
}

void walk_flagged_fragments(Slice& slice, std::int16_t object_format,
                            std::vector<std::size_t>& offsets, bool scene) {
  slice.u8();  // extra bind
  const auto flags = slice.u8();
  walk_script_entry(slice, object_format, offsets);
  unsigned count = 0;
  auto bits = flags;
  for (int i = 0; i < 8; ++i) {
    if ((bits & 1u) != 0) {
      ++count;
    }
    bits = static_cast<std::uint8_t>(bits >> 1);
  }
  for (unsigned i = 0; i < count; ++i) {
    slice.u8();
    slice.skip_wstring();
    slice.skip_wstring();
  }
  if (scene && !slice.at_end()) {
    const auto phases = slice.u16();
    for (std::uint16_t i = 0; i < phases; ++i) {
      slice.u8();
      slice.u8();
      slice.u16();
      slice.u8();
      slice.u8();
      slice.skip_wstring();
      slice.skip_wstring();
    }
  }
}

VmadScan scan_vmad(std::span<const std::byte> data, Signature record_signature) {
  VmadScan result;
  if (data.size() < 6) {
    return result;
  }

  try {
    Slice slice{data};
    slice.i16();  // version
    const auto object_format = slice.i16();
    result.object_format = object_format;
    walk_scripts(slice, object_format, slice.u16(), result.offsets);
    if (!slice.at_end()) {
      if (record_signature == sig_qust) {
        walk_quest_extra(slice, object_format, result);
      } else if (record_signature == sig_perk || record_signature == sig_term) {
        walk_indexed_fragments(slice, object_format, result.offsets);
      } else if (record_signature == sig_pack || record_signature == sig_info) {
        walk_flagged_fragments(slice, object_format, result.offsets, false);
      } else if (record_signature == sig_scen) {
        walk_flagged_fragments(slice, object_format, result.offsets, true);
      }
    }
    result.fully_consumed = slice.at_end();
    result.trailing_data = !slice.at_end();
  } catch (const UnsupportedVmadProperty&) {
    result.offsets.clear();
    result.unsupported = true;
    result.failed = true;
    result.quest_fragment_count = 0;
    result.quest_alias_count = 0;
  } catch (const parse_error&) {
    result.offsets.clear();
    result.failed = true;
    result.quest_fragment_count = 0;
    result.quest_alias_count = 0;
  }
  return result;
}

}  // namespace

std::vector<std::size_t> vmad_formid_offsets(std::span<const std::byte> data,
                                              Signature record_signature) {
  return scan_vmad(data, record_signature).offsets;
}

VmadCoverage vmad_coverage(std::span<const std::byte> data,
                           Signature record_signature) {
  const auto scan = scan_vmad(data, record_signature);
  VmadCoverage coverage;
  coverage.unsupported = scan.unsupported;
  coverage.failed = scan.failed;
  coverage.trailing_data = scan.trailing_data;
  coverage.fully_consumed = scan.fully_consumed;
  return coverage;
}

bool vmad_has_unsupported_properties(std::span<const std::byte> data,
                                     Signature record_signature) {
  return vmad_coverage(data, record_signature).unsupported;
}

std::optional<std::vector<std::size_t>> vmad_formid_offsets_for_remap(
    std::span<const std::byte> data,
    Signature record_signature) {
  auto scan = scan_vmad(data, record_signature);
  if (scan.unsupported || scan.failed || scan.trailing_data ||
      !scan.fully_consumed) {
    return std::nullopt;
  }
  return std::move(scan.offsets);
}

QuestVmadBindings quest_vmad_bindings(std::span<const std::byte> data) {
  QuestVmadBindings bindings;
  if (data.size() < 6) {
    bindings.unparseable = true;
    return bindings;
  }
  const auto scan = scan_vmad(data, sig_qust);
  if (scan.unsupported || scan.failed || scan.trailing_data || !scan.fully_consumed) {
    bindings.unparseable = true;
    return bindings;
  }
  bindings.binds_stages = scan.quest_fragment_count > 0;
  bindings.binds_aliases = scan.quest_alias_count > 0;
  return bindings;
}

QuestVmadLayout inspect_quest_vmad(std::span<const std::byte> data) {
  QuestVmadLayout layout;
  if (data.size() < 6) {
    layout.unparseable = true;
    return layout;
  }
  const auto scan = scan_vmad(data, sig_qust);
  if (scan.unsupported) {
    layout.unsupported = true;
    layout.unparseable = true;
    return layout;
  }
  if (scan.failed || scan.trailing_data || !scan.fully_consumed) {
    layout.unparseable = true;
    return layout;
  }
  layout.object_format = scan.object_format;
  layout.fragments = scan.quest_fragments;
  layout.aliases = scan.quest_aliases;
  return layout;
}

}  // namespace fo4
