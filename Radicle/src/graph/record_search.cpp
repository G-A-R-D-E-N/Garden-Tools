#include "fo4/graph/record_search.hpp"

#include "fo4/graph/conflict_kind.hpp"
#include "fo4/plugin/load_order.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/schema/form_links.hpp"
#include "fo4/schema/localized_fields.hpp"
#include "fo4/schema/scalar.hpp"
#include "fo4/text/encoding.hpp"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <map>
#include <optional>
#include <regex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fo4 {
namespace {

using LinkMap = std::map<FormKey, std::vector<FormKey>>;

const auto k_full = Signature::from_string("FULL");

struct FormIdFilter {
  enum class Mode { exact_key, local, load_order };
  Mode mode{Mode::local};
  FormKey key;
  std::uint32_t local{0};
  std::uint32_t raw{0};
};

char ascii_lower(char ch) noexcept {
  return ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch - 'A' + 'a') : ch;
}

bool contains_insensitive(std::string_view value, std::string_view needle) {
  if (needle.empty()) return true;
  return std::search(value.begin(), value.end(), needle.begin(), needle.end(),
                     [](char a, char b) { return ascii_lower(a) == ascii_lower(b); }) != value.end();
}

std::optional<std::uint32_t> parse_hex_digits(std::string_view digits) {
  if (digits.empty() || digits.size() > 8) return std::nullopt;
  std::uint32_t value = 0;
  for (const char ch : digits) {
    std::uint32_t digit = 0;
    if (ch >= '0' && ch <= '9') {
      digit = static_cast<std::uint32_t>(ch - '0');
    } else if (ch >= 'a' && ch <= 'f') {
      digit = static_cast<std::uint32_t>(ch - 'a' + 10);
    } else if (ch >= 'A' && ch <= 'F') {
      digit = static_cast<std::uint32_t>(ch - 'A' + 10);
    } else {
      return std::nullopt;
    }
    value = (value << 4) | digit;
  }
  return value;
}

FormIdFilter parse_form_id_filter(std::string_view text) {
  FormIdFilter filter;
  if (text.find(':') != std::string_view::npos) {
    try {
      filter.mode = FormIdFilter::Mode::exact_key;
      filter.key = FormKey::parse(text);
    } catch (const std::exception& error) {
      throw std::invalid_argument(std::string("form_id is not a valid FormKey: ") + error.what());
    }
    return filter;
  }

  auto digits = text;
  if (digits.size() >= 2 && digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X')) {
    digits.remove_prefix(2);
  }
  const auto value = parse_hex_digits(digits);
  if (!value) {
    throw std::invalid_argument("form_id must be hex digits, a load-order form ID or a FormKey");
  }
  if (digits.size() == 8) {
    filter.mode = FormIdFilter::Mode::load_order;
    filter.raw = *value;
    return filter;
  }
  if (digits.size() > 6) {
    throw std::invalid_argument("form_id must be six hex digits, eight load-order digits or a FormKey");
  }
  filter.mode = FormIdFilter::Mode::local;
  filter.local = *value;
  return filter;
}

bool resolve_load_order_key(FormIdFilter& filter, std::span<const Plugin* const> load_order) {
  try {
    filter.key = resolve_runtime_form_id(RawFormId{filter.raw}, build_runtime_form_id_context(load_order));
    return true;
  } catch (const FormIdError&) {
    return false;
  }
}

bool form_id_matches(const FormIdFilter& filter, const FormKey& key) {
  switch (filter.mode) {
    case FormIdFilter::Mode::exact_key:
    case FormIdFilter::Mode::load_order:
      return key == filter.key;
    case FormIdFilter::Mode::local:
      return (key.local().value() & 0x00FFFFFFu) == filter.local;
  }
  return false;
}

constexpr std::size_t k_max_editor_id_pattern = 128;

bool glob_matches(std::string_view pattern, std::string_view text) {
  constexpr auto none = std::string_view::npos;
  std::size_t p = 0;
  std::size_t t = 0;
  std::size_t star = none;
  std::size_t resume = 0;
  while (t < text.size()) {
    if (p < pattern.size() && pattern[p] == '*') {
      star = p++;
      resume = t;
    } else if (p < pattern.size() && (pattern[p] == '?' || ascii_lower(pattern[p]) == ascii_lower(text[t]))) {
      ++p;
      ++t;
    } else if (star != none) {
      p = star + 1;
      t = ++resume;
    } else {
      return false;
    }
  }
  while (p < pattern.size() && pattern[p] == '*') ++p;
  return p == pattern.size();
}

std::string editor_id_text(const Record& record) {
  for (const auto& sub : record.subrecords) {
    if (sub.signature != sig_edid || sub.data.empty()) continue;
    const auto* begin = reinterpret_cast<const char*>(sub.data.data());
    const auto* end = begin + sub.data.size();
    const auto* nul = std::find(begin, end, '\0');
    return text::windows1252_to_utf8(std::string_view(begin, nul));
  }
  return {};
}

std::string display_name_text(const Plugin& plugin,
                              std::size_t plugin_index,
                              const Record& record,
                              const LocalizedNameResolver& resolve_name) {
  for (const auto& sub : record.subrecords) {
    if (sub.signature != k_full) continue;
    try {
      const auto value = schema::read_localized_string(plugin, record.header.signature, k_full, sub);
      if (!value.localized()) return text::windows1252_to_utf8(value.inline_text());
      if (!resolve_name) return {};
      return resolve_name(plugin_index, *value.id).value_or(std::string{});
    } catch (const std::exception&) {
      return {};
    }
  }
  return {};
}

bool enumeration_name_matches(const schema::ScalarDescriptor& descriptor,
                              std::string_view decoded,
                              std::string_view needle) {
  if (descriptor.presentation != schema::ScalarPresentation::enumeration) return false;
  return std::any_of(descriptor.names.begin(), descriptor.names.end(), [&](const auto& name) {
    return contains_insensitive(name.name, needle) && decoded == std::to_string(name.value);
  });
}

bool scalar_value_matches(const Record& record, bool localized, std::string_view needle) {
  for (const auto& field : schema::scalar_fields(record.header.signature)) {
    const auto resolved = schema::resolve_field_slots(record, schema::scalar_field_shape(field.scalar),
                                                      field.scalar.address);
    if (resolved.duplicate || resolved.malformed) continue;
    for (const auto& slot : resolved.slots) {
      if (!slot.valid) continue;
      const auto& payload = record.subrecords[slot.subrecord].data;
      if (localized && field.scalar.storage == schema::ScalarStorage::zstring && payload.size() == 4 &&
          (field.scalar.localized || schema::is_localized_field(field.record, field.scalar.subrecord))) {
        continue;
      }
      try {
        const auto decoded =
            schema::decode_scalar(schema::scalar_slot_bytes(payload, slot, field.scalar), field.scalar);
        if (contains_insensitive(decoded, needle) || enumeration_name_matches(field.scalar, decoded, needle)) {
          return true;
        }
      } catch (const schema::ScalarError&) {
        continue;
      }
    }
  }
  return false;
}

LinkMap link_targets(const Plugin& plugin) {
  LinkMap out;
  for (const auto& hit : collect_form_links_detailed(plugin).links) out[hit.record].push_back(hit.key);
  return out;
}

bool field_value_matches(const Record& record, bool localized, std::string_view needle, const LinkMap& links) {
  if (!record.undecodable && scalar_value_matches(record, localized, needle)) return true;
  const auto found = links.find(record.form_key);
  if (found == links.end()) return false;
  return std::any_of(found->second.begin(), found->second.end(), [needle](const FormKey& target) {
    return contains_insensitive(target.to_string(), needle);
  });
}

bool conflict_matches(const RecordSearchQuery& query,
                      const ConflictGraph& graph,
                      std::span<const Plugin* const> load_order,
                      std::span<const ConflictPriorityRule> priorities,
                      const FormKey& key,
                      std::map<FormKey, ConflictCategory>& cache) {
  auto found = cache.find(key);
  if (found == cache.end()) {
    const auto conflict = classify_record_conflict(
        graph, ConflictQuery{load_order, key, ConflictPriority::normal, priorities});
    found = cache.emplace(key, conflict.category).first;
  }
  return std::find(query.conflict_categories.begin(), query.conflict_categories.end(), found->second) !=
         query.conflict_categories.end();
}

}  // namespace

RecordSearchResult search_records(std::span<const Plugin* const> load_order,
                                  const RecordSearchQuery& query,
                                  const ConflictGraph* graph,
                                  std::span<const ConflictPriorityRule> priorities,
                                  const LocalizedNameResolver& resolve_name) {
  if (query.limit == 0) throw std::invalid_argument("record search limit must be positive");
  if (!query.conflict_categories.empty() && graph == nullptr) {
    throw std::invalid_argument("conflict filter requires a conflict graph");
  }

  std::optional<FormIdFilter> form_id;
  if (!query.form_id.empty()) form_id = parse_form_id_filter(query.form_id);
  if (query.editor_id_pattern.size() > k_max_editor_id_pattern) {
    throw std::invalid_argument("editor_id_pattern must be at most 128 bytes");
  }

  std::regex text_regex;
  if (query.text_is_regex && !query.text.empty()) {
    text_regex = std::regex(query.text, std::regex::ECMAScript | std::regex::icase | std::regex::optimize);
  }
  const auto text_matches = [&](std::string_view value) {
    if (!query.text_is_regex) return contains_insensitive(value, query.text);
    return !value.empty() && std::regex_search(value.begin(), value.end(), text_regex);
  };

  RecordSearchResult result;
  if (form_id && form_id->mode == FormIdFilter::Mode::load_order && !resolve_load_order_key(*form_id, load_order)) {
    return result;
  }
  std::map<FormKey, ConflictCategory> categories;
  for (std::size_t index = 0; index < load_order.size(); ++index) {
    if (load_order[index] == nullptr) continue;
    const auto& plugin = *load_order[index];
    const bool localized = plugin.header.tes4_flags.is_localized();
    LinkMap links;
    bool links_built = false;

    for (const auto* record : collect_records(plugin)) {
      if (record->header.signature == sig_tes4 || record->form_key.owner().empty()) continue;
      if (!query.signatures.empty() &&
          std::find(query.signatures.begin(), query.signatures.end(), record->header.signature) ==
              query.signatures.end()) {
        continue;
      }
      if (form_id && !form_id_matches(*form_id, record->form_key)) continue;

      const auto type = record->header.signature.to_string();
      const auto editor = editor_id_text(*record);
      const auto name_matches = [&](std::string_view needle) {
        return contains_insensitive(display_name_text(plugin, index, *record, resolve_name), needle);
      };
      if (!query.text.empty() && !text_matches(record->form_key.to_string()) && !text_matches(editor) &&
          !text_matches(type) && !text_matches(display_name_text(plugin, index, *record, resolve_name))) {
        continue;
      }
      if (!contains_insensitive(editor, query.editor_id)) continue;
      if (!query.editor_id_pattern.empty() && !glob_matches(query.editor_id_pattern, editor)) continue;
      if (!query.display_name.empty() && !name_matches(query.display_name)) continue;
      if (!query.field_value.empty()) {
        if (!links_built) {
          links = link_targets(plugin);
          links_built = true;
        }
        if (!field_value_matches(*record, localized, query.field_value, links)) continue;
      }
      if (!query.conflict_categories.empty() &&
          !conflict_matches(query, *graph, load_order, priorities, record->form_key, categories)) {
        continue;
      }

      if (result.hits.size() == query.limit) {
        result.truncated = true;
        return result;
      }
      result.hits.push_back(RecordSearchHit{.key = record->form_key,
                                            .signature = record->header.signature,
                                            .editor_id = editor,
                                            .plugin_index = index,
                                            .display_name = display_name_text(plugin, index, *record, resolve_name)});
    }
  }
  return result;
}

}  // namespace fo4
