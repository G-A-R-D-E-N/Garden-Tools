#include "fo4/forms/quest.hpp"

#include "fo4/plugin/reader.hpp"
#include "fo4/schema/ctda.hpp"
#include "fo4/signature.hpp"
#include "quest_aliases_internal.hpp"

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <set>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace fo4::forms {

using namespace detail;

namespace {

QuestAlias encode_base_alias(const QuestAliasCreation& creation) {
  QuestAlias alias;
  alias.kind = creation.kind;

  if (creation.kind == QuestAliasKind::collection) {
    if (creation.name.has_value() || creation.flags.has_value() ||
        creation.force_into_alias.has_value() || creation.closest_to_alias.has_value()) {
      throw QuestError(
          "QUST collection aliases do not use ALID, FNAM, ALFI, or ALCC fields");
    }
    if (!std::holds_alternative<std::monostate>(creation.fill)) {
      throw QuestError("QUST collection alias Fill Type is not modeled by ALCS/ALMI");
    }
    std::vector<QuestCollectionEntry> entries = creation.collection_entries;
    if (entries.empty()) {
      entries.push_back(QuestCollectionEntry{
          creation.id, creation.max_initial_fill_count});
    } else if (creation.max_initial_fill_count.has_value()) {
      throw QuestError(
          "QUST collection aliases cannot mix entries with max_initial_fill_count");
    }
    std::set<std::uint32_t> entry_ids;
    for (const auto& entry : entries) {
      if (!entry_ids.insert(entry.id).second) {
        throw QuestError(
            "QUST collection alias contains duplicate ALCS id " +
            std::to_string(entry.id));
      }
      alias.subrecords.push_back(make_subrecord(k_alcs, write_u32(entry.id)));
      if (entry.max_initial_fill_count.has_value()) {
        alias.subrecords.push_back(make_subrecord(
            k_almi,
            std::vector<std::byte>{std::byte{*entry.max_initial_fill_count}}));
      }
    }
    return alias;
  }

  alias.id = creation.id;
  if (!creation.flags.has_value()) {
    throw QuestError("QUST reference/location alias creation requires FNAM Flags");
  }
  if (creation.kind == QuestAliasKind::reference && !creation.name.has_value()) {
    throw QuestError("QUST reference alias creation requires ALID Alias Name");
  }
  if (creation.kind == QuestAliasKind::reference && creation.closest_to_alias.has_value()) {
    throw QuestError("QUST reference aliases use ALCC only inside Find Matching Reference");
  }
  if (creation.max_initial_fill_count.has_value()) {
    throw QuestError("QUST reference/location aliases do not use collection ALMI");
  }
  if (!creation.collection_entries.empty()) {
    throw QuestError("QUST collection entries are valid only on collection aliases");
  }

  alias.subrecords.push_back(make_subrecord(
      creation.kind == QuestAliasKind::reference ? k_alst : k_alls,
      write_u32(creation.id)));
  if (creation.name.has_value()) {
    alias.subrecords.push_back(make_subrecord(k_alid, zstring(*creation.name)));
  }
  alias.subrecords.push_back(make_subrecord(k_fnam, write_u32(*creation.flags)));
  if (creation.force_into_alias.has_value()) {
    alias.subrecords.push_back(
        make_subrecord(k_alfi, write_i32(*creation.force_into_alias)));
  }
  alias.subrecords.push_back(make_subrecord(k_aled));
  return alias;
}

void insert_alias_fill(QuestAlias& alias,
                       const QuestAliasFill& fill,
                       const PluginFormIdContext& context) {
  if (std::holds_alternative<std::monostate>(fill)) return;
  if (alias.kind == QuestAliasKind::collection) {
    throw QuestError("QUST collection aliases do not use reference/location Fill Type unions");
  }
  if (alias.subrecords.empty() || alias.subrecords.back().signature != k_aled) {
    throw QuestError("QUST alias encoder lost required ALED boundary");
  }

  std::vector<Subrecord> members;
  std::visit(
      [&](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, std::monostate>) {
          return;
        } else if constexpr (std::is_same_v<T, QuestSpecificReferenceFill>) {
          if (alias.kind != QuestAliasKind::reference) {
            throw QuestError("Specific Reference fill is valid only on reference aliases");
          }
          members.push_back(encoded_optional_form_key(k_alfr, value.reference, context));
        } else if constexpr (std::is_same_v<T, QuestUniqueActorFill>) {
          if (alias.kind != QuestAliasKind::reference) {
            throw QuestError("Unique Actor fill is valid only on reference aliases");
          }
          members.push_back(encoded_form_key(k_alua, value.actor, context));
        } else if constexpr (std::is_same_v<T, QuestLocationAliasReferenceFill>) {
          if (alias.kind != QuestAliasKind::reference) {
            throw QuestError("Location Alias Reference fill is valid only on reference aliases");
          }
          members.push_back(make_subrecord(k_alfa, write_i32(value.alias)));
          members.push_back(encoded_form_key(k_alrt, value.reference_type, context));
        } else if constexpr (std::is_same_v<T, QuestExternalAliasReferenceFill>) {
          if (alias.kind != QuestAliasKind::reference) {
            throw QuestError("External Alias Reference fill is valid only on reference aliases");
          }
          if (value.quest.has_value()) {
            members.push_back(encoded_form_key(k_aleq, *value.quest, context));
          }
          members.push_back(make_subrecord(k_alea, write_i32(value.alias)));
        } else if constexpr (std::is_same_v<T, QuestCreateReferenceFill>) {
          if (alias.kind != QuestAliasKind::reference) {
            throw QuestError("Create Reference fill is valid only on reference aliases");
          }
          if (value.create_mode != 0 && value.create_mode != 0x8000u) {
            throw QuestError("QUST ALCA Create mode must be 0 (At) or 0x8000 (In)");
          }
          if (value.level > 4u) throw QuestError("QUST ALCL Level must be 0..4 (Easy, Medium, Hard, VeryHard, None)");
          members.push_back(encoded_optional_form_key(k_alco, value.object, context));
          members.push_back(make_subrecord(
              k_alca, write_i16_u16(value.alias, value.create_mode)));
          members.push_back(make_subrecord(k_alcl, write_u32(value.level)));
        } else if constexpr (std::is_same_v<T, QuestFindMatchingReferenceFill>) {
          if (alias.kind != QuestAliasKind::reference) {
            throw QuestError("Find Matching Reference fill is valid only on reference aliases");
          }
          std::visit(
              [&](const auto& selector) {
                using S = std::decay_t<decltype(selector)>;
                if constexpr (std::is_same_v<S, QuestMatchingReferenceNearAlias>) {
                  if (selector.type > 1u) {
                    throw QuestError("QUST ALNT match type must be 0 or 1");
                  }
                  members.push_back(make_subrecord(k_alna, write_i32(selector.alias)));
                  members.push_back(make_subrecord(k_alnt, write_u32(selector.type)));
                } else if constexpr (std::is_same_v<S, QuestMatchingReferenceEvent>) {
                  members.push_back(make_subrecord(k_alfe, write_u32(selector.event)));
                  members.push_back(make_subrecord(k_alfd, write_u32(selector.data)));
                } else if constexpr (std::is_same_v<S, QuestMatchingReferenceClosest>) {
                  members.push_back(make_subrecord(k_alcc, write_i32(selector.alias)));
                }
              },
              value.selector);
        } else if constexpr (std::is_same_v<T, QuestSpecificLocationFill>) {
          if (alias.kind != QuestAliasKind::location) {
            throw QuestError("Specific Location fill is valid only on location aliases");
          }
          members.push_back(encoded_form_key(k_alfl, value.location, context));
        } else if constexpr (std::is_same_v<T, QuestReferenceAliasLocationFill>) {
          if (alias.kind != QuestAliasKind::location) {
            throw QuestError("Reference Alias Location fill is valid only on location aliases");
          }
          members.push_back(make_subrecord(k_alfa, write_i32(value.alias)));
          if (value.keyword.has_value()) {
            members.push_back(encoded_form_key(k_knam, *value.keyword, context));
          }
        } else if constexpr (std::is_same_v<T, QuestExternalAliasLocationFill>) {
          if (alias.kind != QuestAliasKind::location) {
            throw QuestError("External Alias Location fill is valid only on location aliases");
          }
          if (!value.quest.has_value() && !value.alias.has_value()) {
            throw QuestError("External Alias Location fill must contain ALEQ or ALEA");
          }
          if (value.quest.has_value()) {
            members.push_back(encoded_form_key(k_aleq, *value.quest, context));
          }
          if (value.alias.has_value()) {
            members.push_back(make_subrecord(k_alea, write_i32(*value.alias)));
          }
        } else if constexpr (std::is_same_v<T, QuestFindMatchingLocationFill>) {
          if (alias.kind != QuestAliasKind::location) {
            throw QuestError("Find Matching Location fill is valid only on location aliases");
          }
          members.push_back(make_subrecord(k_alfe, write_u32(value.event)));
          members.push_back(make_subrecord(k_alfd, write_u32(value.data)));
        }
      },
      fill);

  alias.subrecords.insert(
      alias.subrecords.end() - 1,
      std::make_move_iterator(members.begin()),
      std::make_move_iterator(members.end()));
}

void insert_alias_conditions(QuestAlias& alias, std::span<const CtdaCondition> conditions) {
  if (conditions.empty()) return;
  if (alias.kind == QuestAliasKind::collection) {
    throw QuestError("QUST collection aliases do not use CTDA/CIS conditions");
  }
  insert_before_aled(alias, fo4::serialize_ctda_run(conditions));
}

void insert_post_fill_fields(QuestAlias& alias,
                             const QuestAliasCreation& creation) {
  if (!creation.closest_to_alias.has_value()) return;
  if (alias.kind != QuestAliasKind::location) {
    throw QuestError("QUST Closest To Alias is valid only on location aliases");
  }
  if (alias.subrecords.empty() || alias.subrecords.back().signature != k_aled) {
    throw QuestError("QUST alias encoder lost required ALED boundary");
  }
  alias.subrecords.insert(
      alias.subrecords.end() - 1,
      make_subrecord(k_alcc, write_i32(*creation.closest_to_alias)));
}

}  // namespace

QuestAlias encode_quest_alias(const QuestAliasCreation& creation) {
  if (!std::holds_alternative<std::monostate>(creation.fill)) {
    throw QuestError("QUST FormLink-bearing Fill Type encoding requires a plugin context");
  }
  if (has_reference_extras(creation)) {
    throw QuestError("QUST alias extra FormLinks require a plugin context");
  }
  auto alias = encode_base_alias(creation);
  insert_alias_conditions(alias, creation.conditions);
  insert_post_fill_fields(alias, creation);
  decode_alias_semantics(alias, nullptr, nullptr, nullptr);
  return alias;
}

QuestAlias encode_quest_alias(const Plugin& plugin,
                              const QuestAliasCreation& creation) {
  auto alias = encode_base_alias(creation);
  const auto context = plugin_form_id_context(plugin);
  insert_alias_fill(alias, creation.fill, context);
  insert_alias_conditions(alias, creation.conditions);
  insert_alias_extras(alias, creation, context);
  insert_post_fill_fields(alias, creation);
  decode_alias_semantics(alias, &context, &plugin, nullptr);
  return alias;
}

}  // namespace fo4::forms
