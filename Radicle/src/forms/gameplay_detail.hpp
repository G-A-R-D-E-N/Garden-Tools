#pragma once

#include "fo4/environment/environment.hpp"
#include "fo4/forms/gameplay_rules.hpp"
#include "fo4/formid.hpp"
#include "fo4/plugin/container.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <type_traits>

namespace fo4::forms::gameplay_detail {

inline const Subrecord* first_subrecord(const Record& record, Signature signature) {
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature == signature) return &subrecord;
  }
  return nullptr;
}

template <typename T>
T read_at(const Subrecord& subrecord, std::size_t offset) {
  static_assert(std::is_trivially_copyable_v<T>);
  if (offset + sizeof(T) > subrecord.data.size()) {
    throw GameplayError(subrecord.signature.to_string() + " is shorter than " +
                        std::to_string(offset + sizeof(T)) + " bytes");
  }
  T value{};
  std::memcpy(&value, subrecord.data.data() + offset, sizeof(T));
  return value;
}

inline std::string zstring(const Subrecord& subrecord) {
  std::string text;
  for (const auto byte : subrecord.data) {
    const auto ch = static_cast<char>(std::to_integer<std::uint8_t>(byte));
    if (ch == '\0') break;
    text.push_back(ch);
  }
  return text;
}

inline std::optional<FormKey> link_at(const env::Environment& environment,
                                      const env::RecordRef& ref,
                                      const Subrecord& subrecord,
                                      std::size_t offset) {
  const auto key = environment.resolve_link(ref, RawFormId{read_at<std::uint32_t>(subrecord, offset)});
  if (key.owner().empty()) return std::nullopt;
  return key;
}

inline std::optional<FormKey> link_of(const env::Environment& environment,
                                      const env::RecordRef& ref,
                                      Signature signature) {
  const auto* subrecord = first_subrecord(*ref.record, signature);
  if (subrecord == nullptr) return std::nullopt;
  if (subrecord->data.size() != 4) {
    throw GameplayError(ref.record->form_key.to_string() + " " + signature.to_string() +
                        " must be 4 bytes");
  }
  return link_at(environment, ref, *subrecord, 0);
}

inline const Subrecord& acbs_of(const Record& record) {
  const auto* acbs = first_subrecord(record, Signature::from_string("ACBS"));
  if (acbs == nullptr || acbs->data.size() < 16) {
    throw GameplayError(record.form_key.to_string() + " ACBS must be at least 16 bytes");
  }
  return *acbs;
}

inline std::optional<std::string> editor_id_of(const Record& record) {
  const auto* edid = first_subrecord(record, Signature::from_string("EDID"));
  if (edid == nullptr) return std::nullopt;
  return zstring(*edid);
}

}  // namespace fo4::forms::gameplay_detail
