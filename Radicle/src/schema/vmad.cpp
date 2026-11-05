#include "fo4/schema/vmad.hpp"

#include "fo4/error.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
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

  std::string wstring() {
    const auto n = u16();
    require(n);
    std::string text(reinterpret_cast<const char*>(data_.data() + pos_), n);
    pos_ += n;
    return text;
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
  std::uint32_t fragment_count{0};
  std::uint32_t fragment_script_count{0};
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

void require_zero_unused_word(Slice& slice) {
  if (slice.u16() != 0) throw UnsupportedVmadProperty{};
}

void walk_object(Slice& slice, std::int16_t object_format,
                 std::vector<std::size_t>& offsets) {
  if (object_format == 2) {
    require_zero_unused_word(slice);
    slice.u16();
    offsets.push_back(slice.position());
    slice.u32();
  } else if (object_format == 1) {
    offsets.push_back(slice.position());
    slice.u32();
    slice.u16();
    require_zero_unused_word(slice);
  } else {
    throw parse_error(slice.position(), "unknown VMAD object format");
  }
}

void walk_property(Slice& slice, std::int16_t object_format,
                   std::vector<std::size_t>& offsets, bool nested);

void walk_properties(Slice& slice, std::int16_t object_format, std::uint32_t count,
                     std::vector<std::size_t>& offsets, bool nested = false) {
  for (std::uint32_t i = 0; i < count; ++i) {
    walk_property(slice, object_format, offsets, nested);
  }
}

void walk_property(Slice& slice, std::int16_t object_format,
                   std::vector<std::size_t>& offsets, bool nested) {
  slice.skip_wstring();
  const auto type = slice.u8();
  slice.u8();  // flags
  switch (type) {
    case 0:
      break;
    case k_type_variable:
      throw UnsupportedVmadProperty{};
    case k_type_variable_array:
      if (slice.u32() != 0) throw UnsupportedVmadProperty{};
      break;
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
      if (nested) throw parse_error(slice.position(), "nested VMAD struct");
      walk_properties(slice, object_format, slice.u32(), offsets, true);
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
      if (nested) throw parse_error(slice.position(), "nested VMAD struct");
      const auto n = slice.u32();
      for (std::uint32_t i = 0; i < n; ++i) {
        walk_properties(slice, object_format, slice.u32(), offsets, true);
      }
      break;
    }
    default:
      throw UnsupportedVmadProperty{};
  }
}

bool walk_script_entry(Slice& slice, std::int16_t object_format,
                       std::vector<std::size_t>& offsets) {
  const auto name_len = slice.u16();
  slice.skip(name_len);
  slice.u8();
  walk_properties(slice, object_format, slice.u16(), offsets);
  return name_len != 0;
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
  result.fragment_count = fragment_count;
  result.fragment_script_count = script_name_len != 0 ? 1u : 0u;
  result.quest_alias_count = alias_count;
  result.quest_aliases.reserve(alias_count);
  for (std::uint16_t i = 0; i < alias_count; ++i) {
    QuestVmadAliasBinding alias;
    if (object_format == 2) {
      require_zero_unused_word(slice);
      alias.alias_id_offset = slice.position();
      alias.alias_id = slice.i16();
      result.offsets.push_back(slice.position());
      slice.u32();
    } else if (object_format == 1) {
      result.offsets.push_back(slice.position());
      slice.u32();
      alias.alias_id_offset = slice.position();
      alias.alias_id = slice.i16();
      require_zero_unused_word(slice);
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
                            VmadScan& result) {
  slice.u8();
  result.fragment_script_count = walk_script_entry(slice, object_format, result.offsets) ? 1u : 0u;
  const auto n = slice.u16();
  result.fragment_count = n;
  for (std::uint16_t i = 0; i < n; ++i) {
    slice.u16();
    slice.u16();
    slice.u8();
    slice.skip_wstring();
    slice.skip_wstring();
  }
}

void walk_flagged_fragments(Slice& slice, std::int16_t object_format,
                            VmadScan& result, bool scene, unsigned flag_bits) {
  slice.u8();  // extra bind
  const auto flags = slice.u8();
  result.fragment_script_count = walk_script_entry(slice, object_format, result.offsets) ? 1u : 0u;
  unsigned count = 0;
  auto bits = flags;
  for (unsigned i = 0; i < flag_bits; ++i) {
    if ((bits & 1u) != 0) {
      ++count;
    }
    bits = static_cast<std::uint8_t>(bits >> 1);
  }
  result.fragment_count = count;
  for (unsigned i = 0; i < count; ++i) {
    slice.u8();
    slice.skip_wstring();
    slice.skip_wstring();
  }
  if (scene && !slice.at_end()) {
    const auto phases = slice.u16();
    result.fragment_count += phases;
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
        walk_indexed_fragments(slice, object_format, result);
      } else if (record_signature == sig_pack || record_signature == sig_info) {
        walk_flagged_fragments(slice, object_format, result, false,
                               record_signature == sig_pack ? 3u : 2u);
      } else if (record_signature == sig_scen) {
        walk_flagged_fragments(slice, object_format, result, true, 2u);
      }
    }
    result.fully_consumed = slice.at_end();
    result.trailing_data = !slice.at_end();
  } catch (const UnsupportedVmadProperty&) {
    // Requires: links parsed before the unsupported property stay valid.
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
  if (coverage.complete()) {
    coverage.fragment_count = scan.fragment_count;
    coverage.fragment_script_count = scan.fragment_script_count;
  }
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

namespace {

VmadObjectElement read_object_element(Slice& slice, std::int16_t object_format) {
  VmadObjectElement element;
  if (object_format == 2) {
    require_zero_unused_word(slice);
    element.alias = slice.i16();
    element.object = RawFormId{slice.u32()};
  } else if (object_format == 1) {
    element.object = RawFormId{slice.u32()};
    element.alias = slice.i16();
    require_zero_unused_word(slice);
  } else {
    throw parse_error(slice.position(), "unknown VMAD object format");
  }
  return element;
}

float read_float(Slice& slice) {
  const auto bits = slice.u32();
  float value = 0.0f;
  std::memcpy(&value, &bits, 4);
  return value;
}

bool read_struct_members(Slice& slice, std::int16_t object_format, VmadStruct& out) {
  const auto count = slice.u32();
  for (std::uint32_t i = 0; i < count; ++i) {
    VmadStructMember member;
    member.name = slice.wstring();
    member.type = slice.u8();
    member.flags = slice.u8();
    switch (member.type) {
      case k_type_object:
        member.object = read_object_element(slice, object_format);
        break;
      case k_type_string:
        member.text = slice.wstring();
        break;
      case k_type_int:
        member.integer = static_cast<std::int32_t>(slice.u32());
        break;
      case k_type_float:
        member.real = read_float(slice);
        break;
      case k_type_bool:
        member.boolean = slice.u8() != 0;
        break;
      default:
        return false;
    }
    out.members.push_back(std::move(member));
  }
  return true;
}

std::optional<std::vector<VmadStruct>> decode_structs(Slice values, std::int16_t object_format,
                                                      bool array) {
  std::vector<VmadStruct> structs;
  try {
    const auto n = array ? values.u32() : 1u;
    for (std::uint32_t i = 0; i < n; ++i) {
      VmadStruct value;
      if (!read_struct_members(values, object_format, value)) return std::nullopt;
      structs.push_back(std::move(value));
    }
  } catch (const UnsupportedVmadProperty&) {
    return std::nullopt;
  }
  return structs;
}

VmadProperty read_property(Slice& slice, std::int16_t object_format) {
  VmadProperty property;
  property.offset = slice.position();
  property.name = slice.wstring();
  property.type = slice.u8();
  property.flags = slice.u8();
  property.value_offset = slice.position();
  const auto finish = [&] {
    property.value_size = slice.position() - property.value_offset;
    return property;
  };
  switch (property.type) {
    case k_type_object: {
      const auto element = read_object_element(slice, object_format);
      property.object = element.object;
      property.alias = element.alias;
      return finish();
    }
    case k_type_string:
      property.text = slice.wstring();
      return finish();
    case k_type_int:
      property.integer = static_cast<std::int32_t>(slice.u32());
      return finish();
    case k_type_float:
      property.real = read_float(slice);
      return finish();
    case k_type_bool:
      property.boolean = slice.u8() != 0;
      return finish();
    default:
      break;
  }
  if (property.type == k_type_struct || (property.type >= k_type_object_array &&
                                         property.type <= k_type_struct_array)) {
    Slice peek = slice;
    property.element_count = peek.u32();
  }
  std::vector<std::size_t> ignored;
  switch (property.type) {
    case 0:
      break;
    case k_type_variable:
      throw UnsupportedVmadProperty{};
    case k_type_variable_array:
      if (slice.u32() != 0) throw UnsupportedVmadProperty{};
      break;
    case k_type_struct: {
      Slice values = slice;
      walk_properties(slice, object_format, slice.u32(), ignored);
      property.struct_elements = decode_structs(values, object_format, false);
      break;
    }
    case k_type_object_array: {
      auto& elements = property.object_elements.emplace();
      const auto n = slice.u32();
      for (std::uint32_t i = 0; i < n; ++i) elements.push_back(read_object_element(slice, object_format));
      break;
    }
    case k_type_string_array: {
      auto& elements = property.text_elements.emplace();
      const auto n = slice.u32();
      for (std::uint32_t i = 0; i < n; ++i) elements.push_back(slice.wstring());
      break;
    }
    case k_type_int_array: {
      auto& elements = property.integer_elements.emplace();
      const auto n = slice.u32();
      for (std::uint32_t i = 0; i < n; ++i) elements.push_back(static_cast<std::int32_t>(slice.u32()));
      break;
    }
    case k_type_float_array: {
      auto& elements = property.real_elements.emplace();
      const auto n = slice.u32();
      for (std::uint32_t i = 0; i < n; ++i) elements.push_back(read_float(slice));
      break;
    }
    case k_type_bool_array: {
      auto& elements = property.boolean_elements.emplace();
      const auto n = slice.u32();
      for (std::uint32_t i = 0; i < n; ++i) elements.push_back(slice.u8() != 0);
      break;
    }
    case k_type_struct_array: {
      Slice values = slice;
      const auto n = slice.u32();
      for (std::uint32_t i = 0; i < n; ++i) walk_properties(slice, object_format, slice.u32(), ignored);
      property.struct_elements = decode_structs(values, object_format, true);
      break;
    }
    default:
      throw UnsupportedVmadProperty{};
  }
  return finish();
}

}  // namespace

VmadScripts read_vmad_scripts(std::span<const std::byte> data) {
  Slice slice(data);
  VmadScripts result;
  result.version = slice.i16();
  result.object_format = slice.i16();
  const auto count = slice.u16();
  for (std::uint16_t i = 0; i < count; ++i) {
    VmadScript script;
    script.offset = slice.position();
    script.name = slice.wstring();
    script.flags = slice.u8();
    script.property_count_offset = slice.position();
    const auto properties = slice.u16();
    for (std::uint16_t p = 0; p < properties; ++p) {
      try {
        script.properties.push_back(read_property(slice, result.object_format));
      } catch (const UnsupportedVmadProperty&) {
        result.unsupported = true;
        result.scripts.push_back(std::move(script));
        return result;
      }
    }
    result.scripts.push_back(std::move(script));
  }
  result.trailing_bytes = data.size() - slice.position();
  return result;
}

QuestVmadExtras read_quest_vmad_extras(std::span<const std::byte> data, std::int16_t object_format) {
  QuestVmadExtras result;
  try {
    Slice slice(data);
    slice.u8();
    result.count_offset = slice.position();
    const auto fragment_count = slice.u16();
    result.script_name = slice.wstring();
    if (!result.script_name.empty()) {
      slice.u8();
      const auto properties = slice.u16();
      for (std::uint16_t i = 0; i < properties; ++i) {
        result.properties.push_back(read_property(slice, object_format));
      }
    }

    result.entries_begin = slice.position();
    result.fragments.reserve(fragment_count);
    for (std::uint16_t i = 0; i < fragment_count; ++i) {
      QuestVmadFragment fragment;
      fragment.offset = slice.position();
      fragment.stage = slice.u16();
      fragment.unknown = slice.u16();
      fragment.stage_index = static_cast<std::int32_t>(slice.u32());
      fragment.unknown_byte = slice.u8();
      fragment.script = slice.wstring();
      fragment.function = slice.wstring();
      fragment.end = slice.position();
      result.fragments.push_back(std::move(fragment));
    }

    const auto alias_count = slice.u16();
    result.aliases.reserve(alias_count);
    for (std::uint16_t i = 0; i < alias_count; ++i) {
      QuestVmadAliasScripts alias;
      if (object_format == 2) {
        require_zero_unused_word(slice);
        alias.alias_id = slice.i16();
        slice.u32();
      } else if (object_format == 1) {
        slice.u32();
        alias.alias_id = slice.i16();
        require_zero_unused_word(slice);
      } else {
        throw parse_error(slice.position(), "unknown VMAD object format");
      }
      slice.i16();
      alias.script_format = slice.i16();
      const auto scripts = slice.u16();
      for (std::uint16_t s = 0; s < scripts; ++s) {
        VmadScript script;
        script.name = slice.wstring();
        script.flags = slice.u8();
        const auto properties = slice.u16();
        for (std::uint16_t p = 0; p < properties; ++p) {
          script.properties.push_back(read_property(slice, alias.script_format));
        }
        alias.scripts.push_back(std::move(script));
      }
      result.aliases.push_back(std::move(alias));
    }
    result.fully_consumed = slice.at_end();
  } catch (const UnsupportedVmadProperty&) {
    result.unsupported = true;
    result.unsupported_reason = "a VMAD property in the quest extras is unsupported";
  } catch (const parse_error& error) {
    result.unsupported = true;
    result.unsupported_reason = error.what();
  }
  return result;
}

SceneVmadExtras read_scene_vmad_extras(std::span<const std::byte> data,
                                       std::int16_t object_format) {
  SceneVmadExtras result;
  try {
    Slice slice(data);
    slice.u8();
    result.flags = slice.u8();
    const unsigned entries = (result.flags & 1u) + ((result.flags >> 1) & 1u);
    std::vector<std::size_t> ignored;
    walk_script_entry(slice, object_format, ignored);
    for (unsigned i = 0; i < entries; ++i) {
      slice.u8();
      slice.skip_wstring();
      slice.skip_wstring();
    }
    const auto phases = slice.u16();
    result.phases.reserve(phases);
    for (std::uint16_t i = 0; i < phases; ++i) {
      SceneVmadPhaseFragment phase;
      phase.offset = slice.position();
      phase.flag = slice.u8();
      phase.index = slice.u32();
      phase.unknown = slice.u8();
      phase.script = slice.wstring();
      phase.function = slice.wstring();
      phase.end = slice.position();
      result.phases.push_back(std::move(phase));
    }
    result.fully_consumed = slice.at_end();
  } catch (const UnsupportedVmadProperty&) {
    result.unsupported = true;
    result.unsupported_reason = "a VMAD property in the scene extras is unsupported";
  } catch (const parse_error& error) {
    result.unsupported = true;
    result.unsupported_reason = error.what();
  }
  return result;
}

InfoVmadExtras read_info_vmad_extras(std::span<const std::byte> data,
                                     std::int16_t object_format) {
  InfoVmadExtras result;
  try {
    Slice slice(data);
    result.version = static_cast<std::int8_t>(slice.u8());
    result.flags = slice.u8();
    std::vector<std::size_t> ignored;
    walk_script_entry(slice, object_format, ignored);
    // Constraint: INFO stores no fragment count; the OnBegin/OnEnd flag bits are the count.
    const unsigned count = (result.flags & 1u) + ((result.flags >> 1) & 1u);
    result.fragments.reserve(count);
    for (unsigned i = 0; i < count; ++i) {
      InfoVmadFragment fragment;
      fragment.offset = slice.position();
      fragment.unknown = slice.u8();
      fragment.script = slice.wstring();
      fragment.function = slice.wstring();
      fragment.end = slice.position();
      result.fragments.push_back(std::move(fragment));
    }
    result.fully_consumed = slice.at_end();
  } catch (const UnsupportedVmadProperty&) {
    result.unsupported = true;
    result.unsupported_reason = "a VMAD property in the info extras is unsupported";
  } catch (const parse_error& error) {
    result.unsupported = true;
    result.unsupported_reason = error.what();
  }
  return result;
}

}  // namespace fo4
