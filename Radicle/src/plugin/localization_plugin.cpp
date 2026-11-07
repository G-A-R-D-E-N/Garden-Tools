#include "fo4/plugin/mutation.hpp"

#include "record_edit.hpp"

#include "fo4/localization/strings.hpp"
#include "fo4/plugin/container.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/localized_fields.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4 {
namespace {

struct Target {
  Record* record{nullptr};
  Subrecord* subrecord{nullptr};
  std::string_view text;
  std::vector<std::byte> payload;
};

template <class Fn>
void for_each_localized_subrecord(Plugin& plugin, Fn&& fn) {
  visit_records(plugin, [&](Record& record) {
    if (record.header.signature == sig_tes4) return;
    for (auto& subrecord : record.subrecords) {
      if (!schema::is_localized_field(record.header.signature, subrecord.signature)) continue;
      fn(record, subrecord);
    }
  });
}

std::optional<std::string_view> inline_text(std::span<const std::byte> payload) {
  if (payload.empty() || payload.back() != std::byte{0}) return std::nullopt;
  if (std::find(payload.begin(), payload.end() - 1, std::byte{0}) != payload.end() - 1) {
    return std::nullopt;
  }
  return std::string_view(reinterpret_cast<const char*>(payload.data()), payload.size() - 1);
}

std::optional<std::uint32_t> payload_string_id(std::span<const std::byte> payload) {
  if (payload.size() != sizeof(std::uint32_t)) return std::nullopt;
  std::uint32_t id = 0;
  std::memcpy(&id, payload.data(), sizeof(id));
  return id;
}

std::vector<std::byte> string_id_payload(std::uint32_t id) {
  std::vector<std::byte> payload(sizeof(id));
  std::memcpy(payload.data(), &id, sizeof(id));
  return payload;
}

std::vector<std::byte> text_payload(const std::vector<std::byte>& text) {
  std::vector<std::byte> payload = text;
  payload.push_back(std::byte{0});
  return payload;
}

PluginLocalizationResult failed(PluginLocalizationStatus status) {
  PluginLocalizationResult result;
  result.status = status;
  return result;
}

}  // namespace

PluginLocalizationResult localize_plugin(Plugin& plugin,
                                         localization::LocalizationTables& tables) {
  if (plugin.header.tes4_flags.is_localized()) {
    return failed(PluginLocalizationStatus::already_localized);
  }
  detail::require_plugin_decodable(plugin, "localize the plugin");

  bool unconvertible = false;
  std::vector<Target> targets;
  for_each_localized_subrecord(plugin, [&](Record& record, Subrecord& subrecord) {
    const auto text = inline_text(subrecord.data);
    if (!text.has_value()) {
      unconvertible = true;
      return;
    }
    targets.push_back(Target{&record, &subrecord, *text, {}});
  });
  if (unconvertible) return failed(PluginLocalizationStatus::unconvertible_field);

  PluginLocalizationResult result;
  for (auto& target : targets) {
    const auto kind = localization::string_table_kind(target.record->header.signature,
                                                      target.subrecord->signature);
    const auto id = localization::add_string(localization::table_for(tables, kind), target.text);
    if (id == 0) ++result.empty_text;
    target.subrecord->data = string_id_payload(id);
    detail::mark_record_modified(plugin, *target.record);
    ++result.converted;
  }

  plugin = set_plugin_localized_flag(std::move(plugin), true);
  return result;
}

PluginLocalizationResult delocalize_plugin(Plugin& plugin,
                                           localization::LocalizationTables& tables) {
  if (!plugin.header.tes4_flags.is_localized()) {
    return failed(PluginLocalizationStatus::not_localized);
  }
  detail::require_plugin_decodable(plugin, "delocalize the plugin");

  PluginLocalizationStatus status = PluginLocalizationStatus::converted;
  std::size_t empty_text = 0;
  std::vector<Target> targets;
  for_each_localized_subrecord(plugin, [&](Record& record, Subrecord& subrecord) {
    if (status != PluginLocalizationStatus::converted) return;
    const auto id = payload_string_id(subrecord.data);
    if (!id.has_value()) {
      status = PluginLocalizationStatus::unconvertible_field;
      return;
    }
    if (*id == 0) {
      ++empty_text;
      targets.push_back(Target{&record, &subrecord, {}, text_payload({})});
      return;
    }
    const auto kind = localization::string_table_kind(record.header.signature, subrecord.signature);
    const auto* entry = localization::find_string(localization::table_for(tables, kind), *id);
    if (entry == nullptr) {
      status = PluginLocalizationStatus::unresolved_string_id;
      return;
    }
    targets.push_back(Target{&record, &subrecord, {}, text_payload(entry->text)});
  });
  if (status != PluginLocalizationStatus::converted) return failed(status);

  PluginLocalizationResult result;
  result.empty_text = empty_text;
  for (auto& target : targets) {
    target.subrecord->data = std::move(target.payload);
    detail::mark_record_modified(plugin, *target.record);
    ++result.converted;
  }

  plugin = set_plugin_localized_flag(std::move(plugin), false);
  return result;
}

}  // namespace fo4
