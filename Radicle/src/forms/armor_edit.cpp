#include "fo4/forms/armor.hpp"

#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/schema/preflight.hpp"
#include "fo4/schema/record_constraints.hpp"
#include "../plugin/record_edit.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::forms {
namespace {

constexpr auto k_armo = Signature::from_string("ARMO");
constexpr auto k_arma = Signature::from_string("ARMA");
constexpr auto k_mod2 = Signature::from_string("MOD2");
constexpr auto k_mod3 = Signature::from_string("MOD3");
constexpr auto k_mod4 = Signature::from_string("MOD4");
constexpr auto k_mod5 = Signature::from_string("MOD5");
constexpr auto k_icon = Signature::from_string("ICON");
constexpr auto k_mico = Signature::from_string("MICO");
constexpr auto k_ico2 = Signature::from_string("ICO2");
constexpr auto k_mic2 = Signature::from_string("MIC2");

std::vector<std::byte> zstring(std::string_view value) {
  if (value.find('\0') != std::string_view::npos) {
    throw ArmorError("ARMO/ARMA string fields cannot contain embedded NUL bytes");
  }
  std::vector<std::byte> data;
  data.reserve(value.size() + 1);
  for (const char ch : value) {
    data.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  }
  data.push_back(std::byte{0});
  return data;
}

using fo4::detail::find_mutable_record;

void mark_modified(Plugin& plugin, Record& record) {
  record.original_range.clear();
  record.decompressed_payload.clear();
  record.header.data_size = 0;
  record.uncompressed_size = 0;
  plugin.original_range.clear();
}

std::optional<std::size_t> rule_index(
    std::span<const schema::SubrecordAuthoringConstraint> rules,
    Signature signature) {
  for (std::size_t index = 0; index < rules.size(); ++index) {
    if (rules[index].signature == signature) return index;
  }
  return std::nullopt;
}

std::span<const schema::SubrecordAuthoringConstraint> rules_for(
    Signature record_signature) {
  const auto constraint = schema::record_authoring_constraint(record_signature);
  if (!constraint.has_value()) {
    throw ArmorError(
        "record type has no generated authoring schema: " +
        record_signature.to_string());
  }
  return schema::authoring_subrecords(*constraint);
}

void require_safe_missing_field_insertion(const Plugin& plugin,
                                          const Record& record,
                                          const FormKey& key) {
  const auto rules = rules_for(record.header.signature);
  for (const auto& subrecord : record.subrecords) {
    if (!rule_index(rules, subrecord.signature).has_value()) {
      throw ArmorError(
          "cannot insert a missing gendered field around unmodeled subrecord " +
          subrecord.signature.to_string() + " in " +
          record.header.signature.to_string());
    }
  }

  for (const auto& issue : schema::preflight_authoring_record(plugin, key)) {
    if (issue.severity == schema::PreflightSeverity::error) {
      throw ArmorError(
          "cannot insert a missing gendered field into an invalid authoring layout: " +
          issue.message);
    }
  }
}

bool upsert_ordered_string(Plugin& plugin,
                           Record& record,
                           const FormKey& key,
                           Signature signature,
                           std::string_view value) {
  const auto encoded = zstring(stored_inline_text(value, signature.to_string()));
  Subrecord* found = nullptr;
  for (auto& subrecord : record.subrecords) {
    if (subrecord.signature != signature) continue;
    if (found != nullptr) {
      throw ArmorError(
          record.header.signature.to_string() + " contains duplicate " +
          signature.to_string() + " subrecords");
    }
    found = &subrecord;
  }

  if (found != nullptr) {
    if (found->data == encoded) return false;
    found->data = encoded;
    return true;
  }

  require_safe_missing_field_insertion(plugin, record, key);
  const auto rules = rules_for(record.header.signature);
  const auto target = rule_index(rules, signature);
  if (!target.has_value()) {
    throw ArmorError(
        "generated authoring schema does not model " + signature.to_string() +
        " for " + record.header.signature.to_string());
  }

  auto position = record.subrecords.end();
  for (auto it = record.subrecords.begin(); it != record.subrecords.end(); ++it) {
    const auto existing = rule_index(rules, it->signature);
    if (!existing.has_value()) {
      throw ArmorError("authoring layout changed during gendered field insertion");
    }
    if (*existing > *target) {
      position = it;
      break;
    }
  }

  Subrecord created;
  created.signature = signature;
  created.data = encoded;
  record.subrecords.insert(position, std::move(created));
  return true;
}

}  // namespace

bool set_armor_model_fields(
    Plugin& plugin,
    const FormKey& key,
    std::optional<std::string_view> male_world_model,
    std::optional<std::string_view> male_icon,
    std::optional<std::string_view> male_message_icon,
    std::optional<std::string_view> female_world_model,
    std::optional<std::string_view> female_icon,
    std::optional<std::string_view> female_message_icon) {
  auto next = plugin;
  assign_record_form_keys(next);
  auto* record = find_mutable_record(next, key);
  if (record == nullptr) return false;
  if (record->header.signature != k_armo) {
    throw ArmorError("record is not ARMO: " + key.to_string());
  }

  bool changed = false;
  if (male_world_model.has_value()) {
    changed |= upsert_ordered_string(next, *record, key, k_mod2, *male_world_model);
  }
  if (male_icon.has_value()) {
    changed |= upsert_ordered_string(next, *record, key, k_icon, *male_icon);
  }
  if (male_message_icon.has_value()) {
    changed |= upsert_ordered_string(next, *record, key, k_mico, *male_message_icon);
  }
  if (female_world_model.has_value()) {
    changed |= upsert_ordered_string(next, *record, key, k_mod4, *female_world_model);
  }
  if (female_icon.has_value()) {
    changed |= upsert_ordered_string(next, *record, key, k_ico2, *female_icon);
  }
  if (female_message_icon.has_value()) {
    changed |= upsert_ordered_string(next, *record, key, k_mic2, *female_message_icon);
  }

  if (!changed) return true;
  mark_modified(next, *record);
  plugin = std::move(next);
  return true;
}

bool set_armor_addon_model_fields(
    Plugin& plugin,
    const FormKey& key,
    std::optional<std::string_view> male_biped_model,
    std::optional<std::string_view> female_biped_model,
    std::optional<std::string_view> male_first_person_model,
    std::optional<std::string_view> female_first_person_model) {
  auto next = plugin;
  assign_record_form_keys(next);
  auto* record = find_mutable_record(next, key);
  if (record == nullptr) return false;
  if (record->header.signature != k_arma) {
    throw ArmorError("record is not ARMA: " + key.to_string());
  }

  bool changed = false;
  if (male_biped_model.has_value()) {
    changed |= upsert_ordered_string(next, *record, key, k_mod2, *male_biped_model);
  }
  if (female_biped_model.has_value()) {
    changed |= upsert_ordered_string(next, *record, key, k_mod3, *female_biped_model);
  }
  if (male_first_person_model.has_value()) {
    changed |= upsert_ordered_string(next, *record, key, k_mod4, *male_first_person_model);
  }
  if (female_first_person_model.has_value()) {
    changed |= upsert_ordered_string(next, *record, key, k_mod5, *female_first_person_model);
  }

  if (!changed) return true;
  mark_modified(next, *record);
  plugin = std::move(next);
  return true;
}

}  // namespace fo4::forms
