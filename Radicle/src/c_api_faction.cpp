#include "c_api_forms_internal.hpp"
#include "fo4/c_api_faction.h"
#include "fo4/forms/faction.hpp"
#include "fo4/plugin/reader.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace radicle_c_detail;
using namespace radicle_c_env_detail;
using namespace radicle_c_forms_detail;

struct RankHolder {
  radicle_faction_ranks view{};
  std::vector<radicle_faction_rank> ranks;
  std::vector<std::vector<uint8_t>> insignia;
  TextStore text;
};

struct RelationHolder {
  radicle_faction_relations view{};
  std::vector<radicle_faction_relation> relations;
};

void fill_title(const fo4::schema::LocalizedString& source,
                TextStore& text,
                radicle_name_kind& kind,
                const char*& value,
                uint32_t& string_id) {
  if (source.id) {
    kind = RADICLE_NAME_STRING_ID;
    string_id = *source.id;
    return;
  }
  if (source.raw_id) {
    kind = RADICLE_NAME_MALFORMED;
    return;
  }
  kind = RADICLE_NAME_INLINE;
  value = text.keep(source.text);
}

radicle_status to_title(radicle_name_kind kind,
                        const char* text,
                        uint32_t string_id,
                        std::optional<fo4::schema::LocalizedString>& out) {
  out.reset();
  switch (kind) {
    case RADICLE_NAME_ABSENT:
      return RADICLE_STATUS_OK;
    case RADICLE_NAME_INLINE: {
      if (text == nullptr) {
        return fail(RADICLE_STATUS_INVALID_ARGUMENT, "an inline title requires text");
      }
      if (!text_representable(text)) return bad_text();
      fo4::schema::LocalizedString value;
      value.text = fo4::text::utf8_to_windows1252(text);
      out = std::move(value);
      return RADICLE_STATUS_OK;
    }
    case RADICLE_NAME_STRING_ID: {
      fo4::schema::LocalizedString value;
      value.id = string_id;
      out = std::move(value);
      return RADICLE_STATUS_OK;
    }
    default:
      return fail(RADICLE_STATUS_INVALID_ARGUMENT,
                  "a title kind must be absent, inline text or a string ID");
  }
}

radicle_status to_rank(const radicle_faction_rank_input& input,
                       uint32_t number,
                       fo4::forms::FactionRank& out) {
  out.number = number;
  if (const auto status = to_title(input.male_title_kind, input.male_title_text,
                                   input.male_title_string_id, out.male_title);
      status != RADICLE_STATUS_OK) {
    return status;
  }
  if (const auto status = to_title(input.female_title_kind, input.female_title_text,
                                   input.female_title_string_id, out.female_title);
      status != RADICLE_STATUS_OK) {
    return status;
  }
  if (input.insignia != nullptr) {
    std::vector<std::byte> bytes;
    bytes.reserve(input.insignia_size);
    for (size_t index = 0; index < input.insignia_size; ++index) {
      bytes.push_back(std::byte{input.insignia[index]});
    }
    out.insignia = std::move(bytes);
  }
  return RADICLE_STATUS_OK;
}

fo4::forms::FactionRelation to_relation(const fo4::FormKey& faction, int32_t modifier,
                                        uint32_t reaction) {
  fo4::forms::FactionRelation relation;
  relation.faction = faction;
  relation.modifier = modifier;
  relation.reaction = static_cast<fo4::forms::FactionCombatReaction>(reaction);
  return relation;
}

radicle_status require_faction(radicle_form_key faction, fo4::FormKey& out) {
  const auto key = to_form_key(faction);
  if (!key) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "a valid faction key is required");
  out = *key;
  return RADICLE_STATUS_OK;
}

}  // namespace

extern "C" {

radicle_status radicle_faction_relations_decode(const radicle_env* env,
                                                const radicle_record* record,
                                                radicle_faction_relations** out_relations) {
  clear_error();
  if (out_relations != nullptr) *out_relations = nullptr;
  if (const auto status = require_env_record(env, record, out_relations); status != RADICLE_STATUS_OK) return status;
  return guarded("unknown exception while decoding FACT relations", [&] {
    try {
      const auto& ref = *to_ref(record);
      const auto data = fo4::forms::decode_faction_relations(env->value.plugin(ref.plugin),
                                                             ref.record->form_key);
      auto holder = std::make_unique<RelationHolder>();
      holder->relations.reserve(data.size());
      for (const auto& relation : data) {
        radicle_faction_relation row{};
        row.faction = to_c_key(*env, relation.faction);
        row.modifier = relation.modifier;
        row.reaction = static_cast<uint32_t>(relation.reaction);
        holder->relations.push_back(row);
      }
      holder->view.relations = holder->relations.empty() ? nullptr : holder->relations.data();
      holder->view.relation_count = holder->relations.size();
      holder->view.internal = holder.get();
      *out_relations = &holder.release()->view;
      return RADICLE_STATUS_OK;
    } catch (const fo4::forms::FactionError& error) {
      return fail_exception(RADICLE_STATUS_PARSE_ERROR, error);
    }
  });
}

void radicle_faction_relations_free(radicle_faction_relations* relations) {
  clear_error();
  if (relations == nullptr) return;
  delete static_cast<RelationHolder*>(relations->internal);
}

radicle_status radicle_faction_ranks_decode(const radicle_env* env,
                                            const radicle_record* record,
                                            radicle_faction_ranks** out_ranks) {
  clear_error();
  if (out_ranks != nullptr) *out_ranks = nullptr;
  if (const auto status = require_env_record(env, record, out_ranks); status != RADICLE_STATUS_OK) return status;
  return guarded("unknown exception while decoding FACT ranks", [&] {
    try {
      const auto& ref = *to_ref(record);
      const auto data = fo4::forms::decode_faction_ranks(env->value.plugin(ref.plugin),
                                                         ref.record->form_key);
      auto holder = std::make_unique<RankHolder>();
      holder->ranks.reserve(data.size());
      holder->insignia.resize(data.size());
      for (size_t index = 0; index < data.size(); ++index) {
        const auto& rank = data[index];
        radicle_faction_rank row{};
        row.number = rank.number;
        if (rank.male_title) {
          fill_title(*rank.male_title, holder->text, row.male_title_kind, row.male_title_text,
                     row.male_title_string_id);
        }
        if (rank.female_title) {
          fill_title(*rank.female_title, holder->text, row.female_title_kind, row.female_title_text,
                     row.female_title_string_id);
        }
        if (rank.insignia) {
          auto& bytes = holder->insignia[index];
          bytes.reserve(rank.insignia->size());
          for (const auto value : *rank.insignia) {
            bytes.push_back(std::to_integer<uint8_t>(value));
          }
          row.insignia = bytes.empty() ? nullptr : bytes.data();
          row.insignia_size = bytes.size();
        }
        holder->ranks.push_back(row);
      }
      holder->view.ranks = holder->ranks.empty() ? nullptr : holder->ranks.data();
      holder->view.rank_count = holder->ranks.size();
      holder->view.internal = holder.get();
      *out_ranks = &holder.release()->view;
      return RADICLE_STATUS_OK;
    } catch (const fo4::forms::FactionError& error) {
      return fail_exception(RADICLE_STATUS_PARSE_ERROR, error);
    }
  });
}

void radicle_faction_ranks_free(radicle_faction_ranks* ranks) {
  clear_error();
  if (ranks == nullptr) return;
  delete static_cast<RankHolder*>(ranks->internal);
}

radicle_status radicle_plugin_create_faction(radicle_plugin* plugin,
                                             const char* editor_id,
                                             const char* full_name,
                                             uint32_t flags,
                                             radicle_form_key* out_key) {
  clear_error();
  if (out_key != nullptr) *out_key = {nullptr, 0};
  if (plugin == nullptr || out_key == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin and out_key are required");
  }
  if (!texts_representable({editor_id, full_name})) return bad_text();
  return guarded("unknown exception while creating FACT", [&] {
    try {
      const auto key = fo4::forms::create_faction(plugin->value, optional_view(editor_id),
                                                  optional_view(full_name), flags);
      *out_key = {plugin->value.key.filename().data(), key.local().value()};
      return RADICLE_STATUS_OK;
    } catch (const fo4::forms::FactionError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    } catch (const fo4::FormIdError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    }
  });
}

radicle_status radicle_plugin_add_faction_relation(radicle_plugin* plugin,
                                                   radicle_form_key key,
                                                   radicle_form_key faction,
                                                   int32_t modifier,
                                                   uint32_t reaction) {
  clear_error();
  std::optional<fo4::FormKey> form_key;
  if (const auto status = require_plugin_record(plugin, key, form_key); status != RADICLE_STATUS_OK) return status;
  fo4::FormKey target;
  if (const auto status = require_faction(faction, target); status != RADICLE_STATUS_OK) return status;
  return guarded("unknown exception while adding a FACT relation", [&] {
    try {
      fo4::forms::add_faction_relation(plugin->value, *form_key, to_relation(target, modifier, reaction));
      return RADICLE_STATUS_OK;
    } catch (const fo4::forms::FactionError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    } catch (const fo4::FormIdError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    }
  });
}

radicle_status radicle_plugin_set_faction_relation(radicle_plugin* plugin,
                                                   radicle_form_key key,
                                                   size_t index,
                                                   radicle_form_key faction,
                                                   int32_t modifier,
                                                   uint32_t reaction) {
  clear_error();
  std::optional<fo4::FormKey> form_key;
  if (const auto status = require_plugin_record(plugin, key, form_key); status != RADICLE_STATUS_OK) return status;
  fo4::FormKey target;
  if (const auto status = require_faction(faction, target); status != RADICLE_STATUS_OK) return status;
  return guarded("unknown exception while setting a FACT relation", [&] {
    try {
      fo4::forms::set_faction_relation(plugin->value, *form_key, index,
                                       to_relation(target, modifier, reaction));
      return RADICLE_STATUS_OK;
    } catch (const fo4::forms::FactionError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    } catch (const fo4::FormIdError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    }
  });
}

radicle_status radicle_plugin_remove_faction_relation(radicle_plugin* plugin,
                                                      radicle_form_key key,
                                                      size_t index) {
  clear_error();
  std::optional<fo4::FormKey> form_key;
  if (const auto status = require_plugin_record(plugin, key, form_key); status != RADICLE_STATUS_OK) return status;
  return guarded("unknown exception while removing a FACT relation", [&] {
    try {
      fo4::forms::remove_faction_relation(plugin->value, *form_key, index);
      return RADICLE_STATUS_OK;
    } catch (const fo4::forms::FactionError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    } catch (const fo4::FormIdError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    }
  });
}

radicle_status radicle_plugin_add_faction_rank(radicle_plugin* plugin,
                                               radicle_form_key key,
                                               uint32_t number,
                                               const radicle_faction_rank_input* titles) {
  clear_error();
  if (titles == nullptr) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "titles are required");
  std::optional<fo4::FormKey> form_key;
  if (const auto status = require_plugin_record(plugin, key, form_key); status != RADICLE_STATUS_OK) return status;
  fo4::forms::FactionRank rank;
  if (const auto status = to_rank(*titles, number, rank); status != RADICLE_STATUS_OK) return status;
  return guarded("unknown exception while adding a FACT rank", [&] {
    try {
      fo4::forms::add_faction_rank(plugin->value, *form_key, rank);
      return RADICLE_STATUS_OK;
    } catch (const fo4::forms::FactionError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    } catch (const fo4::FormIdError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    }
  });
}

radicle_status radicle_plugin_set_faction_rank(radicle_plugin* plugin,
                                               radicle_form_key key,
                                               size_t index,
                                               uint32_t number,
                                               const radicle_faction_rank_input* titles) {
  clear_error();
  if (titles == nullptr) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "titles are required");
  std::optional<fo4::FormKey> form_key;
  if (const auto status = require_plugin_record(plugin, key, form_key); status != RADICLE_STATUS_OK) return status;
  fo4::forms::FactionRank rank;
  if (const auto status = to_rank(*titles, number, rank); status != RADICLE_STATUS_OK) return status;
  return guarded("unknown exception while setting a FACT rank", [&] {
    try {
      fo4::forms::set_faction_rank(plugin->value, *form_key, index, rank);
      return RADICLE_STATUS_OK;
    } catch (const fo4::forms::FactionError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    } catch (const fo4::FormIdError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    }
  });
}

radicle_status radicle_plugin_remove_faction_rank(radicle_plugin* plugin,
                                                  radicle_form_key key,
                                                  size_t index) {
  clear_error();
  std::optional<fo4::FormKey> form_key;
  if (const auto status = require_plugin_record(plugin, key, form_key); status != RADICLE_STATUS_OK) return status;
  return guarded("unknown exception while removing a FACT rank", [&] {
    try {
      fo4::forms::remove_faction_rank(plugin->value, *form_key, index);
      return RADICLE_STATUS_OK;
    } catch (const fo4::forms::FactionError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    } catch (const fo4::FormIdError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    }
  });
}

}  // extern "C"
