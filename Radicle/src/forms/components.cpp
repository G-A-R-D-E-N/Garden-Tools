#include "fo4/forms/components.hpp"

#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "../plugin/record_edit.hpp"

#include <array>
#include <cstring>
#include <optional>
#include <string>
#include <utility>

namespace fo4::forms {
namespace {

constexpr auto k_misc = Signature::from_string("MISC");
constexpr auto k_cobj = Signature::from_string("COBJ");
constexpr auto k_cvpa = Signature::from_string("CVPA");
constexpr auto k_fvpa = Signature::from_string("FVPA");
constexpr auto k_cdix = Signature::from_string("CDIX");

constexpr Signature k_misc_predecessors[] = {
    Signature::from_string("DATA"), Signature::from_string("FIMD"),
    Signature::from_string("KWDA"), Signature::from_string("KSIZ"),
    Signature::from_string("EDID"),
};
constexpr Signature k_cobj_predecessors[] = {
    Signature::from_string("ZNAM"), Signature::from_string("YNAM"),
    Signature::from_string("EDID"),
};
constexpr Signature k_cobj_successors[] = {
    Signature::from_string("DESC"), Signature::from_string("CTDA"),
    Signature::from_string("CNAM"), Signature::from_string("BNAM"),
    Signature::from_string("NAM1"), Signature::from_string("NAM2"),
    Signature::from_string("NAM3"), Signature::from_string("ANAM"),
    Signature::from_string("FNAM"), Signature::from_string("INTV"),
};

struct ComponentLayout {
  Signature array{};
  bool has_display_indices{false};
  std::span<const Signature> predecessors{};
  std::span<const Signature> successors{};
  bool append_when_empty{false};
};

std::optional<ComponentLayout> layout_for(Signature record) {
  if (record == k_misc) {
    return ComponentLayout{k_cvpa, true, k_misc_predecessors, {}, true};
  }
  if (record == k_cobj) {
    return ComponentLayout{k_fvpa, false, k_cobj_predecessors,
                           k_cobj_successors, false};
  }
  return std::nullopt;
}

std::uint32_t read_u32(const std::vector<std::byte>& data, std::size_t offset) {
  std::uint32_t value = 0;
  std::memcpy(&value, data.data() + offset, sizeof(value));
  return value;
}

using fo4::detail::find_mutable_record;

void mark_modified(Plugin& plugin, Record& record) {
  record.original_range.clear();
  record.decompressed_payload.clear();
  record.header.data_size = 0;
  record.uncompressed_size = 0;
  plugin.original_range.clear();
}

std::optional<std::size_t> canonical_insert_index(
    const std::vector<Subrecord>& subrecords, const ComponentLayout& layout) {
  for (const auto predecessor : layout.predecessors) {
    std::optional<std::size_t> last;
    for (std::size_t index = 0; index < subrecords.size(); ++index) {
      if (subrecords[index].signature == predecessor) last = index;
    }
    if (last.has_value()) return *last + 1;
  }
  for (const auto successor : layout.successors) {
    for (std::size_t index = 0; index < subrecords.size(); ++index) {
      if (subrecords[index].signature == successor) return index;
    }
  }
  if (layout.append_when_empty) return subrecords.size();
  return std::nullopt;
}

}  // namespace

bool record_supports_components(Signature record) noexcept {
  return layout_for(record).has_value();
}

std::vector<ComponentEntry> decode_components(const Plugin& plugin,
                                              const FormKey& key) {
  const auto* record = find_record(plugin, key);
  if (record == nullptr) {
    throw ComponentError("record not found: " + key.to_string());
  }
  const auto layout = layout_for(record->header.signature);
  if (!layout.has_value()) {
    throw ComponentError("record does not carry components: " +
                         record->header.signature.to_string());
  }

  std::vector<ComponentEntry> result;
  const auto context = plugin_form_id_context(plugin);
  for (const auto& subrecord : record->subrecords) {
    if (subrecord.signature != layout->array) continue;
    if (subrecord.data.size() % 8 != 0) {
      throw ComponentError(layout->array.to_string() +
                           " length must be a multiple of eight bytes");
    }
    for (std::size_t offset = 0; offset + 8 <= subrecord.data.size(); offset += 8) {
      ComponentEntry entry;
      const auto raw = RawFormId{read_u32(subrecord.data, offset)};
      if (!raw.is_null()) {
        try {
          entry.component = resolve_plugin_form_id(raw, context);
        } catch (const FormIdError& error) {
          throw ComponentError(std::string("invalid component FormID: ") +
                               error.what());
        }
      }
      entry.count = read_u32(subrecord.data, offset + 4);
      result.push_back(std::move(entry));
    }
  }
  return result;
}

std::size_t set_record_components(Plugin& plugin,
                                  const FormKey& key,
                                  std::span<const ComponentEntry> entries) {
  assign_record_form_keys(plugin);
  auto* record = find_mutable_record(plugin, key);
  if (record == nullptr) {
    throw ComponentError("record not found: " + key.to_string());
  }
  const auto layout = layout_for(record->header.signature);
  if (!layout.has_value()) {
    throw ComponentError(record->header.signature.to_string() +
                         " does not carry components");
  }
  if (layout->has_display_indices && entries.size() > 0x100u) {
    throw ComponentError("CDIX cannot index more than 256 components");
  }

  const auto context = plugin_form_id_context(plugin);
  std::vector<std::byte> array_bytes;
  array_bytes.reserve(entries.size() * 8);
  for (const auto& entry : entries) {
    if (entry.component.owner().empty()) {
      throw ComponentError("component entry must reference a FormKey");
    }
    RawFormId raw{};
    try {
      raw = encode_plugin_form_id(entry.component, context);
    } catch (const FormIdError& error) {
      throw ComponentError(std::string("cannot encode component FormKey: ") +
                           error.what());
    }
    std::array<std::byte, 8> block{};
    const std::uint32_t id = raw.value();
    std::memcpy(block.data(), &id, sizeof(id));
    std::memcpy(block.data() + 4, &entry.count, sizeof(entry.count));
    array_bytes.insert(array_bytes.end(), block.begin(), block.end());
  }

  std::optional<std::size_t> array_pos;
  std::vector<Subrecord> next;
  next.reserve(record->subrecords.size() + 2);
  for (const auto& subrecord : record->subrecords) {
    if (subrecord.signature == layout->array) {
      if (!array_pos.has_value()) array_pos = next.size();
      continue;
    }
    if (layout->has_display_indices && subrecord.signature == k_cdix) continue;
    next.push_back(subrecord);
  }

  if (!entries.empty()) {
    Subrecord array_subrecord;
    array_subrecord.signature = layout->array;
    array_subrecord.data = std::move(array_bytes);
    std::size_t at = 0;
    if (array_pos.has_value()) {
      at = *array_pos;
    } else {
      const auto anchored = canonical_insert_index(next, *layout);
      if (!anchored.has_value()) {
        throw ComponentError("cannot determine the canonical position for " +
                             layout->array.to_string() + " on a " +
                             record->header.signature.to_string() + " with no anchor field");
      }
      at = *anchored;
    }
    if (at > next.size()) at = next.size();
    next.insert(next.begin() + static_cast<std::ptrdiff_t>(at),
                std::move(array_subrecord));
    if (layout->has_display_indices) {
      Subrecord display;
      display.signature = k_cdix;
      display.data.resize(entries.size());
      for (std::size_t index = 0; index < entries.size(); ++index) {
        display.data[index] = std::byte{static_cast<std::uint8_t>(index)};
      }
      next.insert(next.begin() + static_cast<std::ptrdiff_t>(at + 1),
                  std::move(display));
    }
  }

  record->subrecords = std::move(next);
  mark_modified(plugin, *record);
  return entries.size();
}

}  // namespace fo4::forms
