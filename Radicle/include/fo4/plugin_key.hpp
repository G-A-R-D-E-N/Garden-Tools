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

  std::string_view filename() const noexcept { return filename_; }
  bool empty() const noexcept { return filename_.empty(); }
  bool looks_light() const noexcept;

  friend bool operator==(const PluginKey& a, const PluginKey& b) noexcept;
  friend bool operator<(const PluginKey& a, const PluginKey& b) noexcept;

private:
  explicit PluginKey(std::string filename) : filename_(std::move(filename)) {}

  std::string filename_;
};

std::size_t hash_value(const PluginKey& key) noexcept;

}  // namespace fo4

template <>
struct std::hash<fo4::PluginKey> {
  std::size_t operator()(const fo4::PluginKey& key) const noexcept {
    return fo4::hash_value(key);
  }
};
