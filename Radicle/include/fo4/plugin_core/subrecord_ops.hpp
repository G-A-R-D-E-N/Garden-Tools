#pragma once

#include "fo4/plugin_core/container.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4 {

inline std::vector<std::byte> encode_zstring(std::string_view text) {
  const auto bytes = std::as_bytes(std::span{text});
  std::vector<std::byte> data;
  data.reserve(bytes.size() + 1);
  data.assign(bytes.begin(), bytes.end());
  data.push_back(std::byte{0});
  return data;
}

inline std::optional<std::string_view> zstring_view(std::span<const std::byte> data) noexcept {
  if (data.empty() || data.back() != std::byte{0}) return std::nullopt;
  const std::string_view text{reinterpret_cast<const char*>(data.data()), data.size() - 1};
  if (text.find('\0') != std::string_view::npos) return std::nullopt;
  return text;
}

inline std::optional<std::string_view> zstring_view(const Subrecord& subrecord) noexcept {
  return zstring_view(std::span<const std::byte>{subrecord.data});
}

inline std::string zstring_lossy(std::span<const std::byte> data) {
  const auto end = std::find(data.begin(), data.end(), std::byte{0});
  return std::string{reinterpret_cast<const char*>(data.data()), static_cast<std::size_t>(end - data.begin())};
}

inline const Subrecord* find_subrecord(const Record& record, Signature signature) {
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature == signature) return &subrecord;
  }
  return nullptr;
}

inline Subrecord* find_subrecord(Record& record, Signature signature) {
  for (auto& subrecord : record.subrecords) {
    if (subrecord.signature == signature) return &subrecord;
  }
  return nullptr;
}

template <class OnDuplicate>
Subrecord* unique_subrecord(Record& record, Signature signature, OnDuplicate&& on_duplicate) {
  Subrecord* found = nullptr;
  for (auto& subrecord : record.subrecords) {
    if (subrecord.signature != signature) continue;
    if (found != nullptr) on_duplicate();
    found = &subrecord;
  }
  return found;
}

inline Subrecord make_subrecord(Signature signature, std::vector<std::byte> data = {}) {
  return Subrecord{signature, std::move(data), false};
}

inline Subrecord make_subrecord(Signature signature, std::size_t zero_filled_size) {
  return Subrecord{signature, std::vector<std::byte>(zero_filled_size, std::byte{0}), false};
}

inline std::size_t erase_subrecords(Record& record, Signature signature) {
  return std::erase_if(static_cast<std::vector<Subrecord>&>(record.subrecords),
                       [&](const Subrecord& sub) { return sub.signature == signature; });
}

inline std::optional<std::string> editor_id_of(const Record& record) {
  const auto* edid = find_subrecord(record, sig_edid);
  if (edid == nullptr) return std::nullopt;
  return zstring_lossy(edid->data);
}

}
