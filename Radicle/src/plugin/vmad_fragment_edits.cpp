#include "fo4/plugin/vmad_writer.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/schema/scalar.hpp"
#include "record_edit.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4 {
namespace {

constexpr auto k_vmad = Signature::from_string("VMAD");

constexpr std::uint8_t k_type_object = 1;
constexpr std::uint8_t k_type_string = 2;
constexpr std::uint8_t k_type_int = 3;
constexpr std::uint8_t k_type_float = 4;
constexpr std::uint8_t k_type_bool = 5;
constexpr std::uint8_t k_type_struct = 7;
constexpr std::uint8_t k_type_object_array = 11;
constexpr std::uint8_t k_type_string_array = 12;
constexpr std::uint8_t k_type_int_array = 13;
constexpr std::uint8_t k_type_float_array = 14;
constexpr std::uint8_t k_type_bool_array = 15;
constexpr std::uint8_t k_type_variable_array = 16;
constexpr std::uint8_t k_type_struct_array = 17;

class Cursor {
 public:
  explicit Cursor(std::span<const std::byte> data) : data_(data) {}

  std::size_t position() const noexcept { return pos_; }

  std::uint8_t u8() {
    require(1);
    return std::to_integer<std::uint8_t>(data_[pos_++]);
  }

  std::uint16_t u16() {
    const auto lo = u8();
    return static_cast<std::uint16_t>(lo | (static_cast<std::uint16_t>(u8()) << 8));
  }

  std::uint32_t u32() {
    const auto lo = u16();
    return lo | (static_cast<std::uint32_t>(u16()) << 16);
  }

  std::string wstring() {
    const auto length = u16();
    require(length);
    std::string text(reinterpret_cast<const char*>(data_.data() + pos_), length);
    pos_ += length;
    return text;
  }

  void skip(std::size_t count) {
    require(count);
    pos_ += count;
  }

 private:
  void require(std::size_t count) const {
    if (count > data_.size() - pos_) {
      throw schema::ScalarError("truncated VMAD fragment section");
    }
  }

  std::span<const std::byte> data_;
  std::size_t pos_{0};
};

void skip_object(Cursor& cursor, std::int16_t object_format) {
  if (object_format == 1) {
    cursor.u32();
    cursor.u16();
    cursor.u16();
    return;
  }
  if (object_format == 2) {
    cursor.u16();
    cursor.u16();
    cursor.u32();
    return;
  }
  throw schema::ScalarError("unknown VMAD object format");
}

void skip_property(Cursor& cursor, std::int16_t object_format, bool nested);

void skip_properties(Cursor& cursor, std::int16_t object_format, std::uint32_t count,
                     bool nested) {
  for (std::uint32_t i = 0; i < count; ++i) {
    skip_property(cursor, object_format, nested);
  }
}

void skip_property(Cursor& cursor, std::int16_t object_format, bool nested) {
  cursor.wstring();
  const auto type = cursor.u8();
  cursor.u8();
  switch (type) {
    case 0:
      return;
    case k_type_object:
      skip_object(cursor, object_format);
      return;
    case k_type_string:
      cursor.wstring();
      return;
    case k_type_int:
    case k_type_float:
      cursor.u32();
      return;
    case k_type_bool:
      cursor.u8();
      return;
    case k_type_struct: {
      if (nested) throw schema::ScalarError("nested VMAD struct");
      skip_properties(cursor, object_format, cursor.u32(), true);
      return;
    }
    case k_type_object_array: {
      const auto n = cursor.u32();
      for (std::uint32_t i = 0; i < n; ++i) {
        skip_object(cursor, object_format);
      }
      return;
    }
    case k_type_string_array: {
      const auto n = cursor.u32();
      for (std::uint32_t i = 0; i < n; ++i) {
        cursor.wstring();
      }
      return;
    }
    case k_type_int_array:
    case k_type_float_array: {
      const auto n = cursor.u32();
      for (std::uint32_t i = 0; i < n; ++i) {
        cursor.u32();
      }
      return;
    }
    case k_type_bool_array:
      cursor.skip(cursor.u32());
      return;
    case k_type_struct_array: {
      if (nested) throw schema::ScalarError("nested VMAD struct");
      const auto n = cursor.u32();
      for (std::uint32_t i = 0; i < n; ++i) {
        skip_properties(cursor, object_format, cursor.u32(), true);
      }
      return;
    }
    case k_type_variable_array:
      if (cursor.u32() != 0) {
        throw schema::ScalarError("VMAD variable array cannot be skipped");
      }
      return;
    default:
      throw schema::ScalarError("VMAD property type cannot be skipped");
  }
}

bool skip_script_entry(Cursor& cursor, std::int16_t object_format) {
  const auto name = cursor.wstring();
  cursor.u8();
  skip_properties(cursor, object_format, cursor.u16(), false);
  return !name.empty();
}

struct FragmentBinding {
  std::uint32_t key{0};
  std::uint8_t unknown{0};
  std::string script;
  std::string function;
  std::size_t offset{0};
  std::size_t end{0};
};

struct ScenePhaseBinding {
  std::uint32_t index{0};
  std::uint8_t flag{0};
  std::uint8_t unknown{0};
  std::string script;
  std::string function;
  std::size_t offset{0};
  std::size_t end{0};
};

constexpr std::uint8_t k_scene_phase_flags = 0x03;

struct FragmentSection {
  Record* record{nullptr};
  Subrecord* vmad{nullptr};
  bool indexed{false};
  bool scene{false};
  std::size_t flags_offset{0};
  std::size_t count_offset{0};
  std::size_t entries_begin{0};
  std::size_t phase_count_offset{0};
  std::size_t phases_begin{0};
  std::uint8_t flags{0};
  std::vector<FragmentBinding> bindings;
  std::vector<ScenePhaseBinding> phases;
};

bool is_fragment_record(Signature signature) {
  return signature == sig_pack || signature == sig_info || signature == sig_scen ||
         signature == sig_perk || signature == sig_term;
}

unsigned fragment_flag_bits(Signature signature) {
  return signature == sig_pack ? 3u : 2u;
}

std::vector<FragmentBinding> read_flagged_bindings(Cursor& cursor, std::uint8_t flags,
                                                   unsigned bits) {
  unsigned count = 0;
  auto remaining = flags;
  for (unsigned i = 0; i < bits; ++i) {
    if ((remaining & 1u) != 0) ++count;
    remaining = static_cast<std::uint8_t>(remaining >> 1);
  }
  std::vector<FragmentBinding> bindings;
  bindings.reserve(count);
  unsigned key = 0;
  remaining = flags;
  for (unsigned i = 0; i < count; ++i) {
    while ((remaining & 1u) == 0) {
      remaining = static_cast<std::uint8_t>(remaining >> 1);
      ++key;
    }
    remaining = static_cast<std::uint8_t>(remaining >> 1);
    FragmentBinding binding;
    binding.key = key++;
    binding.offset = cursor.position();
    binding.unknown = cursor.u8();
    binding.script = cursor.wstring();
    binding.function = cursor.wstring();
    binding.end = cursor.position();
    bindings.push_back(std::move(binding));
  }
  return bindings;
}

const FragmentBinding* find_binding(const std::vector<FragmentBinding>& bindings,
                                    std::uint32_t key) {
  for (const auto& binding : bindings) {
    if (binding.key == key) return &binding;
  }
  return nullptr;
}

const ScenePhaseBinding* find_phase(const std::vector<ScenePhaseBinding>& phases,
                                    std::uint32_t index, std::uint8_t flag) {
  for (const auto& phase : phases) {
    if (phase.index == index && phase.flag == flag) return &phase;
  }
  return nullptr;
}

void append_u8(std::vector<std::byte>& out, std::uint8_t value) {
  out.push_back(static_cast<std::byte>(value));
}

void append_u16(std::vector<std::byte>& out, std::uint16_t value) {
  append_u8(out, static_cast<std::uint8_t>(value));
  append_u8(out, static_cast<std::uint8_t>(value >> 8));
}

void append_u32(std::vector<std::byte>& out, std::uint32_t value) {
  append_u16(out, static_cast<std::uint16_t>(value));
  append_u16(out, static_cast<std::uint16_t>(value >> 16));
}

void append_name(std::vector<std::byte>& out, std::string_view text) {
  if (text.size() > 0xFFFF) {
    throw schema::ScalarError("VMAD fragment name is too long");
  }
  append_u16(out, static_cast<std::uint16_t>(text.size()));
  for (const auto ch : text) {
    append_u8(out, static_cast<std::uint8_t>(ch));
  }
}

void splice(std::vector<std::byte>& data, std::size_t at, std::size_t count,
            std::span<const std::byte> replacement) {
  std::vector<std::byte> rebuilt;
  rebuilt.reserve(data.size() - count + replacement.size());
  rebuilt.insert(rebuilt.end(), data.begin(),
                 data.begin() + static_cast<std::ptrdiff_t>(at));
  rebuilt.insert(rebuilt.end(), replacement.begin(), replacement.end());
  rebuilt.insert(rebuilt.end(), data.begin() + static_cast<std::ptrdiff_t>(at + count),
                 data.end());
  data = std::move(rebuilt);
}

void put_u16(std::vector<std::byte>& data, std::size_t at, std::uint16_t value) {
  data[at] = static_cast<std::byte>(value);
  data[at + 1] = static_cast<std::byte>(value >> 8);
}

std::optional<VmadStructureMutationStatus> load_fragment_section(Plugin& plugin,
                                                                const FormKey& record_key,
                                                                FragmentSection& section) {
  section.record = detail::find_mutable_record(plugin, record_key);
  if (section.record == nullptr) {
    assign_record_form_keys(plugin);
    section.record = detail::find_mutable_record(plugin, record_key);
  }
  if (section.record == nullptr) return VmadStructureMutationStatus::record_missing;
  if (!is_fragment_record(section.record->header.signature)) {
    return VmadStructureMutationStatus::not_authorable;
  }
  for (auto& sub : section.record->subrecords) {
    if (sub.signature != k_vmad) continue;
    if (section.vmad != nullptr) throw schema::ScalarError("duplicate VMAD subrecords");
    section.vmad = &sub;
  }
  if (section.vmad == nullptr) return VmadStructureMutationStatus::subrecord_missing;
  const auto signature = section.record->header.signature;
  if (!vmad_coverage(section.vmad->data, signature).complete()) {
    throw schema::ScalarError("VMAD layout is unsupported, malformed or has unknown trailing bytes");
  }
  const auto scripts = read_vmad_scripts(section.vmad->data);
  if (scripts.unsupported) {
    throw schema::ScalarError("VMAD has properties that cannot be re-encoded");
  }
  if ((scripts.version != 5 && scripts.version != 6) ||
      (scripts.object_format != 1 && scripts.object_format != 2)) {
    throw schema::ScalarError("unsupported VMAD version or object format");
  }
  const auto extras_start = section.vmad->data.size() - scripts.trailing_bytes;
  if (extras_start == section.vmad->data.size()) {
    return VmadStructureMutationStatus::binding_missing;
  }
  Cursor cursor(std::span<const std::byte>(section.vmad->data).subspan(extras_start));
  cursor.u8();
  section.indexed = signature == sig_perk || signature == sig_term;
  if (section.indexed) {
    if (!skip_script_entry(cursor, scripts.object_format)) {
      return VmadStructureMutationStatus::binding_missing;
    }
    section.count_offset = extras_start + cursor.position();
    const auto count = cursor.u16();
    section.entries_begin = extras_start + cursor.position();
    section.bindings.reserve(count);
    for (std::uint16_t i = 0; i < count; ++i) {
      FragmentBinding binding;
      binding.offset = extras_start + cursor.position();
      binding.key = cursor.u32();
      binding.unknown = cursor.u8();
      binding.script = cursor.wstring();
      binding.function = cursor.wstring();
      binding.end = extras_start + cursor.position();
      section.bindings.push_back(std::move(binding));
    }
    return std::nullopt;
  }
  section.flags_offset = extras_start + 1;
  section.flags = cursor.u8();
  if (!skip_script_entry(cursor, scripts.object_format)) {
    return VmadStructureMutationStatus::binding_missing;
  }
  section.entries_begin = extras_start + cursor.position();
  for (auto& binding : read_flagged_bindings(cursor, section.flags,
                                            fragment_flag_bits(signature))) {
    binding.offset += extras_start;
    binding.end += extras_start;
    section.bindings.push_back(std::move(binding));
  }
  if (signature != sig_scen) return std::nullopt;
  section.scene = true;
  section.phase_count_offset = extras_start + cursor.position();
  const auto phases = cursor.u16();
  section.phases_begin = extras_start + cursor.position();
  section.phases.reserve(phases);
  for (std::uint16_t i = 0; i < phases; ++i) {
    ScenePhaseBinding phase;
    phase.offset = extras_start + cursor.position();
    phase.flag = cursor.u8();
    phase.index = cursor.u32();
    phase.unknown = cursor.u8();
    phase.script = cursor.wstring();
    phase.function = cursor.wstring();
    phase.end = extras_start + cursor.position();
    section.phases.push_back(std::move(phase));
  }
  return std::nullopt;
}

std::vector<std::byte> encode_binding(const FragmentBinding& binding, bool indexed) {
  std::vector<std::byte> entry;
  if (indexed) {
    append_u32(entry, binding.key);
  }
  append_u8(entry, binding.unknown);
  append_name(entry, binding.script);
  append_name(entry, binding.function);
  return entry;
}

std::vector<std::byte> encode_phase(const ScenePhaseBinding& phase) {
  std::vector<std::byte> entry;
  append_u8(entry, phase.flag);
  append_u32(entry, phase.index);
  append_u8(entry, phase.unknown);
  append_name(entry, phase.script);
  append_name(entry, phase.function);
  return entry;
}

}  // namespace

VmadStructureMutationStatus add_vmad_fragment(Plugin& plugin, const FormKey& record_key,
                                              std::uint32_t key, std::string_view script,
                                              std::string_view function) {
  FragmentSection section;
  if (const auto refusal = load_fragment_section(plugin, record_key, section)) return *refusal;
  if (script.empty() || function.empty()) {
    throw schema::ScalarError("VMAD fragment script and function names are required");
  }
  if (!section.indexed && key >= fragment_flag_bits(section.record->header.signature)) {
    return VmadStructureMutationStatus::index_out_of_range;
  }
  if (find_binding(section.bindings, key) != nullptr) {
    return VmadStructureMutationStatus::already_present;
  }
  if (section.bindings.size() >= 0xFFFFu) {
    throw schema::ScalarError("VMAD fragment list is full");
  }
  FragmentBinding binding;
  binding.key = key;
  binding.script = std::string(script);
  binding.function = std::string(function);
  const auto entry = encode_binding(binding, section.indexed);
  auto at = section.entries_begin;
  for (const auto& existing : section.bindings) {
    if (existing.key > key) {
      at = existing.offset;
      break;
    }
    at = existing.end;
  }
  splice(section.vmad->data, at, 0, entry);
  if (section.indexed) {
    put_u16(section.vmad->data, section.count_offset,
            static_cast<std::uint16_t>(section.bindings.size() + 1));
  } else {
    section.vmad->data[section.flags_offset] =
        static_cast<std::byte>(section.flags | (1u << key));
  }
  detail::mark_record_modified(plugin, *section.record);
  return VmadStructureMutationStatus::updated;
}

VmadStructureMutationStatus set_vmad_fragment(Plugin& plugin, const FormKey& record_key,
                                              std::uint32_t key, std::string_view script,
                                              std::string_view function) {
  FragmentSection section;
  if (const auto refusal = load_fragment_section(plugin, record_key, section)) return *refusal;
  if (script.empty() || function.empty()) {
    throw schema::ScalarError("VMAD fragment script and function names are required");
  }
  const auto* found = find_binding(section.bindings, key);
  if (found == nullptr) return VmadStructureMutationStatus::binding_missing;
  auto updated = *found;
  updated.script = std::string(script);
  updated.function = std::string(function);
  const auto entry = encode_binding(updated, section.indexed);
  if (entry.size() == found->end - found->offset &&
      std::equal(entry.begin(), entry.end(),
                 section.vmad->data.begin() + static_cast<std::ptrdiff_t>(found->offset))) {
    return VmadStructureMutationStatus::unchanged;
  }
  splice(section.vmad->data, found->offset, found->end - found->offset, entry);
  detail::mark_record_modified(plugin, *section.record);
  return VmadStructureMutationStatus::updated;
}

VmadStructureMutationStatus remove_vmad_fragment(Plugin& plugin, const FormKey& record_key,
                                                 std::uint32_t key) {
  FragmentSection section;
  if (const auto refusal = load_fragment_section(plugin, record_key, section)) return *refusal;
  const auto* found = find_binding(section.bindings, key);
  if (found == nullptr) return VmadStructureMutationStatus::binding_missing;
  splice(section.vmad->data, found->offset, found->end - found->offset, {});
  if (section.indexed) {
    put_u16(section.vmad->data, section.count_offset,
            static_cast<std::uint16_t>(section.bindings.size() - 1));
  } else if (key < 8) {
    section.vmad->data[section.flags_offset] =
        static_cast<std::byte>(section.flags & ~(1u << key));
  }
  detail::mark_record_modified(plugin, *section.record);
  return VmadStructureMutationStatus::updated;
}

VmadStructureMutationStatus add_scene_vmad_phase_fragment(Plugin& plugin, const FormKey& record_key,
                                                          std::uint32_t phase_index,
                                                          std::uint8_t phase_flag,
                                                          std::string_view script,
                                                          std::string_view function) {
  FragmentSection section;
  if (const auto refusal = load_fragment_section(plugin, record_key, section)) return *refusal;
  if (!section.scene) return VmadStructureMutationStatus::not_authorable;
  if (script.empty() || function.empty()) {
    throw schema::ScalarError("VMAD fragment script and function names are required");
  }
  if (phase_flag == 0 || (phase_flag & ~k_scene_phase_flags) != 0) {
    return VmadStructureMutationStatus::index_out_of_range;
  }
  if (find_phase(section.phases, phase_index, phase_flag) != nullptr) {
    return VmadStructureMutationStatus::already_present;
  }
  if (section.phases.size() >= 0xFFFFu) {
    throw schema::ScalarError("VMAD fragment list is full");
  }
  ScenePhaseBinding phase;
  phase.index = phase_index;
  phase.flag = phase_flag;
  phase.script = std::string(script);
  phase.function = std::string(function);
  const auto entry = encode_phase(phase);
  auto at = section.phases_begin;
  for (const auto& existing : section.phases) {
    if (existing.index > phase_index ||
        (existing.index == phase_index && existing.flag > phase_flag)) {
      at = existing.offset;
      break;
    }
    at = existing.end;
  }
  splice(section.vmad->data, at, 0, entry);
  put_u16(section.vmad->data, section.phase_count_offset,
          static_cast<std::uint16_t>(section.phases.size() + 1));
  detail::mark_record_modified(plugin, *section.record);
  return VmadStructureMutationStatus::updated;
}

VmadStructureMutationStatus set_scene_vmad_phase_fragment(Plugin& plugin, const FormKey& record_key,
                                                          std::uint32_t phase_index,
                                                          std::uint8_t phase_flag,
                                                          std::string_view script,
                                                          std::string_view function) {
  FragmentSection section;
  if (const auto refusal = load_fragment_section(plugin, record_key, section)) return *refusal;
  if (!section.scene) return VmadStructureMutationStatus::not_authorable;
  if (script.empty() || function.empty()) {
    throw schema::ScalarError("VMAD fragment script and function names are required");
  }
  const auto* found = find_phase(section.phases, phase_index, phase_flag);
  if (found == nullptr) return VmadStructureMutationStatus::binding_missing;
  auto updated = *found;
  updated.script = std::string(script);
  updated.function = std::string(function);
  const auto entry = encode_phase(updated);
  if (entry.size() == found->end - found->offset &&
      std::equal(entry.begin(), entry.end(),
                 section.vmad->data.begin() + static_cast<std::ptrdiff_t>(found->offset))) {
    return VmadStructureMutationStatus::unchanged;
  }
  splice(section.vmad->data, found->offset, found->end - found->offset, entry);
  detail::mark_record_modified(plugin, *section.record);
  return VmadStructureMutationStatus::updated;
}

VmadStructureMutationStatus remove_scene_vmad_phase_fragment(Plugin& plugin,
                                                             const FormKey& record_key,
                                                             std::uint32_t phase_index,
                                                             std::uint8_t phase_flag) {
  FragmentSection section;
  if (const auto refusal = load_fragment_section(plugin, record_key, section)) return *refusal;
  if (!section.scene) return VmadStructureMutationStatus::not_authorable;
  const auto* found = find_phase(section.phases, phase_index, phase_flag);
  if (found == nullptr) return VmadStructureMutationStatus::binding_missing;
  splice(section.vmad->data, found->offset, found->end - found->offset, {});
  put_u16(section.vmad->data, section.phase_count_offset,
          static_cast<std::uint16_t>(section.phases.size() - 1));
  detail::mark_record_modified(plugin, *section.record);
  return VmadStructureMutationStatus::updated;
}

}  // namespace fo4
