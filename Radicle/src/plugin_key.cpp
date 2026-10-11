#include "fo4/plugin_key.hpp"

#include "fo4/text/encoding.hpp"

#include <cctype>
#include <string>

namespace fo4 {
namespace {

char ascii_lower(char ch) noexcept {
  return static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
}

std::string filename_only(std::string_view path) {
  const auto slash = path.find_last_of("/\\");
  if (slash == std::string_view::npos) {
    return std::string{path};
  }
  return std::string{path.substr(slash + 1)};
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
  return PluginKey{filename_only(filename)};
}

bool operator==(const PluginKey& a, const PluginKey& b) noexcept {
  return text::equals_folded(a.filename_, b.filename_);
}

bool operator<(const PluginKey& a, const PluginKey& b) noexcept {
  return text::compare_folded(a.filename_, b.filename_) < 0;
}

bool PluginKey::looks_light() const noexcept {
  return has_extension(filename_, ".esl");
}

bool PluginKey::looks_master() const noexcept {
  return has_extension(filename_, ".esm") || looks_light();
}

std::size_t hash_value(const PluginKey& key) noexcept {
  return text::hash_folded(key.filename());
}

}  // namespace fo4
