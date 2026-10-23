#include <fo4/pex_analysis/script_set.hpp>

#include <fo4/io/path_text.hpp>

#include <filesystem>
#include <map>
#include <stdexcept>

namespace fo4::pex_analysis {

namespace {

bool outranks(const ScriptCandidate& candidate, const ScriptCandidate& other) {
  if (candidate.kind != other.kind) return candidate.kind > other.kind;
  if (candidate.load_index != other.load_index) return candidate.load_index > other.load_index;
  return candidate.container > other.container;
}

std::int32_t loose_load_index(const fo4::archive::AssetResolver& resolver, const std::filesystem::path& file) {
  const auto& roots = resolver.roots();
  for (std::size_t i = 0; i < roots.size(); ++i) {
    const auto relative = file.lexically_relative(roots[i]);
    if (!relative.empty() && *relative.begin() != std::filesystem::path("..")) return static_cast<std::int32_t>(roots.size() - 1 - i);
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

std::vector<ScriptCandidate> enumerate_script_candidates(const fo4::archive::AssetResolver& resolver) {
  std::vector<ScriptCandidate> candidates;
  for (const auto& listing : resolver.list("Scripts", ".pex", true)) {
    const auto name = normalize_script_name(listing.path);
    if (name.empty()) continue;
    if (listing.location.origin == fo4::archive::AssetOrigin::archive) {
      const auto archive = listing.location.archive;
      candidates.push_back({name, ScriptSourceKind::archive, fo4::io::utf8(resolver.archives()[archive].path()),
                            static_cast<std::int32_t>(archive), listing.size});
    } else {
      candidates.push_back({name, ScriptSourceKind::loose, fo4::io::utf8(listing.location.loose_path),
                            loose_load_index(resolver, listing.location.loose_path), listing.size});
    }
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

}  // namespace fo4::pex_analysis
