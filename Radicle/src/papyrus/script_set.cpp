#include "fo4/papyrus/script_set.hpp"

#include "fo4/io/path_text.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <stdexcept>
#include <utility>

namespace fo4::papyrus {
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

std::optional<std::size_t> loose_root_index(std::span<const std::filesystem::path> roots,
                                            const std::filesystem::path& path) {
  for (std::size_t i = 0; i < roots.size(); ++i) {
    if (roots[i].empty()) continue;
    const auto relative = path.lexically_relative(roots[i]);
    if (relative.empty() || *relative.begin() == "..") continue;
    return i;
  }
  return std::nullopt;
}

int kind_rank(archive::AssetOrigin origin) {
  return origin == archive::AssetOrigin::loose ? 1 : 0;
}

bool outranks(const ScriptCandidate& left, const ScriptCandidate& right) {
  const auto left_kind = kind_rank(left.location.origin);
  const auto right_kind = kind_rank(right.location.origin);
  if (left_kind != right_kind) return left_kind > right_kind;
  if (left.load_index != right.load_index) return left.load_index > right.load_index;
  return left.container > right.container;
}

}  // namespace

std::string normalize_script_name(std::string_view relative_path) {
  std::string name = lower(relative_path);
  std::replace(name.begin(), name.end(), '\\', '/');
  while (!name.empty() && name.front() == '/') name.erase(name.begin());
  if (ends_with(name, ".pex")) name.resize(name.size() - 4);
  return name;
}

std::optional<std::string> script_name_from_entry(std::string_view entry_path) {
  std::string normalized = lower(entry_path);
  std::replace(normalized.begin(), normalized.end(), '\\', '/');
  while (!normalized.empty() && normalized.front() == '/') normalized.erase(normalized.begin());
  constexpr std::string_view k_scripts = "scripts/";
  if (!normalized.starts_with(k_scripts)) return std::nullopt;
  const std::string relative = normalized.substr(k_scripts.size());
  if (!ends_with(relative, ".pex") || !is_safe_script_name(relative)) return std::nullopt;
  return normalize_script_name(relative);
}

std::vector<ScriptCandidate> enumerate_script_candidates(const archive::AssetResolver& resolver,
                                                          std::span<const std::int32_t> root_load_index) {
  if (!root_load_index.empty() && root_load_index.size() != resolver.roots().size()) {
    throw std::invalid_argument("a script set needs one load index per resolver root");
  }
  std::vector<ScriptCandidate> candidates;
  for (const auto& listing : resolver.list("scripts", ".pex", true)) {
    auto name = script_name_from_entry(listing.path);
    if (!name) continue;
    ScriptCandidate candidate;
    candidate.name = std::move(*name);
    candidate.path = listing.path;
    candidate.location = listing.location;
    candidate.size = listing.size;
    if (listing.location.origin == archive::AssetOrigin::loose) {
      const auto root = loose_root_index(resolver.roots(), listing.location.loose_path);
      if (!root) continue;  // invariant: list only walks the resolver's own roots
      candidate.container = io::utf8(resolver.roots()[*root]);
      candidate.load_index = root_load_index.empty()
                                 ? static_cast<std::int32_t>(resolver.roots().size() - 1 - *root)
                                 : root_load_index[*root];
    } else {
      candidate.container = io::utf8(resolver.archives().at(listing.location.archive).path());
      candidate.load_index = static_cast<std::int32_t>(listing.location.archive);
    }
    candidates.push_back(std::move(candidate));
  }
  return candidates;
}

std::vector<ScriptWinner> resolve_script_set(std::span<const ScriptCandidate> candidates) {
  std::map<std::string, ScriptWinner> by_name;
  for (const auto& candidate : candidates) {
    auto [it, inserted] = by_name.try_emplace(candidate.name);
    if (inserted) {
      it->second.name = candidate.name;
      it->second.winner = candidate;
      continue;
    }
    it->second.replaced += 1;
    if (outranks(candidate, it->second.winner)) it->second.winner = candidate;
  }
  std::vector<ScriptWinner> winners;
  winners.reserve(by_name.size());
  for (auto& entry : by_name) winners.push_back(std::move(entry.second));
  return winners;
}

std::vector<ScriptWinner> resolve_script_set(const archive::AssetResolver& resolver,
                                              std::span<const std::int32_t> root_load_index) {
  const auto candidates = enumerate_script_candidates(resolver, root_load_index);
  return resolve_script_set(candidates);
}

}  // namespace fo4::papyrus
