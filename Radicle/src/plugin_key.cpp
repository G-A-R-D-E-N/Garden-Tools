#include "fo4/plugin_key.hpp"

#include <cctype>
#include <string>

namespace fo4 {
namespace {

char ascii_lower(char ch) noexcept {
  return static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
}

int compare_insensitive(std::string_view a, std::string_view b) noexcept {
  const auto n = a.size() < b.size() ? a.size() : b.size();
  for (std::size_t i = 0; i < n; ++i) {
    const auto ca = ascii_lower(a[i]);
    const auto cb = ascii_lower(b[i]);
    if (ca < cb) return -1;
    if (ca > cb) return 1;
  }
  if (a.size() < b.size()) return -1;
  if (a.size() > b.size()) return 1;
  return 0;
}

std::string filename_only(std::string_view path) {
  const auto slash = path.find_last_of("/\\");
  if (slash == std::string_view::npos) {
    return std::string{path};
  }
  return std::string{path.substr(slash + 1)};
}

}  // namespace

PluginKey PluginKey::from_filename(std::string_view filename) {
  return PluginKey{filename_only(filename)};
}

bool operator==(const PluginKey& a, const PluginKey& b) noexcept {
  return compare_insensitive(a.filename_, b.filename_) == 0;
}

bool operator<(const PluginKey& a, const PluginKey& b) noexcept {
  return compare_insensitive(a.filename_, b.filename_) < 0;
}

bool PluginKey::looks_light() const noexcept {
  if (filename_.size() < 4) {
    return false;
  }
  const auto ext = filename_.substr(filename_.size() - 4);
  return ascii_lower(ext[0]) == '.' && ascii_lower(ext[1]) == 'e' &&
         ascii_lower(ext[2]) == 's' && ascii_lower(ext[3]) == 'l';
}

std::size_t hash_value(const PluginKey& key) noexcept {
  std::size_t h = 14695981039346656037ull;
  for (const char ch : key.filename()) {
    h ^= static_cast<unsigned char>(ascii_lower(ch));
    h *= 1099511628211ull;
  }
  return h;
}

}  // namespace fo4
