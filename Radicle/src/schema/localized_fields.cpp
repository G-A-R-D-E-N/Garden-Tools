#include "fo4/schema/localized_fields.hpp"

#include "fo4/error.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace fo4::schema {
namespace {

const std::string& empty_string() {
  static const std::string value;
  return value;
}

}  // namespace

const std::string& LocalizedString::inline_text() const noexcept {
  return text.has_value() ? *text : empty_string();
}

bool is_localized_field(Signature record, Signature subrecord) noexcept {
  return std::any_of(std::begin(k_generated_localized_fields),
                     std::end(k_generated_localized_fields),
                     [&](const LocalizedFieldEntry& entry) {
                       return entry.record == record && entry.subrecord == subrecord;
                     });
}

LocalizedString read_localized_string(const Plugin& plugin,
                                      Signature record,
                                      Signature subrecord,
                                      const Subrecord& payload) {
  LocalizedString value;

  if (plugin.header.tes4_flags.is_localized() && is_localized_field(record, subrecord)) {
    if (payload.data.size() != 4) {
      value.raw_id = payload.data;
      return value;
    }
    std::uint32_t id = 0;
    std::memcpy(&id, payload.data.data(), 4);
    value.id = id;
    return value;
  }

  if (payload.data.empty() || payload.data.back() != std::byte{0} ||
      std::find(payload.data.begin(), payload.data.end() - 1, std::byte{0}) !=
          payload.data.end() - 1) {
    throw std::runtime_error(record.to_string() + " " + subrecord.to_string() +
                             " is not a single NUL-terminated string");
  }
  const auto* begin = reinterpret_cast<const char*>(payload.data.data());
  value.text = std::string(begin, begin + payload.data.size() - 1);
  return value;
}

std::vector<std::byte> write_localized_string(const LocalizedString& value) {
  if (value.raw_id.has_value()) return *value.raw_id;
  std::vector<std::byte> bytes;
  if (value.id.has_value()) {
    bytes.resize(4);
    const auto id = *value.id;
    std::memcpy(bytes.data(), &id, 4);
    return bytes;
  }
  const auto& text = value.inline_text();
  bytes.reserve(text.size() + 1);
  for (const char ch : text) {
    bytes.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  }
  bytes.push_back(std::byte{0});
  return bytes;
}

}  // namespace fo4::schema
