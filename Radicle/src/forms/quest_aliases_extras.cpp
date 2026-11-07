#include "fo4/forms/quest.hpp"

#include "fo4/plugin/reader.hpp"
#include "fo4/signature.hpp"
#include "quest_aliases_internal.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace fo4::forms {

using namespace detail;

namespace {

void append_encoded_form_id(std::vector<std::byte>& packed,
                            const FormKey& key,
                            const PluginFormIdContext& context) {
  try {
    const auto raw = encode_plugin_form_id(key, context).value();
    const auto bytes = write_u32(raw);
    packed.insert(packed.end(), bytes.begin(), bytes.end());
  } catch (const FormIdError& error) {
    throw QuestError("QUST alias FormLink cannot be encoded: " + std::string(error.what()));
  }
}

}  // namespace

namespace detail {

void decode_alias_extras(QuestAlias& alias,
                         const PluginFormIdContext& context,
                         const Plugin* plugin,
                         const RecordIndex* index,
                         const FormLinkTypeLookup* owner_types) {
  if (alias.kind != QuestAliasKind::reference) return;

  const auto* ksiz = unique_member(alias, k_ksiz, "KSIZ");
  const auto* kwda = unique_member(alias, k_kwda, "KWDA");
  if (ksiz != nullptr || kwda != nullptr) {
    if (ksiz == nullptr || kwda == nullptr) {
      throw QuestError("QUST alias keywords require both KSIZ and KWDA");
    }
    if (ksiz->data.size() != 4) {
      throw QuestError("QUST alias KSIZ must be exactly four bytes");
    }
    if (kwda->data.size() % 4 != 0) {
      throw QuestError("QUST alias KWDA must be a multiple of four bytes");
    }
    const auto count = read_u32(ksiz->data);
    if (count != kwda->data.size() / 4) {
      throw QuestError("QUST alias KSIZ does not match KWDA");
    }
    for (std::uint32_t n = 0; n < count; ++n) {
      if (auto key = decode_optional_form_key_at(
              kwda->data, n * 4u, k_kwda, context, "KWDA")) {
        alias.keywords.push_back(*key);
      }
    }
  }

  std::size_t cnto_count = 0;
  std::size_t coed_total = 0;
  std::size_t coed_before_coct = 0;
  bool seen_coct = false;
  for (const auto& member : alias.subrecords) {
    if (member.signature == k_coct) seen_coct = true;
    if (member.signature == k_cnto) ++cnto_count;
    if (member.signature == k_coed) {
      ++coed_total;
      if (!seen_coct) ++coed_before_coct;
    }
  }
  const auto* coct = unique_member(alias, k_coct, "COCT");
  if (coct == nullptr) {
    if (cnto_count != 0) {
      throw QuestError("QUST alias CNTO is missing COCT");
    }
    if (coed_before_coct != 0) {
      throw QuestError("QUST alias COED is missing preceding CNTO");
    }
  } else {
    if (coed_before_coct != 0) {
      throw QuestError("QUST alias COED is missing preceding CNTO");
    }
    if (coct->data.size() != 4) {
      throw QuestError("QUST alias COCT must be exactly four bytes");
    }
    const auto expected = read_u32(coct->data);
    std::size_t seen = 0;
    std::size_t coed_seen = 0;
    bool after_coct = false;
    bool item_open = false;
    bool item_has_extra = false;
    for (const auto& member : alias.subrecords) {
      if (member.signature == k_coct) {
        after_coct = true;
        continue;
      }
      if (!after_coct) continue;
      if (member.signature == k_cnto) {
        if (seen == expected) {
          throw QuestError("QUST alias COCT does not match CNTO");
        }
        if (member.data.size() != 8) {
          throw QuestError("QUST alias CNTO must be exactly eight bytes");
        }
        if (auto key = decode_optional_form_key_at(
                member.data, 0, k_cnto, context, "CNTO")) {
          alias.items.push_back(QuestAliasItem{
              .item = *key,
              .count = read_i32(member.data, 4),
              .extra = {},
          });
          item_open = true;
        } else {
          item_open = false;
        }
        item_has_extra = false;
        ++seen;
        continue;
      }
      if (member.signature == k_coed) {
        if (seen == 0 || item_has_extra) {
          throw QuestError("QUST alias COED is missing preceding CNTO");
        }
        if (member.data.size() != 12) {
          throw QuestError("QUST alias COED must be exactly twelve bytes");
        }
        QuestAliasItemExtra extra;
        extra.owner = decode_optional_form_key_at(
            member.data, 0, k_coed, context, "COED owner");
        extra.item_condition = read_f32(member.data, 8);
        if (!std::isfinite(extra.item_condition)) {
          throw QuestError("QUST alias COED item condition must be finite");
        }
        std::optional<Signature> owner_signature;
        if (extra.owner.has_value() && owner_types != nullptr) {
          owner_signature = (*owner_types)(*extra.owner);
        } else if (extra.owner.has_value() && plugin != nullptr) {
          const Record* owner_record = index != nullptr
                                           ? index->find(*extra.owner)
                                           : find_record(*plugin, *extra.owner);
          if (owner_record != nullptr) owner_signature = owner_record->header.signature;
        }
        if (owner_signature.has_value() && owner_signature->to_string() == "FACT") {
          extra.required_rank = read_i32(member.data, 4);
        } else if (owner_signature.has_value() && owner_signature->to_string() == "NPC_") {
          extra.global = decode_optional_form_key_at(
              member.data, 4, k_coed, context, "COED global");
        }
        if (item_open) {
          alias.items.back().extra = std::move(extra);
        }
        item_has_extra = true;
        item_open = false;
        ++coed_seen;
        continue;
      }
      break;
    }
    if (seen != expected) {
      throw QuestError("QUST alias COCT does not match CNTO");
    }
    if (coed_seen != coed_total) {
      throw QuestError("QUST alias COED is missing preceding CNTO");
    }
  }

  const auto decode_unique_optional = [&](Signature signature,
                                          std::string_view label,
                                          std::optional<FormKey>& out) {
    const auto* member = unique_member(alias, signature, label);
    if (member == nullptr) return;
    out = decode_optional_form_key(*member, context, label);
  };
  decode_unique_optional(k_spor, "SPOR", alias.spectator_override);
  decode_unique_optional(k_ocor, "OCOR", alias.observe_dead_body_override);
  decode_unique_optional(k_gwor, "GWOR", alias.guard_warn_override);
  decode_unique_optional(k_ecor, "ECOR", alias.combat_override);

  const auto* alla = unique_member(alias, k_alla, "ALLA");
  if (alla != nullptr) {
    if (alla->data.size() % 8 != 0) {
      throw QuestError("QUST alias ALLA must be a multiple of eight bytes");
    }
    for (std::size_t offset = 0; offset < alla->data.size(); offset += 8) {
      auto keyword =
          decode_optional_form_key_at(alla->data, offset, k_alla, context, "ALLA");
      if (!keyword.has_value()) continue;
      alias.linked_aliases.push_back(
          QuestLinkedAlias{*keyword, read_i32(alla->data, offset + 4)});
    }
  }

  decode_unique_optional(k_aldn, "ALDN", alias.display_name);
  decode_unique_optional(k_alfv, "ALFV", alias.forced_voice);
  decode_unique_optional(k_aldi, "ALDI", alias.death_item);

  const auto decode_repeatable = [&](Signature signature,
                                     std::string_view label,
                                     std::vector<FormKey>& out) {
    for (const auto& member : alias.subrecords) {
      if (member.signature != signature) continue;
      if (auto key = decode_optional_form_key(member, context, label)) {
        out.push_back(*key);
      }
    }
  };
  decode_repeatable(k_alsp, "ALSP", alias.spells);
  decode_repeatable(k_alfc, "ALFC", alias.factions);
  decode_repeatable(k_alpc, "ALPC", alias.packages);

  decode_unique_optional(k_vtck, "VTCK", alias.voice_types);
}

bool has_reference_extras(const QuestAliasCreation& creation) {
  return !creation.keywords.empty() || !creation.items.empty() ||
         !creation.linked_aliases.empty() || !creation.spells.empty() ||
         !creation.factions.empty() || !creation.packages.empty() ||
         creation.spectator_override.has_value() ||
         creation.observe_dead_body_override.has_value() ||
         creation.guard_warn_override.has_value() ||
         creation.combat_override.has_value() ||
         creation.display_name.has_value() ||
         creation.forced_voice.has_value() ||
         creation.death_item.has_value() ||
         creation.voice_types.has_value();
}

void insert_alias_extras(QuestAlias& alias,
                         const QuestAliasCreation& creation,
                         const PluginFormIdContext& context) {
  if (!has_reference_extras(creation)) return;
  if (alias.kind != QuestAliasKind::reference) {
    throw QuestError(
        "QUST keywords, items, linked aliases, package-list overrides, "
        "display name, voice, death item, spells, factions, and packages "
        "are valid only on reference aliases");
  }
  std::vector<Subrecord> extra;
  if (!creation.keywords.empty()) {
    extra.push_back(make_subrecord(
        k_ksiz, write_u32(static_cast<std::uint32_t>(creation.keywords.size()))));
    std::vector<std::byte> packed;
    packed.reserve(creation.keywords.size() * 4);
    for (const auto& key : creation.keywords) {
      append_encoded_form_id(packed, key, context);
    }
    extra.push_back(make_subrecord(k_kwda, std::move(packed)));
  }
  if (!creation.items.empty()) {
    extra.push_back(make_subrecord(
        k_coct, write_u32(static_cast<std::uint32_t>(creation.items.size()))));
    for (const auto& item : creation.items) {
      if (item.count < 1) {
        throw QuestError("QUST alias item count must be at least 1");
      }
      std::vector<std::byte> cnto;
      append_encoded_form_id(cnto, item.item, context);
      const auto count = write_i32(item.count);
      cnto.insert(cnto.end(), count.begin(), count.end());
      extra.push_back(make_subrecord(k_cnto, std::move(cnto)));
      if (!item.extra.has_value()) continue;
      const auto& extra_data = *item.extra;
      if (extra_data.global.has_value() && extra_data.required_rank.has_value()) {
        throw QuestError("QUST alias COED cannot set both global and requiredRank");
      }
      if (!std::isfinite(extra_data.item_condition)) {
        throw QuestError("QUST alias COED item condition must be finite");
      }
      std::vector<std::byte> coed;
      if (extra_data.owner.has_value()) {
        append_encoded_form_id(coed, *extra_data.owner, context);
      } else {
        const auto none = write_u32(0);
        coed.insert(coed.end(), none.begin(), none.end());
      }
      if (extra_data.global.has_value()) {
        append_encoded_form_id(coed, *extra_data.global, context);
      } else if (extra_data.required_rank.has_value()) {
        const auto rank = write_i32(*extra_data.required_rank);
        coed.insert(coed.end(), rank.begin(), rank.end());
      } else {
        const auto unused = write_u32(0);
        coed.insert(coed.end(), unused.begin(), unused.end());
      }
      const auto condition = write_f32(extra_data.item_condition);
      coed.insert(coed.end(), condition.begin(), condition.end());
      extra.push_back(make_subrecord(k_coed, std::move(coed)));
    }
  }
  const auto emitOptional = [&](Signature signature, const std::optional<FormKey>& key) {
    if (!key.has_value()) return;
    extra.push_back(encoded_form_key(signature, *key, context));
  };
  emitOptional(k_spor, creation.spectator_override);
  emitOptional(k_ocor, creation.observe_dead_body_override);
  emitOptional(k_gwor, creation.guard_warn_override);
  emitOptional(k_ecor, creation.combat_override);
  if (!creation.linked_aliases.empty()) {
    std::vector<std::byte> alla;
    alla.reserve(creation.linked_aliases.size() * 8);
    for (const auto& linked : creation.linked_aliases) {
      append_encoded_form_id(alla, linked.keyword, context);
      const auto aliasBytes = write_i32(linked.alias);
      alla.insert(alla.end(), aliasBytes.begin(), aliasBytes.end());
    }
    extra.push_back(make_subrecord(k_alla, std::move(alla)));
  }
  emitOptional(k_aldn, creation.display_name);
  emitOptional(k_alfv, creation.forced_voice);
  emitOptional(k_aldi, creation.death_item);
  for (const auto& spell : creation.spells) {
    extra.push_back(encoded_form_key(k_alsp, spell, context));
  }
  for (const auto& faction : creation.factions) {
    extra.push_back(encoded_form_key(k_alfc, faction, context));
  }
  for (const auto& package : creation.packages) {
    extra.push_back(encoded_form_key(k_alpc, package, context));
  }
  emitOptional(k_vtck, creation.voice_types);
  insert_before_aled(alias, std::move(extra));
}

}  // namespace detail

}  // namespace fo4::forms
