#include <fo4/pex_analysis/script_set.hpp>

#include <fo4/io/path_text.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>
#include <stdexcept>
#include <utility>

namespace fo4::pex_analysis {

namespace {

std::string lower(std::string_view text) {
  std::string out(text);
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return out;
}

bool ends_with(const std::string& text, std::string_view suffix) {
  return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Requires the name to be usable as a cache file path, as the C# SafeName rule demands.
bool is_safe_script_name(std::string_view name) {
  if (name.empty() || name.front() == '/' || name.find(':') != std::string_view::npos) return false;
  for (const char ch : name) {
    if (ch == '\0' || ch == '<' || ch == '>' || ch == '"' || ch == '|' || ch == '?' || ch == '*') return false;
  }
  std::size_t start = 0;
  while (start <= name.size()) {
    const auto end = std::min(name.find('/', start), name.size());
    const auto part = name.substr(start, end - start);
    if (part.empty() || part == "." || part == ".." || part.back() == '.' || part.back() == ' ') return false;
    start = end + 1;
  }
  return true;
}

bool outranks(const ScriptCandidate& candidate, const ScriptCandidate& other) {
  if (candidate.kind != other.kind) return candidate.kind > other.kind;
  if (candidate.load_index != other.load_index) return candidate.load_index > other.load_index;
  return candidate.container > other.container;
}

std::size_t loose_root_index(const fo4::archive::AssetResolver& resolver, const std::filesystem::path& file) {
  const auto& roots = resolver.roots();
  for (std::size_t i = 0; i < roots.size(); ++i) {
    const auto relative = file.lexically_relative(roots[i]);
    if (!relative.empty() && *relative.begin() != std::filesystem::path("..")) return i;
  }
  throw std::logic_error("loose script is outside every resolver root");
}

}  // namespace

std::string normalize_script_name(std::string_view data_relative_path) {
  constexpr std::string_view scripts_prefix = "scripts/";
  constexpr std::string_view pex_suffix = ".pex";
  auto name = fo4::archive::normalize_asset_path(data_relative_path);
  if (name.starts_with(scripts_prefix)) name.erase(0, scripts_prefix.size());
  if (name.ends_with(pex_suffix)) name.resize(name.size() - pex_suffix.size());
  return name;
}

std::optional<std::string> script_name_from_entry(std::string_view entry_path) {
  constexpr std::string_view scripts_prefix = "scripts/";
  constexpr std::string_view pex_suffix = ".pex";
  std::string normalized = lower(entry_path);
  std::replace(normalized.begin(), normalized.end(), '\\', '/');
  const auto first = normalized.find_first_not_of('/');
  normalized.erase(0, first == std::string::npos ? normalized.size() : first);
  if (!normalized.starts_with(scripts_prefix)) return std::nullopt;
  std::string relative = normalized.substr(scripts_prefix.size());
  if (!ends_with(relative, pex_suffix)) return std::nullopt;
  relative.resize(relative.size() - pex_suffix.size());
  if (!is_safe_script_name(relative)) return std::nullopt;
  return relative;
}

std::vector<ScriptCandidate> enumerate_script_candidates(const fo4::archive::AssetResolver& resolver,
                                                         std::span<const std::int32_t> root_load_index) {
  if (!root_load_index.empty() && root_load_index.size() != resolver.roots().size()) {
    throw std::invalid_argument("a script set requires one load index per resolver root");
  }
  std::vector<ScriptCandidate> candidates;
  for (const auto& listing : resolver.list("Scripts", ".pex", true)) {
    auto name = script_name_from_entry(listing.path);
    if (!name) continue;
    ScriptCandidate candidate;
    candidate.name = std::move(*name);
    candidate.path = listing.path;
    candidate.location = listing.location;
    candidate.kind = listing.location.origin == fo4::archive::AssetOrigin::loose ? ScriptSourceKind::loose
                                                                                : ScriptSourceKind::archive;
    candidate.size = listing.size;
    if (candidate.kind == ScriptSourceKind::loose) {
      const auto root = loose_root_index(resolver, listing.location.loose_path);
      candidate.container = fo4::io::utf8(listing.location.loose_path);
      candidate.load_index = root_load_index.empty()
                                 ? static_cast<std::int32_t>(resolver.roots().size() - 1 - root)
                                 : root_load_index[root];
    } else {
      candidate.container = fo4::io::utf8(resolver.archives()[listing.location.archive].path());
      candidate.load_index = static_cast<std::int32_t>(listing.location.archive);
    }
    candidates.push_back(std::move(candidate));
  }
  return candidates;
}

std::vector<ScriptWinner> resolve_script_set(std::span<const ScriptCandidate> candidates) {
  std::map<std::string, std::vector<std::size_t>> groups;
  for (std::size_t i = 0; i < candidates.size(); ++i) groups[candidates[i].name].push_back(i);
  std::vector<ScriptWinner> winners;
  winners.reserve(groups.size());
  for (const auto& group : groups) {
    std::size_t best = group.second.front();
    for (const auto index : group.second) {
      if (outranks(candidates[index], candidates[best])) best = index;
    }
    winners.push_back({candidates[best], best, group.second.size() - 1});
  }
  return winners;
}

std::vector<ScriptWinner> resolve_script_set(const fo4::archive::AssetResolver& resolver,
                                             std::span<const std::int32_t> root_load_index) {
  const auto candidates = enumerate_script_candidates(resolver, root_load_index);
  return resolve_script_set(std::span<const ScriptCandidate>(candidates));
}

}  // namespace fo4::pex_analysis
