#include "fo4/forms/quest.hpp"

#include "fo4/plugin/reader.hpp"
#include "fo4/schema/ctda.hpp"
#include "fo4/signature.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace fo4::forms {
namespace {

constexpr auto k_qust = Signature::from_string("QUST");
constexpr auto k_anam = Signature::from_string("ANAM");
constexpr auto k_alst = Signature::from_string("ALST");
constexpr auto k_alls = Signature::from_string("ALLS");
constexpr auto k_alcs = Signature::from_string("ALCS");
constexpr auto k_alid = Signature::from_string("ALID");
constexpr auto k_fnam = Signature::from_string("FNAM");
constexpr auto k_alfi = Signature::from_string("ALFI");
constexpr auto k_almi = Signature::from_string("ALMI");
constexpr auto k_alfr = Signature::from_string("ALFR");
constexpr auto k_alua = Signature::from_string("ALUA");
constexpr auto k_alfa = Signature::from_string("ALFA");
constexpr auto k_alrt = Signature::from_string("ALRT");
constexpr auto k_alco = Signature::from_string("ALCO");
constexpr auto k_alca = Signature::from_string("ALCA");
constexpr auto k_alna = Signature::from_string("ALNA");
constexpr auto k_alnt = Signature::from_string("ALNT");
constexpr auto k_alfe = Signature::from_string("ALFE");
constexpr auto k_alfd = Signature::from_string("ALFD");
constexpr auto k_alcc = Signature::from_string("ALCC");
constexpr auto k_alfl = Signature::from_string("ALFL");
constexpr auto k_knam = Signature::from_string("KNAM");
constexpr auto k_aleq = Signature::from_string("ALEQ");
constexpr auto k_alea = Signature::from_string("ALEA");
constexpr auto k_aled = Signature::from_string("ALED");
constexpr auto k_ksiz = Signature::from_string("KSIZ");
constexpr auto k_kwda = Signature::from_string("KWDA");
constexpr auto k_coct = Signature::from_string("COCT");
constexpr auto k_cnto = Signature::from_string("CNTO");
constexpr auto k_alla = Signature::from_string("ALLA");
constexpr auto k_spor = Signature::from_string("SPOR");
constexpr auto k_ocor = Signature::from_string("OCOR");
constexpr auto k_gwor = Signature::from_string("GWOR");
constexpr auto k_ecor = Signature::from_string("ECOR");
constexpr auto k_aldn = Signature::from_string("ALDN");
constexpr auto k_alfv = Signature::from_string("ALFV");
constexpr auto k_aldi = Signature::from_string("ALDI");
constexpr auto k_alsp = Signature::from_string("ALSP");
constexpr auto k_alfc = Signature::from_string("ALFC");
constexpr auto k_alpc = Signature::from_string("ALPC");
constexpr auto k_vtck = Signature::from_string("VTCK");
constexpr auto k_coed = Signature::from_string("COED");

std::uint16_t read_u16(const std::vector<std::byte>& data, std::size_t offset = 0) {
  std::uint16_t value = 0;
  std::memcpy(&value, data.data() + offset, sizeof(value));
  return value;
}

std::int16_t read_i16(const std::vector<std::byte>& data, std::size_t offset = 0) {
  std::int16_t value = 0;
  std::memcpy(&value, data.data() + offset, sizeof(value));
  return value;
}

std::uint32_t read_u32(const std::vector<std::byte>& data) {
  std::uint32_t value = 0;
  std::memcpy(&value, data.data(), sizeof(value));
  return value;
}

std::int32_t read_i32(const std::vector<std::byte>& data, std::size_t offset = 0) {
  std::int32_t value = 0;
  std::memcpy(&value, data.data() + offset, sizeof(value));
  return value;
}

std::vector<std::byte> write_u32(std::uint32_t value) {
  std::vector<std::byte> data(sizeof(value));
  std::memcpy(data.data(), &value, sizeof(value));
  return data;
}

std::vector<std::byte> write_i32(std::int32_t value) {
  std::vector<std::byte> data(sizeof(value));
  std::memcpy(data.data(), &value, sizeof(value));
  return data;
}

std::vector<std::byte> write_i16_u16(std::int16_t signed_value,
                                     std::uint16_t unsigned_value) {
  std::vector<std::byte> data(4);
  std::memcpy(data.data(), &signed_value, sizeof(signed_value));
  std::memcpy(data.data() + 2, &unsigned_value, sizeof(unsigned_value));
  return data;
}

std::vector<std::byte> zstring(std::string_view value) {
  if (value.find('\0') != std::string_view::npos) {
    throw QuestError("QUST alias name cannot contain an embedded NUL");
  }
  std::vector<std::byte> data;
  data.reserve(value.size() + 1);
  for (const char ch : value) {
    data.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  }
  data.push_back(std::byte{0});
  return data;
}

Subrecord make_subrecord(Signature signature, std::vector<std::byte> data = {}) {
  return Subrecord{signature, std::move(data), false};
}

std::uint32_t alias_id(const Subrecord& subrecord, std::string_view label) {
  if (subrecord.data.size() != 4) {
    throw QuestError("QUST " + std::string(label) + " alias ID must be exactly four bytes");
  }
  return read_u32(subrecord.data);
}

std::string alias_string(const Subrecord& subrecord, std::string_view label) {
  const auto nul = std::find(subrecord.data.begin(), subrecord.data.end(), std::byte{0});
  if (nul == subrecord.data.end()) {
    throw QuestError("QUST " + std::string(label) + " must be NUL-terminated");
  }
  return std::string(
      reinterpret_cast<const char*>(subrecord.data.data()),
      static_cast<std::size_t>(nul - subrecord.data.begin()));
}

bool is_alias_start(Signature signature) {
  return signature == k_alst || signature == k_alls || signature == k_alcs;
}

QuestAliasKind alias_kind(Signature signature) {
  if (signature == k_alst) return QuestAliasKind::reference;
  if (signature == k_alls) return QuestAliasKind::location;
  return QuestAliasKind::collection;
}

const char* alias_label(QuestAliasKind kind) {
  switch (kind) {
    case QuestAliasKind::reference:
      return "reference";
    case QuestAliasKind::location:
      return "location";
    case QuestAliasKind::collection:
      return "collection";
  }
  return "unknown";
}

const Subrecord* unique_member(const QuestAlias& alias,
                               Signature signature,
                               std::string_view label) {
  const Subrecord* found = nullptr;
  for (const auto& member : alias.subrecords) {
    if (member.signature != signature) continue;
    if (found != nullptr) {
      throw QuestError(
          "QUST " + std::string(alias_label(alias.kind)) +
          " alias contains duplicate " + std::string(label));
    }
    found = &member;
  }
  return found;
}

RawFormId raw_form_id(const Subrecord& member, std::string_view label) {
  if (member.data.size() != 4) {
    throw QuestError("QUST " + std::string(label) + " FormID must be exactly four bytes");
  }
  return RawFormId{read_u32(member.data)};
}

// Collects the signatures of links that could not be resolved, so a dangling
// reference degrades the one field instead of the whole quest.
thread_local std::vector<Signature>* t_unresolved = nullptr;

std::optional<FormKey> decode_optional_form_key(
    const Subrecord& member,
    const PluginFormIdContext& context,
    std::string_view label) {
  const auto raw = raw_form_id(member, label);
  if (raw.is_null()) return std::nullopt;
  try {
    return resolve_plugin_form_id(raw, context);
  } catch (const FormIdError& error) {
    if (t_unresolved != nullptr) {
      t_unresolved->push_back(member.signature);
      return std::nullopt;
    }
    throw QuestError("QUST " + std::string(label) + " cannot resolve: " + error.what());
  }
}

FormKey decode_required_form_key(const Subrecord& member,
                                 const PluginFormIdContext& context,
                                 std::string_view label) {
  const auto resolved = decode_optional_form_key(member, context, label);
  if (!resolved.has_value()) {
    throw QuestError("QUST " + std::string(label) + " cannot be null");
  }
  return *resolved;
}

std::optional<FormKey> decode_optional_form_key_at(
    const std::vector<std::byte>& data,
    std::size_t offset,
    Signature signature,
    const PluginFormIdContext& context,
    std::string_view label) {
  if (offset + 4 > data.size()) {
    throw QuestError("QUST " + std::string(label) + " FormID must be exactly four bytes");
  }
  std::uint32_t raw = 0;
  std::memcpy(&raw, data.data() + offset, sizeof(raw));
  const RawFormId id{raw};
  if (id.is_null()) return std::nullopt;
  try {
    return resolve_plugin_form_id(id, context);
  } catch (const FormIdError& error) {
    if (t_unresolved != nullptr) {
      t_unresolved->push_back(signature);
      return std::nullopt;
    }
    throw QuestError("QUST " + std::string(label) + " cannot resolve: " + error.what());
  }
}

Subrecord encoded_form_key(Signature signature,
                           const FormKey& key,
                           const PluginFormIdContext& context) {
  try {
    return make_subrecord(signature, write_u32(encode_plugin_form_id(key, context).value()));
  } catch (const FormIdError& error) {
    throw QuestError("QUST alias FormLink cannot be encoded: " + std::string(error.what()));
  }
}

Subrecord encoded_optional_form_key(Signature signature,
                                    const std::optional<FormKey>& key,
                                    const PluginFormIdContext& context) {
  if (!key.has_value()) {
    return make_subrecord(signature, write_u32(0));
  }
  return encoded_form_key(signature, *key, context);
}

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
  const auto* near_alias = unique_member(alias, k_alna, "ALNA");
  const auto* near_type = unique_member(alias, k_alnt, "ALNT");
  const auto* event = unique_member(alias, k_alfe, "ALFE");
  const auto* event_data = unique_member(alias, k_alfd, "ALFD");
  const auto* closest = unique_member(alias, k_alcc, "ALCC");

  const bool has_specific = specific != nullptr;
  const bool has_unique = unique_actor != nullptr;
  const bool has_location = location_alias != nullptr || location_type != nullptr;
  const bool has_external = external_quest != nullptr || external_alias != nullptr;
  const bool has_create = create_object != nullptr || create_alias != nullptr;
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
    alias.fill = QuestCreateReferenceFill{
        .object = decode_optional_form_key(*create_object, context, "ALCO Object"),
        .alias = read_i16(create_alias->data),
        .create_mode = read_u16(create_alias->data, 2),
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

void decode_alias_extras(QuestAlias& alias, const PluginFormIdContext& context) {
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

  bool has_coed = false;
  std::size_t cnto_count = 0;
  for (const auto& member : alias.subrecords) {
    if (member.signature == k_coed) has_coed = true;
    if (member.signature == k_cnto) ++cnto_count;
  }
  const auto* coct = unique_member(alias, k_coct, "COCT");
  if (coct == nullptr) {
    if (cnto_count != 0) {
      throw QuestError("QUST alias CNTO is missing COCT");
    }
  } else if (!has_coed) {
    if (coct->data.size() != 4) {
      throw QuestError("QUST alias COCT must be exactly four bytes");
    }
    const auto expected = read_u32(coct->data);
    std::size_t seen = 0;
    bool after_coct = false;
    for (const auto& member : alias.subrecords) {
      if (member.signature == k_coct) {
        after_coct = true;
        continue;
      }
      if (!after_coct) continue;
      if (member.signature != k_cnto) break;
      if (member.data.size() != 8) {
        throw QuestError("QUST alias CNTO must be exactly eight bytes");
      }
      if (auto key = decode_optional_form_key_at(
              member.data, 0, k_cnto, context, "CNTO")) {
        alias.items.push_back(QuestAliasItem{*key, read_i32(member.data, 4)});
      }
      ++seen;
    }
    if (seen != expected) {
      throw QuestError("QUST alias COCT does not match CNTO");
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

void decode_alias_semantics(QuestAlias& alias,
                            const PluginFormIdContext* context) {
  // Scope the unresolved-link collector to this alias so a dangling FormID
  // degrades one field rather than aborting the quest.
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
      throw QuestError("QUST FormLink-bearing alias fill requires plugin FormID context");
    }
    if (alias.kind == QuestAliasKind::reference) {
      decode_reference_fill(alias, *context);
    } else {
      decode_location_fill(alias, *context);
    }
  }

  if (alias.kind == QuestAliasKind::reference && context != nullptr) {
    decode_alias_extras(alias, *context);
  }
}

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
          members.push_back(encoded_optional_form_key(k_alco, value.object, context));
          members.push_back(make_subrecord(
              k_alca, write_i16_u16(value.alias, value.create_mode)));
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

void insert_before_aled(QuestAlias& alias, std::vector<Subrecord> members) {
  if (alias.subrecords.empty() || alias.subrecords.back().signature != k_aled) {
    throw QuestError("QUST alias encoder lost required ALED boundary");
  }
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
  decode_alias_semantics(alias, nullptr);
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
  decode_alias_semantics(alias, &context);
  return alias;
}

QuestAliases decode_quest_aliases(const Plugin& plugin, const FormKey& key) {
  const auto* record = find_record(plugin, key);
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
    // A partial override that does not touch aliases carries neither ANAM nor
    // any alias markers -- seven shipped DLC overrides of Fallout4.esm quests
    // are exactly EDID(+VMAD). Those have no alias array to decode, so report
    // an empty one rather than rejecting the record.
    //
    // Alias markers without ANAM stay fail-closed: next_alias_id would be
    // unknown and the array's start position ambiguous.
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
      decode_alias_semantics(alias, &context);
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
    decode_alias_semantics(alias, &context);
    result.aliases.push_back(std::move(alias));
  }

  // xEdit places optional NNAM/GNAM/SNAM after the single contiguous Alias
  // array. Once a non-alias tail member is reached, a later alias marker is a
  // malformed second array and must not be silently accepted.
  for (; index < record->subrecords.size(); ++index) {
    const auto signature = record->subrecords[index].signature;
    if (is_alias_start(signature) || signature == k_aled || signature == k_almi) {
      throw QuestError("QUST alias member appears after the Alias array tail boundary");
    }
  }

  return result;
}

}  // namespace fo4::forms
