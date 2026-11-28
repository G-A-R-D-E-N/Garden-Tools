#include "fo4/forms/object_template.hpp"

#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/record_edit.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <utility>
namespace fo4::forms {
namespace {

enum class Row { keyword, include, property_form, read, include_options, property_value, structure, insert, property_replace, rank_tier };

std::uint32_t read_u32(const std::vector<std::byte>& bytes, std::size_t pos) {
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    value |= std::uint32_t{std::to_integer<std::uint8_t>(bytes[pos + i])} << (i * 8);
  }
  return value;
}

bool encode_property(const Plugin& plugin, const ObjectTemplateProperty& prop,
                     std::vector<std::byte>& bytes) {
  const auto type = prop.value_type;
  if (!(type == 0 || type == 1 || type == 2 || type == 3 || type == 4 || type == 5 || type == 6) ||
      prop.function_type > 2 || (type == 5 && prop.function_type != 0) ||
      ((type == 3 || type == 5) && prop.value2 != read_u32(bytes, 16)) ||
      (type == 2 && (prop.value1 > 1 || prop.value2 > 1)) ||
      ((type == 4 || type == 6) != prop.form_value.has_value())) return false;
  std::uint32_t first = prop.value1;
  if (prop.form_value) {
    try {
      first = encode_plugin_form_id(*prop.form_value, plugin_form_id_context(plugin)).value();
    } catch (const FormIdError&) { return false; }
  }
  bytes[0] = std::byte(type);
  bytes[4] = std::byte(prop.function_type);
  bytes[8] = std::byte(prop.property & 0xff);
  bytes[9] = std::byte(prop.property >> 8);
  const auto put = [&](std::size_t pos, std::uint32_t value) {
    for (std::size_t i = 0; i < 4; ++i) bytes[pos + i] = std::byte((value >> (i * 8)) & 0xff);
  };
  put(12, first);
  if (type != 3 && type != 5) put(16, prop.value2);
  put(20, prop.step_bits);
  return true;
}

ObjectTemplateWrite edit(Plugin& plugin, const FormKey& key, std::size_t combination,
                         std::size_t index, const FormKey& target, Row row,
                         ObjectTemplateRows* out = nullptr, std::uint8_t part = 0,
                         std::uint32_t value = 0, std::uint8_t option = 0,
                         std::uint8_t dont_use_all = 0, std::size_t destination = 0,
                         const ObjectTemplateInsertRow* input = nullptr,
                         const ObjectTemplateProperty* replacement = nullptr) {
  auto* record = detail::find_mutable_record(plugin, key);
  if (record == nullptr) return ObjectTemplateWrite::record_missing;
  require_decodable(*record);
  const auto signature = record->header.signature.to_string();
  if (signature != "ARMO" && signature != "WEAP" && signature != "NPC_" && signature != "FURN") {
    return ObjectTemplateWrite::unsupported_record;
  }
  const auto obte = Signature::from_string("OBTE");
  const auto obts = Signature::from_string("OBTS");
  const auto stop = Signature::from_string("STOP");
  std::size_t start = record->subrecords.size();
  for (std::size_t i = 0; i < record->subrecords.size(); ++i) {
    if (record->subrecords[i].signature != obte) continue;
    if (start != record->subrecords.size()) return ObjectTemplateWrite::malformed;
    start = i;
  }
  if (start == record->subrecords.size()) return ObjectTemplateWrite::combination_missing;
  const auto& count_data = record->subrecords[start].data;
  if (count_data.size() != 4) return ObjectTemplateWrite::malformed;
  const auto combination_count = read_u32(count_data, 0);
  Subrecord* selected = nullptr;
  std::size_t seen = 0;
  bool ended = false;
  bool has_obtf = false;
  bool has_full = false;
  for (std::size_t i = start + 1; i < record->subrecords.size(); ++i) {
    auto& sub = record->subrecords[i];
    if (sub.signature == stop) {
      if (!sub.data.empty() || (combination_count != 1 && (has_obtf || has_full))) return ObjectTemplateWrite::malformed;
      ended = true;
      break;
    }
    if (sub.signature == obts) {
      if (seen == combination) selected = &sub;
      ++seen;
      has_obtf = false;
      has_full = false;
    } else if (sub.signature == Signature::from_string("OBTF")) {
      // A marker after an OBTS must head the next combination.
      if (has_obtf || !sub.data.empty()) return ObjectTemplateWrite::malformed;
      has_obtf = true;
    } else if (sub.signature == Signature::from_string("FULL")) {
      if (has_full) return ObjectTemplateWrite::malformed;
      has_full = true;
    } else {
      return ObjectTemplateWrite::malformed;
    }
  }
  if (!ended || seen != combination_count) return ObjectTemplateWrite::malformed;
  if (selected == nullptr) return ObjectTemplateWrite::combination_missing;
  if (row == Row::include && target.owner().empty() && target.local().value() == 0) {
    return ObjectTemplateWrite::invalid_value;
  }
  if ((row == Row::keyword || row == Row::include) &&
      !target.owner().empty() && target.owner() == plugin.key) {
    const auto* referenced = find_record(plugin, target);
    const auto required = Signature::from_string(row == Row::keyword ? "KYWD" : "OMOD");
    if (referenced == nullptr || referenced->header.signature != required) {
      return ObjectTemplateWrite::invalid_value;
    }
  }
  auto& bytes = selected->data;
  if (bytes.size() < 18) return ObjectTemplateWrite::malformed;
  const auto includes = read_u32(bytes, 0);
  const auto properties = read_u32(bytes, 4);
  const auto keywords = std::to_integer<std::uint8_t>(bytes[15]);
  const auto include_base = std::size_t{18} + std::size_t{keywords} * 4;
  if (include_base > bytes.size() || includes > (bytes.size() - include_base) / 7) {
    return ObjectTemplateWrite::malformed;
  }
  const auto property_base = include_base + std::size_t{includes} * 7;
  if (properties > (bytes.size() - property_base) / 24 ||
      property_base + std::size_t{properties} * 24 != bytes.size()) {
    return ObjectTemplateWrite::malformed;
  }
  if (row == Row::read) {
    ObjectTemplateRows result;
    result.min_level_for_ranks = std::to_integer<std::uint8_t>(bytes[include_base - 2]);
    result.alt_levels_per_tier = std::to_integer<std::uint8_t>(bytes[include_base - 1]);
    const auto context = plugin_form_id_context(plugin);
    try {
      for (std::size_t i = 0; i < keywords; ++i) {
        result.keywords.push_back(resolve_plugin_form_id(RawFormId{read_u32(bytes, 16 + i * 4)}, context));
      }
      for (std::size_t i = 0; i < includes; ++i) {
        const auto base = include_base + i * 7;
        result.includes.push_back({resolve_plugin_form_id(RawFormId{read_u32(bytes, base)}, context),
            std::to_integer<std::uint8_t>(bytes[base + 4]),
            std::to_integer<std::uint8_t>(bytes[base + 5]),
            std::to_integer<std::uint8_t>(bytes[base + 6])});
      }
      for (std::size_t i = 0; i < properties; ++i) {
        const auto base = property_base + i * 24;
        const auto type = std::to_integer<std::uint8_t>(bytes[base]);
        const auto property = static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes[base + 8]) |
            (std::uint16_t{std::to_integer<std::uint8_t>(bytes[base + 9])} << 8));
        ObjectTemplateProperty entry{type, std::to_integer<std::uint8_t>(bytes[base + 4]), property,
            read_u32(bytes, base + 12), read_u32(bytes, base + 16), read_u32(bytes, base + 20), std::nullopt};
        if (type == 4 || type == 6) {
          entry.form_value = resolve_plugin_form_id(RawFormId{entry.value1}, context);
        }
        result.properties.push_back(std::move(entry));
      }
    } catch (const FormIdError&) {
      return ObjectTemplateWrite::invalid_value;
    }
    *out = std::move(result);
    return ObjectTemplateWrite::updated;
  }
  if (row == Row::rank_tier) {
    bytes[include_base - 2] = std::byte(part);
    bytes[include_base - 1] = std::byte(option);
    detail::mark_record_modified(plugin, *record);
    return ObjectTemplateWrite::updated;
  }
  if (row == Row::property_replace) {
    if (!replacement) return ObjectTemplateWrite::invalid_value;
    if (index >= properties) return ObjectTemplateWrite::index_out_of_range;
    const auto offset = property_base + index * 24;
    std::vector<std::byte> next(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                                bytes.begin() + static_cast<std::ptrdiff_t>(offset + 24));
    if (!encode_property(plugin, *replacement, next)) return ObjectTemplateWrite::invalid_value;
    std::copy(next.begin(), next.end(), bytes.begin() + static_cast<std::ptrdiff_t>(offset));
    detail::mark_record_modified(plugin, *record);
    return ObjectTemplateWrite::updated;
  }
  if (row == Row::insert) {
    if (part > 2 || input == nullptr) return ObjectTemplateWrite::invalid_value;
    const std::size_t count = part == 0 ? keywords : part == 1 ? includes : properties;
    const std::size_t base = part == 0 ? 16 : part == 1 ? include_base : property_base;
    const std::size_t width = part == 0 ? 4 : part == 1 ? 7 : 24;
    if (index > count) return ObjectTemplateWrite::index_out_of_range;
    if ((part == 0 && count == 255) || count == std::numeric_limits<std::uint32_t>::max() ||
        bytes.size() > std::numeric_limits<std::uint32_t>::max() - width) {
      return ObjectTemplateWrite::invalid_value;
    }
    std::vector<std::byte> inserted(width, std::byte{0});
    auto put = [&](std::size_t pos, std::uint32_t data) {
      for (std::size_t i = 0; i < 4; ++i) inserted[pos + i] = std::byte((data >> (i * 8)) & 0xff);
    };
    auto encode = [&](const FormKey& link, bool required, bool check_type) -> bool {
      if (check_type && !link.owner().empty() && link.owner() == plugin.key) {
        const auto* local = find_record(plugin, link);
        const auto expected = Signature::from_string(part == 0 ? "KYWD" : "OMOD");
        if (!local || local->header.signature != expected) return false;
      }
      try {
        const auto raw = encode_plugin_form_id(link, plugin_form_id_context(plugin)).value();
        if (required && raw == 0) return false;
        put(part == 2 ? 12 : 0, raw);
        return true;
      } catch (const FormIdError&) {
        return false;
      }
    };
    if (part < 2) {
      if (!encode(input->link, part == 1, true)) return ObjectTemplateWrite::invalid_value;
      if (part == 1) {
        if (input->optional > 1 || input->dont_use_all > 1) return ObjectTemplateWrite::invalid_value;
        inserted[4] = std::byte(input->attach_point);
        inserted[5] = std::byte(input->optional);
        inserted[6] = std::byte(input->dont_use_all);
      }
    } else if (!encode_property(plugin, input->property, inserted)) {
      return ObjectTemplateWrite::invalid_value;
    }
    auto next = bytes;
    next.insert(next.begin() + static_cast<std::ptrdiff_t>(base + index * width), inserted.begin(), inserted.end());
    if (part == 0) next[15] = std::byte(count + 1);
    else {
      const auto count_offset = part == 1 ? 0u : 4u;
      for (std::size_t i = 0; i < 4; ++i) next[count_offset + i] = std::byte(((count + 1) >> (i * 8)) & 0xff);
    }
    bytes = std::move(next);
    detail::mark_record_modified(plugin, *record);
    return ObjectTemplateWrite::updated;
  }
  if (row == Row::structure) {
    if (part > 2 || option > 2) return ObjectTemplateWrite::invalid_value;
    const std::size_t count = part == 0 ? keywords : part == 1 ? includes : properties;
    const std::size_t base = part == 0 ? 16 : part == 1 ? include_base : property_base;
    const std::size_t width = part == 0 ? 4 : part == 1 ? 7 : 24;
    if (index >= count || (option == 0 && destination > count) ||
        (option == 2 && destination >= count)) return ObjectTemplateWrite::index_out_of_range;
    if (option == 0 && ((part == 0 && count == 255) ||
        count == std::numeric_limits<std::uint32_t>::max() ||
        bytes.size() > std::numeric_limits<std::uint32_t>::max() - width)) {
      return ObjectTemplateWrite::invalid_value;
    }
    auto next = bytes;
    const auto begin = next.begin() + static_cast<std::ptrdiff_t>(base + index * width);
    if (option == 0) {
      const std::vector<std::byte> copied(begin, begin + static_cast<std::ptrdiff_t>(width));
      next.insert(next.begin() + static_cast<std::ptrdiff_t>(base + destination * width), copied.begin(), copied.end());
    } else if (option == 1) {
      next.erase(begin, begin + static_cast<std::ptrdiff_t>(width));
    } else if (index < destination) {
      std::rotate(begin, begin + static_cast<std::ptrdiff_t>(width),
                  next.begin() + static_cast<std::ptrdiff_t>(base + (destination + 1) * width));
    } else if (index > destination) {
      std::rotate(next.begin() + static_cast<std::ptrdiff_t>(base + destination * width), begin,
                  begin + static_cast<std::ptrdiff_t>(width));
    }
    const auto next_count = option == 0 ? count + 1 : option == 1 ? count - 1 : count;
    if (part == 0) next[15] = std::byte(next_count);
    else {
      const auto count_offset = part == 1 ? 0u : 4u;
      for (std::size_t i = 0; i < 4; ++i) next[count_offset + i] = std::byte((next_count >> (i * 8)) & 0xff);
    }
    bytes = std::move(next);
    detail::mark_record_modified(plugin, *record);
    return ObjectTemplateWrite::updated;
  }
  std::size_t offset = 0;
  if (row == Row::keyword) {
    if (index >= keywords) return ObjectTemplateWrite::index_out_of_range;
    offset = 16 + index * 4;
  } else if (row == Row::include || row == Row::include_options) {
    if (index >= includes) return ObjectTemplateWrite::index_out_of_range;
    offset = include_base + index * 7;
  } else {
    if (index >= properties) return ObjectTemplateWrite::index_out_of_range;
    offset = property_base + index * 24;
    const auto type = std::to_integer<std::uint8_t>(bytes[offset]);
    if (row == Row::property_form) {
      if (type != 4 && type != 6) return ObjectTemplateWrite::invalid_value;
      offset += 12;
    } else {
      const bool known = type == 0 || type == 1 || type == 2 || type == 3 || type == 4 || type == 5 || type == 6;
      const bool allowed = part == 0 ? (type == 0 || type == 1 || type == 2 || type == 3 || type == 5)
                         : part == 1 ? (type == 0 || type == 1 || type == 2 || type == 4 || type == 6)
                                     : part == 2 && known;
      if (!allowed || (part != 2 && type == 2 && value > 1)) return ObjectTemplateWrite::invalid_value;
      offset += 12 + part * 4;
    }
  }
  if (row == Row::include_options) {
    if (option > 1 || dont_use_all > 1 || value > 255) return ObjectTemplateWrite::invalid_value;
    bytes[offset + 4] = std::byte(value);
    bytes[offset + 5] = std::byte(option);
    bytes[offset + 6] = std::byte(dont_use_all);
    detail::mark_record_modified(plugin, *record);
    return ObjectTemplateWrite::updated;
  }
  if (row == Row::property_value) {
    for (std::size_t i = 0; i < 4; ++i) bytes[offset + i] = std::byte((value >> (i * 8)) & 0xff);
    detail::mark_record_modified(plugin, *record);
    return ObjectTemplateWrite::updated;
  }
  std::uint32_t raw = 0;
  try {
    raw = encode_plugin_form_id(target, plugin_form_id_context(plugin)).value();
  } catch (const FormIdError&) {
    return ObjectTemplateWrite::invalid_value;
  }
  if (row == Row::include && raw == 0) return ObjectTemplateWrite::invalid_value;
  for (std::size_t i = 0; i < 4; ++i) {
    bytes[offset + i] = std::byte((raw >> (i * 8)) & 0xff);
  }
  detail::mark_record_modified(plugin, *record);
  return ObjectTemplateWrite::updated;
}

}  // namespace

ObjectTemplateWrite set_object_template_property(Plugin& plugin, const FormKey& record,
    std::size_t combination, std::size_t index, const ObjectTemplateProperty& property) {
  return edit(plugin, record, combination, index, FormKey{}, Row::property_replace,
              nullptr, 0, 0, 0, 0, 0, nullptr, &property);
}

ObjectTemplateWrite insert_object_template_row(Plugin& plugin, const FormKey& record,
    std::size_t combination, ObjectTemplateRowKind kind, std::size_t index,
    const ObjectTemplateInsertRow& row) {
  if (kind != ObjectTemplateRowKind::keyword && kind != ObjectTemplateRowKind::include &&
      kind != ObjectTemplateRowKind::property) return ObjectTemplateWrite::invalid_value;
  return edit(plugin, record, combination, index, FormKey{}, Row::insert, nullptr,
              static_cast<std::uint8_t>(kind), 0, 0, 0, 0, &row);
}

ObjectTemplateWrite clone_object_template_row(Plugin& plugin, const FormKey& record,
    std::size_t combination, ObjectTemplateRowKind kind, std::size_t source, std::size_t destination) {
  if (kind != ObjectTemplateRowKind::keyword && kind != ObjectTemplateRowKind::include &&
      kind != ObjectTemplateRowKind::property) return ObjectTemplateWrite::invalid_value;
  return edit(plugin, record, combination, source, FormKey{}, Row::structure, nullptr,
              static_cast<std::uint8_t>(kind), 0, 0, 0, destination);
}

ObjectTemplateWrite remove_object_template_row(Plugin& plugin, const FormKey& record,
    std::size_t combination, ObjectTemplateRowKind kind, std::size_t index) {
  if (kind != ObjectTemplateRowKind::keyword && kind != ObjectTemplateRowKind::include &&
      kind != ObjectTemplateRowKind::property) return ObjectTemplateWrite::invalid_value;
  return edit(plugin, record, combination, index, FormKey{}, Row::structure, nullptr,
              static_cast<std::uint8_t>(kind), 0, 1);
}

ObjectTemplateWrite move_object_template_row(Plugin& plugin, const FormKey& record,
    std::size_t combination, ObjectTemplateRowKind kind, std::size_t source, std::size_t destination) {
  if (kind != ObjectTemplateRowKind::keyword && kind != ObjectTemplateRowKind::include &&
      kind != ObjectTemplateRowKind::property) return ObjectTemplateWrite::invalid_value;
  return edit(plugin, record, combination, source, FormKey{}, Row::structure, nullptr,
              static_cast<std::uint8_t>(kind), 0, 2, 0, destination);
}

ObjectTemplateWrite read_object_template_rows(Plugin& plugin, const FormKey& record,
                                              std::size_t combination, ObjectTemplateRows& out) {
  return edit(plugin, record, combination, 0, FormKey{}, Row::read, &out);
}

ObjectTemplateWrite set_object_template_rank_tier(Plugin& plugin, const FormKey& record,
    std::size_t combination, std::uint8_t min_level_for_ranks, std::uint8_t alt_levels_per_tier) {
  return edit(plugin, record, combination, 0, FormKey{}, Row::rank_tier, nullptr,
              min_level_for_ranks, 0, alt_levels_per_tier);
}

ObjectTemplateWrite set_object_template_include_options(Plugin& plugin, const FormKey& record,
    std::size_t combination, std::size_t index, std::uint8_t attach_point,
    std::uint8_t optional, std::uint8_t dont_use_all) {
  return edit(plugin, record, combination, index, FormKey{}, Row::include_options,
              nullptr, 0, attach_point, optional, dont_use_all);
}

ObjectTemplateWrite set_object_template_property_value(Plugin& plugin, const FormKey& record,
    std::size_t combination, std::size_t index, std::uint8_t part, std::uint32_t value) {
  return edit(plugin, record, combination, index, FormKey{}, Row::property_value, nullptr, part, value);
}

ObjectTemplateWrite set_object_template_keyword(Plugin& plugin, const FormKey& record,
                                                std::size_t combination, std::size_t index,
                                                const FormKey& target) {
  return edit(plugin, record, combination, index, target, Row::keyword);
}

ObjectTemplateWrite set_object_template_include(Plugin& plugin, const FormKey& record,
                                                std::size_t combination, std::size_t index,
                                                const FormKey& target) {
  return edit(plugin, record, combination, index, target, Row::include);
}

ObjectTemplateWrite set_object_template_property_form(Plugin& plugin, const FormKey& record,
                                                      std::size_t combination, std::size_t index,
                                                      const FormKey& target) {
  return edit(plugin, record, combination, index, target, Row::property_form);
}

}  // namespace fo4::forms
