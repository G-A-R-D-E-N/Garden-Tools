#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

namespace fo4 {

class PluginKey {
public:
  PluginKey() = default;
  static PluginKey from_filename(std::string_view filename);

  std::string_view filename() const noexcept { return name_ ? std::string_view(*name_) : std::string_view(""); }
  bool empty() const noexcept { return name_ == nullptr; }
  bool looks_light() const noexcept;
  bool looks_master() const noexcept;

  friend bool operator==(const PluginKey& a, const PluginKey& b) noexcept;
  friend bool operator<(const PluginKey& a, const PluginKey& b) noexcept;

private:
  explicit PluginKey(const std::string* name) noexcept : name_(name) {}

  const std::string* name_{nullptr};
};

std::size_t hash_value(const PluginKey& key) noexcept;

}  // namespace fo4

template <>
struct std::hash<fo4::PluginKey> {
  std::size_t operator()(const fo4::PluginKey& key) const noexcept {
    return fo4::hash_value(key);
  }
};
