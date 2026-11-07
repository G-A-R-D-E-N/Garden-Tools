#pragma once

#include "fo4/forms/quest.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/signature.hpp"

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

inline constexpr auto k_qust = Signature::from_string("QUST");
inline constexpr auto k_anam = Signature::from_string("ANAM");
inline constexpr auto k_alst = Signature::from_string("ALST");
inline constexpr auto k_alls = Signature::from_string("ALLS");
inline constexpr auto k_alcs = Signature::from_string("ALCS");
inline constexpr auto k_alid = Signature::from_string("ALID");
inline constexpr auto k_fnam = Signature::from_string("FNAM");
inline constexpr auto k_alfi = Signature::from_string("ALFI");
inline constexpr auto k_almi = Signature::from_string("ALMI");
inline constexpr auto k_alfr = Signature::from_string("ALFR");
inline constexpr auto k_alua = Signature::from_string("ALUA");
inline constexpr auto k_alfa = Signature::from_string("ALFA");
inline constexpr auto k_alrt = Signature::from_string("ALRT");
inline constexpr auto k_alco = Signature::from_string("ALCO");
inline constexpr auto k_alca = Signature::from_string("ALCA");
inline constexpr auto k_alcl = Signature::from_string("ALCL");
inline constexpr auto k_alna = Signature::from_string("ALNA");
inline constexpr auto k_alnt = Signature::from_string("ALNT");
inline constexpr auto k_alfe = Signature::from_string("ALFE");
inline constexpr auto k_alfd = Signature::from_string("ALFD");
inline constexpr auto k_alcc = Signature::from_string("ALCC");
inline constexpr auto k_alfl = Signature::from_string("ALFL");
inline constexpr auto k_knam = Signature::from_string("KNAM");
inline constexpr auto k_aleq = Signature::from_string("ALEQ");
inline constexpr auto k_alea = Signature::from_string("ALEA");
inline constexpr auto k_aled = Signature::from_string("ALED");
inline constexpr auto k_ksiz = Signature::from_string("KSIZ");
inline constexpr auto k_kwda = Signature::from_string("KWDA");
inline constexpr auto k_coct = Signature::from_string("COCT");
inline constexpr auto k_cnto = Signature::from_string("CNTO");
inline constexpr auto k_alla = Signature::from_string("ALLA");
inline constexpr auto k_spor = Signature::from_string("SPOR");
inline constexpr auto k_ocor = Signature::from_string("OCOR");
inline constexpr auto k_gwor = Signature::from_string("GWOR");
inline constexpr auto k_ecor = Signature::from_string("ECOR");
inline constexpr auto k_aldn = Signature::from_string("ALDN");
inline constexpr auto k_alfv = Signature::from_string("ALFV");
inline constexpr auto k_aldi = Signature::from_string("ALDI");
inline constexpr auto k_alsp = Signature::from_string("ALSP");
inline constexpr auto k_alfc = Signature::from_string("ALFC");
inline constexpr auto k_alpc = Signature::from_string("ALPC");
inline constexpr auto k_vtck = Signature::from_string("VTCK");
inline constexpr auto k_coed = Signature::from_string("COED");

inline std::uint16_t read_u16(const std::vector<std::byte>& data, std::size_t offset = 0) {
  std::uint16_t value = 0;
  std::memcpy(&value, data.data() + offset, sizeof(value));
  return value;
}

inline std::int16_t read_i16(const std::vector<std::byte>& data, std::size_t offset = 0) {
  std::int16_t value = 0;
  std::memcpy(&value, data.data() + offset, sizeof(value));
  return value;
}

inline std::uint32_t read_u32(const std::vector<std::byte>& data) {
  std::uint32_t value = 0;
  std::memcpy(&value, data.data(), sizeof(value));
  return value;
}

inline std::int32_t read_i32(const std::vector<std::byte>& data, std::size_t offset = 0) {
  std::int32_t value = 0;
  std::memcpy(&value, data.data() + offset, sizeof(value));
  return value;
}

inline float read_f32(const std::vector<std::byte>& data, std::size_t offset = 0) {
  float value = 0.0f;
  std::memcpy(&value, data.data() + offset, sizeof(value));
  return value;
}

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
  std::vector<std::byte> data;
  data.reserve(value.size() + 1);
  for (const char ch : value) {
    data.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  }
  data.push_back(std::byte{0});
  return data;
}

inline Subrecord make_subrecord(Signature signature, std::vector<std::byte> data = {}) {
  return Subrecord{signature, std::move(data), false};
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
  return signature == k_alst || signature == k_alls || signature == k_alcs;
}

inline QuestAliasKind alias_kind(Signature signature) {
  if (signature == k_alst) return QuestAliasKind::reference;
  if (signature == k_alls) return QuestAliasKind::location;
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
  if (alias.subrecords.empty() || alias.subrecords.back().signature != k_aled) {
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
