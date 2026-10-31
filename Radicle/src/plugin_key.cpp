#include "fo4/plugin_key.hpp"

#include "fo4/text/encoding.hpp"

#include <cctype>
#include <functional>
#include <mutex>
#include <set>
#include <shared_mutex>
#include <string>

namespace fo4 {
namespace {

char ascii_lower(char ch) noexcept {
  return static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
}

std::string_view filename_only(std::string_view path) {
  const auto slash = path.find_last_of("/\\");
  if (slash == std::string_view::npos) {
    return path;
  }
  return path.substr(slash + 1);
}

const std::string* intern(std::string_view name) {
  static auto* mutex = new std::shared_mutex;
  static auto* names = new std::set<std::string, std::less<>>;
  {
    std::shared_lock lock(*mutex);
    if (const auto it = names->find(name); it != names->end()) {
      return &*it;
    }
  }
  std::unique_lock lock(*mutex);
  return &*names->emplace(name).first;
}

bool has_extension(std::string_view filename, std::string_view extension) noexcept {
  if (filename.size() < extension.size()) {
    return false;
  }
  const auto ext = filename.substr(filename.size() - extension.size());
  for (std::size_t i = 0; i < extension.size(); ++i) {
    if (ascii_lower(ext[i]) != extension[i]) {
      return false;
    }
  }
  return true;
}

}  // namespace

PluginKey PluginKey::from_filename(std::string_view filename) {
  const auto name = filename_only(filename);
  if (name.empty()) {
    return PluginKey{};
  }
  return PluginKey{intern(name)};
}

bool operator==(const PluginKey& a, const PluginKey& b) noexcept {
  return a.name_ == b.name_ || text::equals_folded(a.filename(), b.filename());
}

bool operator<(const PluginKey& a, const PluginKey& b) noexcept {
  return a.name_ != b.name_ && text::compare_folded(a.filename(), b.filename()) < 0;
}

bool PluginKey::looks_light() const noexcept {
  return has_extension(filename(), ".esl");
}

bool PluginKey::looks_master() const noexcept {
  return has_extension(filename(), ".esm") || looks_light();
}

std::size_t hash_value(const PluginKey& key) noexcept {
  return text::hash_folded(key.filename());
}

}  // namespace fo4
