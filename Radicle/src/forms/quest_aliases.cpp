#include "fo4/forms/quest.hpp"

#include "fo4/plugin/reader.hpp"
#include "fo4/signature.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <optional>
#include <set>
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
constexpr auto k_alfl = Signature::from_string("ALFL");
constexpr auto k_knam = Signature::from_string("KNAM");
constexpr auto k_aleq = Signature::from_string("ALEQ");
constexpr auto k_alea = Signature::from_string("ALEA");
constexpr auto k_aled = Signature::from_string("ALED");

std::uint32_t read_u32(const std::vector<std::byte>& data) {
  std::uint32_t value = 0;
  std::memcpy(&value, data.data(), sizeof(value));
  return value;
}

std::int32_t read_i32(const std::vector<std::byte>& data) {
  std::int32_t value = 0;
  std::memcpy(&value, data.data(), sizeof(value));
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

std::optional<FormKey> decode_optional_form_key(
    const Subrecord& member,
    const PluginFormIdContext& context,
    std::string_view label) {
  const auto raw = raw_form_id(member, label);
  if (raw.is_null()) return std::nullopt;
  try {
    return resolve_plugin_form_id(raw, context);
  } catch (const FormIdError& error) {
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

  const bool has_specific = specific != nullptr;
  const bool has_unique = unique_actor != nullptr;
  const bool has_location = location_alias != nullptr || location_type != nullptr;
  const bool has_external = external_quest != nullptr || external_alias != nullptr;
  const auto variants = static_cast<unsigned>(has_specific) +
                        static_cast<unsigned>(has_unique) +
                        static_cast<unsigned>(has_location) +
                        static_cast<unsigned>(has_external);
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
  }
}

void decode_location_fill(QuestAlias& alias,
                          const PluginFormIdContext& context) {
  const auto* specific = unique_member(alias, k_alfl, "ALFL");
  const auto* reference_alias = unique_member(alias, k_alfa, "ALFA");
  const auto* keyword = unique_member(alias, k_knam, "KNAM");
  const auto* external_quest = unique_member(alias, k_aleq, "ALEQ");
  const auto* external_alias = unique_member(alias, k_alea, "ALEA");

  const bool has_specific = specific != nullptr;
  const bool has_reference = reference_alias != nullptr || keyword != nullptr;
  const bool has_external = external_quest != nullptr || external_alias != nullptr;
  const auto variants = static_cast<unsigned>(has_specific) +
                        static_cast<unsigned>(has_reference) +
                        static_cast<unsigned>(has_external);
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
  }
}

void decode_alias_semantics(QuestAlias& alias,
                            const PluginFormIdContext* context) {
  if (alias.kind == QuestAliasKind::collection) {
    if (const auto* max_fill = unique_member(alias, k_almi, "ALMI")) {
      if (max_fill->data.size() != 1) {
        throw QuestError("QUST collection alias ALMI must be exactly one byte");
      }
      alias.max_initial_fill_count =
          std::to_integer<std::uint8_t>(max_fill->data.front());
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

  const bool reference_fill_present =
      unique_member(alias, k_alfr, "ALFR") != nullptr ||
      unique_member(alias, k_alua, "ALUA") != nullptr ||
      unique_member(alias, k_alfa, "ALFA") != nullptr ||
      unique_member(alias, k_alrt, "ALRT") != nullptr ||
      unique_member(alias, k_aleq, "ALEQ") != nullptr ||
      unique_member(alias, k_alea, "ALEA") != nullptr;
  const bool location_fill_present =
      unique_member(alias, k_alfl, "ALFL") != nullptr ||
      unique_member(alias, k_alfa, "ALFA") != nullptr ||
      unique_member(alias, k_knam, "KNAM") != nullptr ||
      unique_member(alias, k_aleq, "ALEQ") != nullptr ||
      unique_member(alias, k_alea, "ALEA") != nullptr;

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
}

QuestAlias encode_base_alias(const QuestAliasCreation& creation) {
  QuestAlias alias;
  alias.kind = creation.kind;
  alias.id = creation.id;

  if (creation.kind == QuestAliasKind::collection) {
    if (creation.name.has_value() || creation.flags.has_value() ||
        creation.force_into_alias.has_value()) {
      throw QuestError(
          "QUST collection aliases do not use ALID, FNAM, or ALFI fields");
    }
    if (!std::holds_alternative<std::monostate>(creation.fill)) {
      throw QuestError("QUST collection alias Fill Type is not modeled by ALCS/ALMI");
    }
    alias.subrecords.push_back(make_subrecord(k_alcs, write_u32(creation.id)));
    if (creation.max_initial_fill_count.has_value()) {
      alias.subrecords.push_back(make_subrecord(
          k_almi,
          std::vector<std::byte>{std::byte{*creation.max_initial_fill_count}}));
    }
    decode_alias_semantics(alias, nullptr);
    return alias;
  }

  if (!creation.flags.has_value()) {
    throw QuestError("QUST reference/location alias creation requires FNAM Flags");
  }
  if (creation.kind == QuestAliasKind::reference && !creation.name.has_value()) {
    throw QuestError("QUST reference alias creation requires ALID Alias Name");
  }
  if (creation.max_initial_fill_count.has_value()) {
    throw QuestError("QUST reference/location aliases do not use collection ALMI");
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
        }
      },
      fill);

  alias.subrecords.insert(
      alias.subrecords.end() - 1,
      std::make_move_iterator(members.begin()),
      std::make_move_iterator(members.end()));
}

}  // namespace

QuestAlias encode_quest_alias(const QuestAliasCreation& creation) {
  if (!std::holds_alternative<std::monostate>(creation.fill)) {
    throw QuestError("QUST FormLink-bearing Fill Type encoding requires a plugin context");
  }
  auto alias = encode_base_alias(creation);
  decode_alias_semantics(alias, nullptr);
  return alias;
}

QuestAlias encode_quest_alias(const Plugin& plugin,
                              const QuestAliasCreation& creation) {
  auto alias = encode_base_alias(creation);
  const auto context = plugin_form_id_context(plugin);
  insert_alias_fill(alias, creation.fill, context);
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
    alias.id = alias_id(start, alias_label(alias.kind));
    if (!alias_ids.insert(alias.id).second) {
      throw QuestError("QUST contains duplicate Alias ID " + std::to_string(alias.id));
    }

    if (alias.kind == QuestAliasKind::collection) {
      alias.subrecords.push_back(start);
      ++index;
      if (index < record->subrecords.size() &&
          record->subrecords[index].signature == k_almi) {
        alias.subrecords.push_back(record->subrecords[index]);
        ++index;
      }
      decode_alias_semantics(alias, &context);
      result.aliases.push_back(std::move(alias));
      continue;
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
