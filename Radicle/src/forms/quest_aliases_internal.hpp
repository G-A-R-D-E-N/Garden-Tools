#pragma once

#include "fo4/binary/checked.hpp"
#include "fo4/forms/quest.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/signature.hpp"
#include "fo4/plugin_core/subrecord_ops.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::forms::detail {

struct AliasRange {
  std::size_t begin{0};
  std::size_t end{0};
  std::uint32_t id{0};
  bool collection{false};
};

struct AliasSection {
  std::size_t anam_index{0};
  std::size_t insertion_index{0};
  std::uint32_t next_alias_id{0};
  std::vector<AliasRange> ranges;
};

AliasSection inspect_quest_alias_section(const Record& record);

using binary::checked::read_u16;
using binary::checked::read_i16;
using binary::checked::read_u32;
using binary::checked::read_i32;
using binary::checked::read_f32;

inline std::vector<std::byte> write_u32(std::uint32_t value) {
  std::vector<std::byte> data(sizeof(value));
  std::memcpy(data.data(), &value, sizeof(value));
  return data;
}

inline std::vector<std::byte> write_i32(std::int32_t value) {
  std::vector<std::byte> data(sizeof(value));
  std::memcpy(data.data(), &value, sizeof(value));
  return data;
}

inline std::vector<std::byte> write_f32(float value) {
  std::vector<std::byte> data(sizeof(value));
  std::memcpy(data.data(), &value, sizeof(value));
  return data;
}

inline std::vector<std::byte> write_i16_u16(std::int16_t signed_value,
                                     std::uint16_t unsigned_value) {
  std::vector<std::byte> data(4);
  std::memcpy(data.data(), &signed_value, sizeof(signed_value));
  std::memcpy(data.data() + 2, &unsigned_value, sizeof(unsigned_value));
  return data;
}

inline std::vector<std::byte> zstring(std::string_view value) {
  if (value.find('\0') != std::string_view::npos) {
    throw QuestError("QUST alias name cannot contain an embedded NUL");
  }
  return encode_zstring(value);
}

inline std::uint32_t alias_id(const Subrecord& subrecord, std::string_view label) {
  if (subrecord.data.size() != 4) {
    throw QuestError("QUST " + std::string(label) + " alias ID must be exactly four bytes");
  }
  return read_u32(subrecord.data);
}

inline std::string alias_string(const Subrecord& subrecord, std::string_view label) {
  const auto nul = std::find(subrecord.data.begin(), subrecord.data.end(), std::byte{0});
  if (nul == subrecord.data.end()) {
    throw QuestError("QUST " + std::string(label) + " must be NUL-terminated");
  }
  return std::string(
      reinterpret_cast<const char*>(subrecord.data.data()),
      static_cast<std::size_t>(nul - subrecord.data.begin()));
}

inline bool is_alias_start(Signature signature) {
  return signature == sig_alst || signature == sig_alls || signature == sig_alcs;
}

inline QuestAliasKind alias_kind(Signature signature) {
  if (signature == sig_alst) return QuestAliasKind::reference;
  if (signature == sig_alls) return QuestAliasKind::location;
  return QuestAliasKind::collection;
}

inline const char* alias_label(QuestAliasKind kind) {
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

inline const Subrecord* unique_member(const QuestAlias& alias,
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

inline RawFormId raw_form_id(const Subrecord& member, std::string_view label) {
  if (member.data.size() != 4) {
    throw QuestError("QUST " + std::string(label) + " form ID must be exactly four bytes");
  }
  return RawFormId{read_u32(member.data)};
}

// Null outside decode_alias_semantics, where an unresolved link must throw.
inline thread_local std::vector<Signature>* t_unresolved = nullptr;

inline std::optional<FormKey> decode_optional_form_key(
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

inline FormKey decode_required_form_key(const Subrecord& member,
                                 const PluginFormIdContext& context,
                                 std::string_view label) {
  const auto resolved = decode_optional_form_key(member, context, label);
  if (!resolved.has_value()) {
    throw QuestError("QUST " + std::string(label) + " cannot be null");
  }
  return *resolved;
}

inline std::optional<FormKey> decode_optional_form_key_at(
    const std::vector<std::byte>& data,
    std::size_t offset,
    Signature signature,
    const PluginFormIdContext& context,
    std::string_view label) {
  if (offset + 4 > data.size()) {
    throw QuestError("QUST " + std::string(label) + " form ID must be exactly four bytes");
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

inline Subrecord encoded_form_key(Signature signature,
                           const FormKey& key,
                           const PluginFormIdContext& context) {
  try {
    return make_subrecord(signature, write_u32(encode_plugin_form_id(key, context).value()));
  } catch (const FormIdError& error) {
    throw QuestError("QUST alias FormLink cannot be encoded: " + std::string(error.what()));
  }
}

inline Subrecord encoded_optional_form_key(Signature signature,
                                    const std::optional<FormKey>& key,
                                    const PluginFormIdContext& context) {
  if (!key.has_value()) {
    return make_subrecord(signature, write_u32(0));
  }
  return encoded_form_key(signature, *key, context);
}

inline void insert_before_aled(QuestAlias& alias, std::vector<Subrecord> members) {
  if (alias.subrecords.empty() || alias.subrecords.back().signature != sig_aled) {
    throw QuestError("QUST alias encoder lost required ALED boundary");
  }
  alias.subrecords.insert(
      alias.subrecords.end() - 1,
      std::make_move_iterator(members.begin()),
      std::make_move_iterator(members.end()));
}

void decode_alias_semantics(QuestAlias& alias,
                            const PluginFormIdContext* context,
                            const Plugin* plugin,
                            const RecordIndex* index,
                            const FormLinkTypeLookup* owner_types = nullptr);
void decode_alias_extras(QuestAlias& alias,
                         const PluginFormIdContext& context,
                         const Plugin* plugin,
                         const RecordIndex* index,
                         const FormLinkTypeLookup* owner_types = nullptr);
bool has_reference_extras(const QuestAliasCreation& creation);
void insert_alias_extras(QuestAlias& alias,
                         const QuestAliasCreation& creation,
                         const PluginFormIdContext& context);

}  // namespace fo4::forms::detail
