#include "fo4/forms/quest.hpp"

#include "fo4/plugin/reader.hpp"
#include "fo4/signature.hpp"
#include "quest_aliases_internal.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace fo4::forms {

using namespace detail;

namespace {

void decode_reference_fill(QuestAlias& alias,
                           const PluginFormIdContext& context) {
  const auto* specific = unique_member(alias, k_alfr, "ALFR");
  const auto* unique_actor = unique_member(alias, k_alua, "ALUA");
  const auto* location_alias = unique_member(alias, k_alfa, "ALFA");
  const auto* location_type = unique_member(alias, k_alrt, "ALRT");
  const auto* external_quest = unique_member(alias, k_aleq, "ALEQ");
  const auto* external_alias = unique_member(alias, k_alea, "ALEA");
  const auto* create_object = unique_member(alias, k_alco, "ALCO");
  const auto* create_alias = unique_member(alias, k_alca, "ALCA");
  const auto* create_level = unique_member(alias, k_alcl, "ALCL");
  const auto* near_alias = unique_member(alias, k_alna, "ALNA");
  const auto* near_type = unique_member(alias, k_alnt, "ALNT");
  const auto* event = unique_member(alias, k_alfe, "ALFE");
  const auto* event_data = unique_member(alias, k_alfd, "ALFD");
  const auto* closest = unique_member(alias, k_alcc, "ALCC");

  const bool has_specific = specific != nullptr;
  const bool has_unique = unique_actor != nullptr;
  const bool has_location = location_alias != nullptr || location_type != nullptr;
  const bool has_external = external_quest != nullptr || external_alias != nullptr;
  const bool has_create =
      create_object != nullptr || create_alias != nullptr || create_level != nullptr;
  const bool has_matching = near_alias != nullptr || near_type != nullptr ||
                            event != nullptr || event_data != nullptr ||
                            closest != nullptr;
  const auto variants = static_cast<unsigned>(has_specific) +
                        static_cast<unsigned>(has_unique) +
                        static_cast<unsigned>(has_location) +
                        static_cast<unsigned>(has_external) +
                        static_cast<unsigned>(has_create) +
                        static_cast<unsigned>(has_matching);
  if (variants > 1) {
    throw QuestError("QUST reference alias contains multiple Fill Type variants");
  }

  if (has_specific) {
    alias.fill = QuestSpecificReferenceFill{
        decode_optional_form_key(*specific, context, "ALFR Specific Reference")};
    return;
  }
  if (has_unique) {
    alias.fill = QuestUniqueActorFill{
        decode_required_form_key(*unique_actor, context, "ALUA Unique Actor")};
    return;
  }
  if (has_location) {
    if (location_alias == nullptr || location_type == nullptr) {
      throw QuestError(
          "QUST Location Alias Reference fill requires both ALFA and ALRT");
    }
    if (location_alias->data.size() != 4) {
      throw QuestError("QUST ALFA alias ID must be exactly four bytes");
    }
    alias.fill = QuestLocationAliasReferenceFill{
        .alias = read_i32(location_alias->data),
        .reference_type =
            decode_required_form_key(*location_type, context, "ALRT Ref Type"),
    };
    return;
  }
  if (has_external) {
    if (external_alias == nullptr) {
      throw QuestError("QUST External Alias Reference fill requires ALEA");
    }
    if (external_alias->data.size() != 4) {
      throw QuestError("QUST ALEA external alias ID must be exactly four bytes");
    }
    std::optional<FormKey> quest;
    if (external_quest != nullptr) {
      quest = decode_optional_form_key(*external_quest, context, "ALEQ Quest");
    }
    alias.fill = QuestExternalAliasReferenceFill{
        .quest = std::move(quest),
        .alias = read_i32(external_alias->data),
    };
    return;
  }
  if (has_create) {
    if (create_object == nullptr || create_alias == nullptr) {
      throw QuestError("QUST Create Reference fill requires both ALCO and ALCA");
    }
    if (create_alias->data.size() != 4) {
      throw QuestError("QUST ALCA Create Reference data must be exactly four bytes");
    }
    std::uint32_t level = 0;
    if (create_level != nullptr) {
      if (create_level->data.size() != 4) {
        throw QuestError("QUST ALCL Level must be exactly four bytes");
      }
      level = read_u32(create_level->data);
      if (level > 4u) throw QuestError("QUST ALCL Level must be 0..4 (Easy, Medium, Hard, VeryHard, None)");
    }
    alias.fill = QuestCreateReferenceFill{
        .object = decode_optional_form_key(*create_object, context, "ALCO Object"),
        .alias = read_i16(create_alias->data),
        .create_mode = read_u16(create_alias->data, 2),
        .level = level,
    };
    return;
  }
  if (has_matching) {
    const bool near = near_alias != nullptr || near_type != nullptr;
    const bool from_event = event != nullptr || event_data != nullptr;
    const bool nearest = closest != nullptr;
    if (static_cast<unsigned>(near) + static_cast<unsigned>(from_event) +
            static_cast<unsigned>(nearest) !=
        1u) {
      throw QuestError("QUST Find Matching Reference contains mixed Match Type selectors");
    }
    if (near) {
      if (near_alias == nullptr || near_type == nullptr ||
          near_alias->data.size() != 4 || near_type->data.size() != 4) {
        throw QuestError("QUST Near Alias match requires four-byte ALNA and ALNT");
      }
      alias.fill = QuestFindMatchingReferenceFill{
          QuestMatchingReferenceNearAlias{
              .alias = read_i32(near_alias->data),
              .type = read_u32(near_type->data),
          }};
    } else if (from_event) {
      if (event == nullptr || event_data == nullptr ||
          event->data.size() != 4 || event_data->data.size() != 4) {
        throw QuestError("QUST From Event match requires four-byte ALFE and ALFD");
      }
      alias.fill = QuestFindMatchingReferenceFill{
          QuestMatchingReferenceEvent{
              .event = read_u32(event->data),
              .data = read_u32(event_data->data),
          }};
    } else {
      if (closest->data.size() != 4) {
        throw QuestError("QUST Closest To match ALCC must be exactly four bytes");
      }
      alias.fill = QuestFindMatchingReferenceFill{
          QuestMatchingReferenceClosest{read_i32(closest->data)}};
    }
  }
}

void decode_location_fill(QuestAlias& alias,
                          const PluginFormIdContext& context) {
  const auto* specific = unique_member(alias, k_alfl, "ALFL");
  const auto* reference_alias = unique_member(alias, k_alfa, "ALFA");
  const auto* keyword = unique_member(alias, k_knam, "KNAM");
  const auto* external_quest = unique_member(alias, k_aleq, "ALEQ");
  const auto* external_alias = unique_member(alias, k_alea, "ALEA");
  const auto* event = unique_member(alias, k_alfe, "ALFE");
  const auto* event_data = unique_member(alias, k_alfd, "ALFD");

  const bool has_specific = specific != nullptr;
  const bool has_reference = reference_alias != nullptr || keyword != nullptr;
  const bool has_external = external_quest != nullptr || external_alias != nullptr;
  const bool has_matching = event != nullptr || event_data != nullptr;
  const auto variants = static_cast<unsigned>(has_specific) +
                        static_cast<unsigned>(has_reference) +
                        static_cast<unsigned>(has_external) +
                        static_cast<unsigned>(has_matching);
  if (variants > 1) {
    throw QuestError("QUST location alias contains multiple Fill Type variants");
  }

  if (has_specific) {
    alias.fill = QuestSpecificLocationFill{
        decode_required_form_key(*specific, context, "ALFL Specific Location")};
    return;
  }
  if (has_reference) {
    if (reference_alias == nullptr) {
      throw QuestError("QUST Reference Alias Location fill requires ALFA");
    }
    if (reference_alias->data.size() != 4) {
      throw QuestError("QUST ALFA alias ID must be exactly four bytes");
    }
    std::optional<FormKey> resolved_keyword;
    if (keyword != nullptr) {
      resolved_keyword = decode_required_form_key(*keyword, context, "KNAM Keyword");
    }
    alias.fill = QuestReferenceAliasLocationFill{
        .alias = read_i32(reference_alias->data),
        .keyword = std::move(resolved_keyword),
    };
    return;
  }
  if (has_external) {
    std::optional<FormKey> quest;
    if (external_quest != nullptr) {
      quest = decode_optional_form_key(*external_quest, context, "ALEQ Quest");
    }
    std::optional<std::int32_t> external_id;
    if (external_alias != nullptr) {
      if (external_alias->data.size() != 4) {
        throw QuestError("QUST ALEA external alias ID must be exactly four bytes");
      }
      external_id = read_i32(external_alias->data);
    }
    alias.fill = QuestExternalAliasLocationFill{
        .quest = std::move(quest),
        .alias = external_id,
    };
    return;
  }
  if (has_matching) {
    if (event == nullptr || event_data == nullptr ||
        event->data.size() != 4 || event_data->data.size() != 4) {
      throw QuestError("QUST Find Matching Location requires four-byte ALFE and ALFD");
    }
    alias.fill = QuestFindMatchingLocationFill{
        .event = read_u32(event->data),
        .data = read_u32(event_data->data),
    };
  }
}

}  // namespace

namespace detail {

void decode_alias_semantics(QuestAlias& alias,
                            const PluginFormIdContext* context,
                            const Plugin* plugin,
                            const RecordIndex* index,
                            const FormLinkTypeLookup* owner_types) {
  alias.unresolved_links.clear();
  t_unresolved = &alias.unresolved_links;
  struct Guard {
    ~Guard() { t_unresolved = nullptr; }
  } guard;

  if (alias.kind == QuestAliasKind::collection) {
    for (std::size_t i = 0; i < alias.subrecords.size();) {
      if (alias.subrecords[i].signature != k_alcs) {
        ++i;
        continue;
      }
      QuestCollectionEntry entry;
      entry.id = alias_id(alias.subrecords[i], "collection ALCS");
      ++i;
      if (i < alias.subrecords.size() && alias.subrecords[i].signature == k_almi) {
        if (alias.subrecords[i].data.size() != 1) {
          throw QuestError("QUST collection alias ALMI must be exactly one byte");
        }
        entry.max_initial_fill_count =
            std::to_integer<std::uint8_t>(alias.subrecords[i].data.front());
        ++i;
      }
      alias.collection_entries.push_back(entry);
    }
    return;
  }

  const auto* name = unique_member(alias, k_alid, "ALID");
  if (alias.kind == QuestAliasKind::reference && name == nullptr) {
    throw QuestError("QUST reference alias is missing required ALID Alias Name");
  }
  if (name != nullptr) {
    alias.name = alias_string(*name, "ALID Alias Name");
  }

  const auto* flags = unique_member(alias, k_fnam, "FNAM");
  if (flags == nullptr) {
    throw QuestError(
        "QUST " + std::string(alias_label(alias.kind)) +
        " alias is missing required FNAM Flags");
  }
  if (flags->data.size() != 4) {
    throw QuestError("QUST alias FNAM Flags must be exactly four bytes");
  }
  alias.flags = read_u32(flags->data);

  if (const auto* force = unique_member(alias, k_alfi, "ALFI")) {
    if (force->data.size() != 4) {
      throw QuestError("QUST alias ALFI Force Into Alias must be exactly four bytes");
    }
    alias.force_into_alias = read_i32(force->data);
  }

  if (alias.kind == QuestAliasKind::location) {
    if (const auto* closest = unique_member(alias, k_alcc, "ALCC")) {
      if (closest->data.size() != 4) {
        throw QuestError("QUST location alias ALCC Closest To Alias must be four bytes");
      }
      alias.closest_to_alias = read_i32(closest->data);
    }
  }

  const bool reference_fill_present =
      unique_member(alias, k_alfr, "ALFR") != nullptr ||
      unique_member(alias, k_alua, "ALUA") != nullptr ||
      unique_member(alias, k_alfa, "ALFA") != nullptr ||
      unique_member(alias, k_alrt, "ALRT") != nullptr ||
      unique_member(alias, k_aleq, "ALEQ") != nullptr ||
      unique_member(alias, k_alea, "ALEA") != nullptr ||
      unique_member(alias, k_alco, "ALCO") != nullptr ||
      unique_member(alias, k_alca, "ALCA") != nullptr ||
      unique_member(alias, k_alcl, "ALCL") != nullptr ||
      unique_member(alias, k_alna, "ALNA") != nullptr ||
      unique_member(alias, k_alnt, "ALNT") != nullptr ||
      unique_member(alias, k_alfe, "ALFE") != nullptr ||
      unique_member(alias, k_alfd, "ALFD") != nullptr ||
      unique_member(alias, k_alcc, "ALCC") != nullptr;
  const bool location_fill_present =
      unique_member(alias, k_alfl, "ALFL") != nullptr ||
      unique_member(alias, k_alfa, "ALFA") != nullptr ||
      unique_member(alias, k_knam, "KNAM") != nullptr ||
      unique_member(alias, k_aleq, "ALEQ") != nullptr ||
      unique_member(alias, k_alea, "ALEA") != nullptr ||
      unique_member(alias, k_alfe, "ALFE") != nullptr ||
      unique_member(alias, k_alfd, "ALFD") != nullptr;

  if ((alias.kind == QuestAliasKind::reference && reference_fill_present) ||
      (alias.kind == QuestAliasKind::location && location_fill_present)) {
    if (context == nullptr) {
      throw QuestError("QUST FormLink-bearing alias fill requires plugin form ID context");
    }
    if (alias.kind == QuestAliasKind::reference) {
      decode_reference_fill(alias, *context);
    } else {
      decode_location_fill(alias, *context);
    }
  }

  if (alias.kind == QuestAliasKind::reference && context != nullptr) {
    decode_alias_extras(alias, *context, plugin, index, owner_types);
  }
}

}  // namespace detail

namespace {

QuestAliases decode_quest_aliases_record(const Plugin& plugin,
                                         const FormKey& key,
                                         const Record* record,
                                         const RecordIndex* record_index,
                                         const FormLinkTypeLookup* owner_types) {
  if (record == nullptr) {
    throw QuestError("QUST record not found: " + key.to_string());
  }
  if (record->header.signature != k_qust) {
    throw QuestError("record is not QUST: " + key.to_string());
  }

  QuestAliases result;
  std::optional<std::size_t> anam_index;
  for (std::size_t index = 0; index < record->subrecords.size(); ++index) {
    const auto& subrecord = record->subrecords[index];
    if (subrecord.signature != k_anam) continue;
    if (anam_index.has_value()) {
      throw QuestError("QUST contains duplicate ANAM Next Alias ID subrecords");
    }
    if (subrecord.data.size() != 4) {
      throw QuestError("QUST ANAM Next Alias ID must be exactly four bytes");
    }
    anam_index = index;
    result.next_alias_id = read_u32(subrecord.data);
  }
  if (!anam_index.has_value()) {
    // Partial overrides without alias markers decode empty; markers without ANAM throw.
    const auto has_alias_markers =
        std::any_of(record->subrecords.begin(), record->subrecords.end(),
                    [](const Subrecord& subrecord) {
                      return is_alias_start(subrecord.signature);
                    });
    if (!has_alias_markers) {
      return result;
    }
    throw QuestError("QUST is missing required ANAM Next Alias ID");
  }

  const auto context = plugin_form_id_context(plugin);
  std::set<std::uint32_t> alias_ids;
  std::size_t index = *anam_index + 1;
  while (index < record->subrecords.size() &&
         is_alias_start(record->subrecords[index].signature)) {
    const auto& start = record->subrecords[index];
    QuestAlias alias;
    alias.kind = alias_kind(start.signature);

    if (alias.kind == QuestAliasKind::collection) {
      while (index < record->subrecords.size() &&
             record->subrecords[index].signature == k_alcs) {
        alias.subrecords.push_back(record->subrecords[index]);
        ++index;
        if (index < record->subrecords.size() &&
            record->subrecords[index].signature == k_almi) {
          alias.subrecords.push_back(record->subrecords[index]);
          ++index;
        }
      }
      decode_alias_semantics(alias, &context, &plugin, record_index, owner_types);
      result.aliases.push_back(std::move(alias));
      continue;
    }

    alias.id = alias_id(start, alias_label(alias.kind));
    if (!alias_ids.insert(alias.id).second) {
      throw QuestError("QUST contains duplicate Alias ID " + std::to_string(alias.id));
    }

    bool ended = false;
    for (; index < record->subrecords.size(); ++index) {
      const auto& member = record->subrecords[index];
      if (!alias.subrecords.empty() && is_alias_start(member.signature)) {
        throw QuestError(
            "QUST " + std::string(alias_label(alias.kind)) +
            " alias begins another alias before required ALED");
      }
      alias.subrecords.push_back(member);
      if (member.signature != k_aled) continue;
      if (!member.data.empty()) {
        throw QuestError("QUST ALED Alias End marker must be empty");
      }
      ++index;
      ended = true;
      break;
    }
    if (!ended) {
      throw QuestError(
          "QUST " + std::string(alias_label(alias.kind)) +
          " alias is missing required ALED end marker");
    }
    decode_alias_semantics(alias, &context, &plugin, record_index, owner_types);
    result.aliases.push_back(std::move(alias));
  }

  for (; index < record->subrecords.size(); ++index) {
    const auto signature = record->subrecords[index].signature;
    if (is_alias_start(signature) || signature == k_aled || signature == k_almi) {
      throw QuestError("QUST alias member appears after the Alias array tail boundary");
    }
  }

  return result;
}

}  // namespace

QuestAliases decode_quest_aliases(const Plugin& plugin, const FormKey& key) {
  return decode_quest_aliases_record(plugin, key, find_record(plugin, key), nullptr, nullptr);
}

QuestAliases decode_quest_aliases(const RecordIndex& index, const FormKey& key) {
  return decode_quest_aliases_record(index.plugin(), key, index.find(key), &index, nullptr);
}

QuestAliases decode_quest_aliases(const Plugin& plugin,
                                  const FormKey& key,
                                  const FormLinkTypeLookup& owner_types) {
  return decode_quest_aliases_record(plugin, key, find_record(plugin, key), nullptr, &owner_types);
}

}  // namespace fo4::forms
