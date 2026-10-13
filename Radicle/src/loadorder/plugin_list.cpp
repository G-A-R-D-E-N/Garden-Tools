#include "fo4/loadorder/load_order_files.hpp"

#include "fo4/io/path_text.hpp"
#include "fo4/plugin_key.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace fo4::loadorder {
namespace {

constexpr std::string_view k_utf8_bom = "\xEF\xBB\xBF";

bool has_control_character(std::string_view name) {
  return std::any_of(name.begin(), name.end(), [](char ch) { return static_cast<unsigned char>(ch) < 0x20; });
}

std::string_view strip_bom(std::string_view text) {
  if (text.starts_with(k_utf8_bom)) text.remove_prefix(k_utf8_bom.size());
  return text;
}

std::vector<std::string_view> split_lines(std::string_view text) {
  std::vector<std::string_view> lines;
  while (!text.empty()) {
    const auto end = text.find('\n');
    auto line = text.substr(0, end);
    if (line.ends_with('\r')) line.remove_suffix(1);
    lines.push_back(line);
    if (end == std::string_view::npos) break;
    text.remove_prefix(end + 1);
  }
  return lines;
}

std::string read_text(const std::filesystem::path& path) {
  std::ifstream in{path, std::ios::binary};
  if (!in) throw std::runtime_error("unable to open " + io::utf8(path));
  return std::string{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

}  // namespace

PluginList parse_plugin_list(std::string_view text) {
  PluginList list;
  std::set<PluginKey> seen;
  const auto lines = split_lines(strip_bom(text));
  for (std::size_t index = 0; index < lines.size(); ++index) {
    const auto line = lines[index];
    if (line.starts_with('#') || line.size() < 2) continue;
    const bool active = line.starts_with('*');
    const auto name = active ? line.substr(1) : line;
    if (has_control_character(name)) {
      list.invalid_lines.push_back(index + 1);
      continue;
    }
    PluginListEntry entry{std::string{name}, active, index + 1};
    if (!seen.insert(PluginKey::from_filename(entry.name)).second) {
      list.duplicates.push_back(std::move(entry));
      continue;
    }
    list.entries.push_back(std::move(entry));
  }
  return list;
}

PluginList read_plugin_list(const std::filesystem::path& path) {
  return parse_plugin_list(read_text(path));
}

std::vector<std::string> parse_ccc(std::string_view text) {
  std::vector<std::string> names;
  for (const auto line : split_lines(strip_bom(text))) {
    const auto first = line.find_first_not_of(" \t\r");
    if (first == std::string_view::npos) continue;
    const auto last = line.find_last_not_of(" \t\r");
    names.emplace_back(line.substr(first, last - first + 1));
  }
  return names;
}

std::vector<std::string> read_ccc(const std::filesystem::path& path) {
  return parse_ccc(read_text(path));
}

}  // namespace fo4::loadorder
