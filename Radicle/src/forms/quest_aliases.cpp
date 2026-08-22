#include "fo4/forms/quest.hpp"

#include "fo4/plugin/reader.hpp"
#include "fo4/signature.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

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

void decode_alias_semantics(QuestAlias& alias) {
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
}

}  // namespace

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

  for (std::size_t index = *anam_index + 1; index < record->subrecords.size();) {
    const auto& start = record->subrecords[index];
    if (!is_alias_start(start.signature)) {
      if (start.signature == k_aled || start.signature == k_almi) {
        throw QuestError(
            "QUST contains an orphan " + start.signature.to_string() +
            " outside an alias container");
      }
      ++index;
      continue;
    }

    QuestAlias alias;
    alias.kind = alias_kind(start.signature);
    alias.id = alias_id(start, alias_label(alias.kind));

    if (alias.kind == QuestAliasKind::collection) {
      alias.subrecords.push_back(start);
      ++index;
      if (index < record->subrecords.size() &&
          record->subrecords[index].signature == k_almi) {
        alias.subrecords.push_back(record->subrecords[index]);
        ++index;
      }
      decode_alias_semantics(alias);
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
    decode_alias_semantics(alias);
    result.aliases.push_back(std::move(alias));
  }

  return result;
}

}  // namespace fo4::forms
