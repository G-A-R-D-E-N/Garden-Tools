#include "fo4/loadorder/load_order_files.hpp"

#include "fo4/io/path_text.hpp"
#include "fo4/plugin_key.hpp"
#include "fo4/text/encoding.hpp"

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace fo4::loadorder {
namespace {

// RE-020: fgets with a 512-byte buffer returns at most 511 bytes per call
constexpr std::size_t k_chunk_limit = 511;

struct Chunk {
  std::string_view text;
  std::size_t line = 0;
};

bool has_control_character(std::string_view name) {
  return std::any_of(name.begin(), name.end(), [](char ch) { return static_cast<unsigned char>(ch) < 0x20; });
}

std::string text_mode(std::string_view text) {
  text = text.substr(0, text.find('\x1A'));
  std::string out;
  out.reserve(text.size());
  for (std::size_t at = 0; at < text.size(); ++at) {
    if (text[at] == '\r' && at + 1 < text.size() && text[at + 1] == '\n') continue;
    out.push_back(text[at]);
  }
  return out;
}

std::vector<Chunk> split_chunks(std::string_view text) {
  std::vector<Chunk> chunks;
  std::size_t line = 1;
  while (!text.empty()) {
    const auto window = text.substr(0, k_chunk_limit);
    const auto lf = window.find('\n');
    const auto length = lf == std::string_view::npos ? window.size() : lf + 1;
    chunks.push_back({text.substr(0, length), line});
    if (lf != std::string_view::npos) ++line;
    text.remove_prefix(length);
  }
  return chunks;
}

std::string read_text(const std::filesystem::path& path) {
  std::ifstream in{path, std::ios::binary};
  if (!in) throw std::runtime_error("unable to open " + io::utf8(path));
  return std::string{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

}  // namespace

PluginList parse_plugin_list(std::string_view text) {
  PluginList list;
  std::map<PluginKey, std::size_t> seen;
  const auto translated = text_mode(text);
  for (const auto& chunk : split_chunks(translated)) {
    if (chunk.text.size() <= 1 || chunk.text.front() == '#') continue;
    const bool active = chunk.text.front() == '*';
    auto name = active ? chunk.text.substr(1) : chunk.text;
    if (name.ends_with('\n')) name.remove_suffix(1);
    if (has_control_character(name)) {
      list.invalid_lines.push_back(chunk.line);
      continue;
    }
    PluginListEntry entry{text::windows1252_to_utf8(name), active, chunk.line};
    const auto key = PluginKey::from_filename(entry.name);
    const auto found = seen.find(key);
    if (found != seen.end()) {
      if (active) list.entries[found->second].active = true;
      list.duplicates.push_back(std::move(entry));
      continue;
    }
    seen.emplace(key, list.entries.size());
    list.entries.push_back(std::move(entry));
  }
  return list;
}

PluginList read_plugin_list(const std::filesystem::path& path) {
  return parse_plugin_list(read_text(path));
}

std::vector<std::string> parse_ccc(std::string_view text) {
  std::vector<std::string> names;
  while (!text.empty()) {
    const auto end = text.find_first_of("\r\n");
    const auto token = text.substr(0, end);
    if (!token.empty() && !has_control_character(token)) names.push_back(text::windows1252_to_utf8(token));
    if (end == std::string_view::npos) break;
    text.remove_prefix(end + 1);
  }
  return names;
}

std::vector<std::string> read_ccc(const std::filesystem::path& path) {
  return parse_ccc(read_text(path));
}

}  // namespace fo4::loadorder
