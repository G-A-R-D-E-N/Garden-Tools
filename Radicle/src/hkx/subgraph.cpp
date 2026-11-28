#include "fo4/hkx/subgraph.hpp"

#include "fo4/archive/assets.hpp"
#include "fo4/hash.hpp"
#include "fo4/signature.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace fo4::hkx {
namespace {

std::vector<std::string_view> manifest_lines(std::string_view text) {
  std::vector<std::string_view> lines;
  std::size_t begin = 0;
  while (begin <= text.size()) {
    const std::size_t found = text.find('\n', begin);
    const std::size_t stop = found == std::string_view::npos ? text.size() : found;
    std::string_view line = text.substr(begin, stop - begin);
    while (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    if (!line.empty()) lines.push_back(line);
    if (found == std::string_view::npos) break;
    begin = found + 1;
  }
  return lines;
}

std::string_view trimmed(std::string_view text) {
  std::size_t begin = 0;
  while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin])) != 0) ++begin;
  std::size_t end = text.size();
  while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) --end;
  return text.substr(begin, end - begin);
}

bool names_a_behavior(std::string_view path) {
  return subgraph_normalize(path).find("\\behaviors\\") != std::string::npos;
}

std::string zstring(std::span<const std::byte> data) {
  std::size_t length = 0;
  while (length < data.size() && data[length] != std::byte{0}) ++length;
  return std::string(reinterpret_cast<const char*>(data.data()), length);
}

void drop_empty(std::vector<std::string>& values) {
  values.erase(std::remove_if(values.begin(), values.end(),
                              [](const std::string& value) { return value.empty(); }),
               values.end());
}

}  // namespace

std::string subgraph_normalize(std::string_view path) {
  std::string out;
  out.reserve(path.size());
  for (const char ch : path) {
    if (ch == '/') {
      out.push_back('\\');
    } else {
      out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
  }
  return out;
}

std::uint32_t subgraph_raw_crc32(std::string_view text) {
  return crc32_raw(std::as_bytes(std::span{text.data(), text.size()}));
}

std::uint32_t subgraph_behavior_half(std::string_view behavior_path) {
  return subgraph_raw_crc32(subgraph_normalize(behavior_path));
}

std::uint64_t subgraph_id(std::string_view behavior_path, std::span<const std::string> prefixes) {
  std::string joined;
  for (std::size_t index = 0; index < prefixes.size(); ++index) {
    if (index != 0) joined.push_back('|');
    joined.append(prefixes[index]);
  }
  const std::uint64_t high = subgraph_raw_crc32(subgraph_normalize(joined));
  const std::uint64_t low = subgraph_behavior_half(behavior_path);
  return (high << 32) | low;
}

std::optional<AnimationFileData> parse_animation_file_data(std::string_view text) {
  const auto lines = manifest_lines(text);
  if (lines.size() < 5) return std::nullopt;

  AnimationFileData parsed;
  const std::string_view id_text = trimmed(lines[2]);
  const char* const begin = id_text.data();
  const char* const end = begin + id_text.size();
  const auto [stop, error] = std::from_chars(begin, end, parsed.id);
  if (error != std::errc{} || stop != end) return std::nullopt;

  for (std::size_t index = 4; index < lines.size(); ++index) {
    std::string path(lines[index]);
    if (names_a_behavior(path)) {
      parsed.behavior_paths.push_back(std::move(path));
    } else {
      parsed.animation_paths.push_back(std::move(path));
    }
  }
  if (parsed.behavior_paths.empty() && !parsed.animation_paths.empty()) {
    parsed.behavior_paths.push_back(std::move(parsed.animation_paths.front()));
    parsed.animation_paths.erase(parsed.animation_paths.begin());
  }
  if (parsed.behavior_paths.empty()) return std::nullopt;
  return parsed;
}

std::optional<std::string> parse_animation_offset_hint(std::span<const std::byte> bytes) {
  if (bytes.size() < 8) return std::nullopt;
  if (bytes[0] != std::byte{'V'} || bytes[1] != std::byte{'4'}) return std::nullopt;

  std::size_t at = 2;
  while (at < bytes.size() && bytes[at] != std::byte{'\n'}) ++at;
  if (at + 1 >= bytes.size()) return std::nullopt;
  ++at;
  const std::size_t length = std::to_integer<std::size_t>(bytes[at]);
  ++at;
  if (length == 0 || at + length > bytes.size()) return std::nullopt;

  std::string path(reinterpret_cast<const char*>(bytes.data() + at), length);
  while (!path.empty() && path.back() == '\0') path.pop_back();
  if (path.empty()) return std::nullopt;
  return path;
}

RaceAnimationSet race_animation_set(const Record& race) {
  RaceAnimationSet set;
  if (race.header.signature.view() != "RACE") return set;

  for (const auto& subrecord : race.subrecords) {
    const auto signature = subrecord.signature.view();
    if (signature == "SGNM") {
      set.behavior_graphs.push_back(zstring(subrecord.data));
    } else if (signature == "SAPT") {
      set.path_prefixes.push_back(zstring(subrecord.data));
    }
  }
  drop_empty(set.behavior_graphs);
  drop_empty(set.path_prefixes);

  if (set.behavior_graphs.size() != 1) return set;

  RaceSubgraph subgraph;
  subgraph.behavior_graph = set.behavior_graphs.front();
  subgraph.path_prefixes = set.path_prefixes;
  subgraph.id = subgraph_id(subgraph.behavior_graph, subgraph.path_prefixes);
  set.subgraphs.push_back(std::move(subgraph));
  return set;
}

std::string subgraph_manifest_path(std::uint64_t id) {
  return "meshes\\animtextdata\\animationfiledata\\" + std::to_string(id) + ".txt";
}

std::optional<SubgraphManifestText> read_subgraph_manifest(const archive::AssetResolver& assets, std::uint64_t id) {
  const auto text_of = [&](const archive::AssetLocation& location) {
    const auto bytes = assets.read(location);
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  };
  const std::string path = subgraph_manifest_path(id);
  try {
    if (const auto loose = assets.locate_loose(path)) {
      return SubgraphManifestText{text_of({archive::AssetOrigin::loose, *loose, 0, 0}), "loose"};
    }
  } catch (const std::exception&) {
  }
  try {
    if (const auto found = assets.locate_in_archives(path)) {
      return SubgraphManifestText{text_of(*found), assets.archives()[found->archive].path().filename().string()};
    }
  } catch (const std::exception&) {
  }
  return std::nullopt;
}

bool animation_exists(const archive::AssetResolver& assets, std::string_view path) {
  const std::string base = "meshes\\" + std::string(path);
  try {
    for (const std::string& candidate : {base, base + ".hkx"}) {
      if (assets.locate(candidate)) return true;
    }
  } catch (const std::exception&) {
  }
  return false;
}

}  // namespace fo4::hkx
