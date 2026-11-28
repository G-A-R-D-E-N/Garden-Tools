#include "fo4/hkx/animation_index.hpp"

#include "fo4/io/path_text.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <stdexcept>
#include <set>
#include <utility>

namespace fo4::hkx {
namespace {

using native_string = std::filesystem::path::string_type;

constexpr std::array<std::string_view, 4> k_role_folders{"behaviors", "animations", "characters", "characterassets"};
constexpr std::array<std::string_view, 3> k_extensions{".hkx", ".hkt", ".nif"};
constexpr std::size_t k_candidate_cap = 50;

std::string lower_of(std::string_view text) {
  std::string lowered;
  lowered.reserve(text.size());
  for (const char ch : text) lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
  return lowered;
}

std::string flatten(std::string_view path) {
  std::string flat = lower_of(path);
  std::replace(flat.begin(), flat.end(), '\\', '/');
  flat.erase(0, std::min(flat.find_first_not_of('/'), flat.size()));
  return flat;
}

std::vector<std::string_view> split_parts(std::string_view flat) {
  std::vector<std::string_view> parts;
  std::size_t start = 0;
  while (start < flat.size()) {
    auto end = flat.find('/', start);
    if (end == std::string_view::npos) end = flat.size();
    if (end > start) parts.push_back(flat.substr(start, end - start));
    start = end + 1;
  }
  return parts;
}

std::string join_parts(const std::vector<std::string_view>& parts, std::size_t from, std::size_t to) {
  std::string joined;
  for (std::size_t i = from; i < to; ++i) {
    if (!joined.empty()) joined.push_back('/');
    joined.append(parts[i]);
  }
  return joined;
}

std::optional<std::size_t> role_of(std::string_view part) {
  for (std::size_t i = 0; i < k_role_folders.size(); ++i) {
    if (part == k_role_folders[i]) return i;
  }
  return std::nullopt;
}

AnimationKind kind_of_role(std::size_t role) {
  switch (role) {
    case 0: return AnimationKind::behaviour;
    case 1: return AnimationKind::animation;
    case 2: return AnimationKind::character;
    case 3: return AnimationKind::skeleton;
    default: return AnimationKind::other;
  }
}

std::string stem_of(std::string_view flat) {
  const auto slash = flat.rfind('/');
  const auto name = slash == std::string_view::npos ? flat : flat.substr(slash + 1);
  const auto dot = name.rfind('.');
  return std::string(dot == std::string_view::npos ? name : name.substr(0, dot));
}

std::string leaf_label(const std::filesystem::path& root) {
  std::filesystem::path trimmed = root;
  if (!trimmed.has_filename()) trimmed = trimmed.parent_path();
  auto label = io::utf8(trimmed.filename());
  return label.empty() ? io::utf8(root) : label;
}

struct RootPrefix {
  native_string prefix;
  AnimationProvider provider;
};

std::vector<RootPrefix> root_prefixes(const archive::AssetResolver& resolver) {
  std::vector<RootPrefix> prefixes;
  for (const auto& root : resolver.roots()) {
    prefixes.push_back({(root / "").native(), AnimationProvider{leaf_label(root), false, root}});
  }
  return prefixes;
}

AnimationProvider provider_for(const std::vector<RootPrefix>& roots, const archive::AssetResolver& resolver,
                               const archive::AssetLocation& location) {
  if (location.origin == archive::AssetOrigin::archive) {
    const auto& path = resolver.archives().at(location.archive).path();
    return AnimationProvider{io::utf8(path.filename()), true, path};
  }
  const RootPrefix* best = nullptr;
  const auto& native = location.loose_path.native();
  for (const auto& root : roots) {
    if (native.starts_with(root.prefix) && (best == nullptr || root.prefix.size() > best->prefix.size())) best = &root;
  }
  if (best != nullptr) {
    auto provider = best->provider;
    provider.file = location.loose_path;
    return provider;
  }
  return AnimationProvider{"loose", false, location.loose_path.parent_path(), location.loose_path};
}

}  // namespace

std::string_view animation_kind_name(AnimationKind kind) {
  switch (kind) {
    case AnimationKind::animation: return "animation";
    case AnimationKind::behaviour: return "behaviour";
    case AnimationKind::project: return "project";
    case AnimationKind::character: return "character";
    case AnimationKind::skeleton: return "skeleton";
    case AnimationKind::other: return "other";
  }
  return "other";
}

std::optional<AnimationKind> animation_kind_from_name(std::string_view name) {
  for (const auto kind : {AnimationKind::animation, AnimationKind::behaviour, AnimationKind::project,
                          AnimationKind::character, AnimationKind::skeleton, AnimationKind::other}) {
    if (animation_kind_name(kind) == name) return kind;
  }
  return std::nullopt;
}

AnimationIndexEntry classify_animation_path(std::string_view path) {
  AnimationIndexEntry entry;
  entry.path = flatten(path);
  const std::string_view flat = entry.path;
  const auto slash = flat.rfind('/');
  if (slash != std::string_view::npos) entry.folder = std::string(flat.substr(0, slash));

  const auto parts = split_parts(flat);
  std::optional<std::size_t> actors;
  std::optional<std::size_t> role;
  for (std::size_t i = 0; i < parts.size(); ++i) {
    if (!actors && parts[i] == "actors") actors = i;
    if (!role && role_of(parts[i])) role = i;
  }

  if (actors) {
    const std::size_t end =
        role && *role > *actors ? *role : std::max(*actors + 1, parts.size() - 1);
    if (end > *actors + 1) {
      entry.project = join_parts(parts, *actors + 1, end);
      entry.race = std::string(parts[end - 1]);
      entry.actor_root = join_parts(parts, 0, end);
    }
  }

  if (role) {
    entry.kind = kind_of_role(*role_of(parts[*role]));
  } else {
    entry.kind = actors ? AnimationKind::project : AnimationKind::other;
  }
  if (entry.kind == AnimationKind::character || entry.kind == AnimationKind::skeleton) entry.character = stem_of(flat);
  return entry;
}

AnimationProvider animation_provider(const archive::AssetResolver& resolver, const archive::AssetLocation& location) {
  return provider_for(root_prefixes(resolver), resolver, location);
}

std::vector<AnimationProvider> animation_index_sources(const archive::AssetResolver& resolver) {
  std::vector<AnimationProvider> sources;
  for (const auto& root : resolver.roots()) sources.push_back(AnimationProvider{leaf_label(root), false, root});
  const auto& archives = resolver.archives();
  for (std::size_t i = archives.size(); i-- > 0;) {
    const auto& path = archives[i].path();
    sources.push_back(AnimationProvider{io::utf8(path.filename()), true, path});
  }
  return sources;
}

AnimationIndex build_animation_index(const archive::AssetResolver& resolver) {
  AnimationIndex index;
  const auto roots = root_prefixes(resolver);
  std::set<std::size_t> nameless;
  for (const auto extension : k_extensions) {
    const bool model_pass = extension == ".nif";
    const auto listed = resolver.list_report("", extension, !model_pass);
    nameless.insert(listed.nameless_archives.begin(), listed.nameless_archives.end());
    for (const auto& item : listed.entries) {
      if (model_pass) {
        if (item.path.find("actors/") != std::string::npos) {
          index.models.push_back({item.path, provider_for(roots, resolver, item.location)});
        }
        continue;
      }
      auto provider = provider_for(roots, resolver, item.location);
      if (item.shadowed && !index.entries.empty() && index.entries.back().path == item.path) {
        index.entries.back().shadowed.push_back(std::move(provider));
        continue;
      }
      auto entry = classify_animation_path(item.path);
      entry.size = item.size;
      entry.winner = std::move(provider);
      index.entries.push_back(std::move(entry));
    }
  }
  std::stable_sort(index.entries.begin(), index.entries.end(),
                   [](const AnimationIndexEntry& left, const AnimationIndexEntry& right) { return left.path < right.path; });
  for (const std::size_t at : nameless) {
    index.notes.push_back("archive " + io::utf8(resolver.archives().at(at).path().filename()) +
                          " has no name table, so only paths found in other sources are listed for it");
  }
  return index;
}

std::vector<std::size_t> filter_animation_index(const AnimationIndex& index, const AnimationIndexFilter& filter) {
  const auto project = lower_of(filter.project);
  const auto race = lower_of(filter.race);
  const auto character = lower_of(filter.character);
  const auto query = lower_of(filter.query);
  std::vector<std::size_t> matched;
  for (std::size_t i = 0; i < index.entries.size(); ++i) {
    const auto& entry = index.entries[i];
    if (filter.kind && entry.kind != *filter.kind) continue;
    if (!project.empty() && entry.project != project) continue;
    if (!race.empty() && entry.race != race) continue;
    if (!character.empty() && entry.character != character) continue;
    if (!query.empty() && entry.path.find(query) == std::string::npos &&
        lower_of(entry.winner.label).find(query) == std::string::npos) {
      continue;
    }
    matched.push_back(i);
  }
  return matched;
}

AnimationIndexFacets animation_index_facets(const AnimationIndex& index) {
  std::set<std::string> projects;
  std::set<std::string> races;
  std::set<std::string> characters;
  for (const auto& entry : index.entries) {
    if (!entry.project.empty()) projects.insert(entry.project);
    if (!entry.race.empty()) races.insert(entry.race);
    if (!entry.character.empty()) characters.insert(entry.character);
  }
  AnimationIndexFacets facets;
  facets.projects.assign(projects.begin(), projects.end());
  facets.races.assign(races.begin(), races.end());
  facets.characters.assign(characters.begin(), characters.end());
  return facets;
}

AnimationActorAssets resolve_animation_actor(const AnimationIndex& index, std::string_view entry_path) {
  AnimationActorAssets out;
  out.actor_root = classify_animation_path(entry_path).actor_root;
  if (out.actor_root.empty()) {
    out.notes.emplace_back("the path is not under an actors folder, so no skeleton or body can be matched");
    return out;
  }

  const std::string assets = out.actor_root + "/characterassets/";
  std::vector<const AnimationIndexEntry*> pool;
  auto at = std::lower_bound(index.entries.begin(), index.entries.end(), assets,
                             [](const AnimationIndexEntry& entry, const std::string& key) { return entry.path < key; });
  for (; at != index.entries.end() && at->path.starts_with(assets); ++at) pool.push_back(&*at);

  std::vector<const AnimationIndexEntry*> skeletons;
  for (const auto* candidate : pool) {
    if (stem_of(candidate->path).starts_with("skeleton")) skeletons.push_back(candidate);
  }
  if (skeletons.empty()) skeletons = pool;

  const AnimationIndexEntry* chosen = nullptr;
  if (skeletons.size() == 1) {
    chosen = skeletons.front();
  } else if (skeletons.size() > 1) {
    std::vector<const AnimationIndexEntry*> exact;
    for (const auto* candidate : skeletons) {
      if (stem_of(candidate->path) == "skeleton") exact.push_back(candidate);
    }
    if (exact.size() == 1) chosen = exact.front();
  }
  for (std::size_t i = 0; i < skeletons.size() && i < k_candidate_cap; ++i) {
    out.skeleton_candidates.push_back(skeletons[i]->path);
  }

  const auto model_at = [&](const std::string& key) {
    return std::lower_bound(index.models.begin(), index.models.end(), key,
                            [](const AnimationModel& model, const std::string& wanted) { return model.path < wanted; });
  };

  if (chosen != nullptr) {
    out.skeleton = chosen->path;
    out.skeleton_provider = chosen->winner;
    const auto nif = chosen->path.substr(0, chosen->path.rfind('.')) + ".nif";
    const auto found = model_at(nif);
    if (found != index.models.end() && found->path == nif) {
      out.skeleton_nif = nif;
      out.skeleton_nif_provider = found->provider;
    }
  } else if (skeletons.empty()) {
    out.notes.push_back("no skeleton file sits under " + assets);
  } else {
    out.notes.push_back(std::to_string(skeletons.size()) + " skeleton candidates sit under " + assets +
                        " and none is named skeleton alone, so none is chosen");
  }

  const std::string body_prefix = out.actor_root + "/";
  std::vector<const AnimationModel*> models;
  for (auto model = model_at(body_prefix); model != index.models.end() && model->path.starts_with(body_prefix); ++model) {
    if (!stem_of(model->path).starts_with("skeleton")) models.push_back(&*model);
  }
  out.body_candidate_total = models.size();
  for (std::size_t i = 0; i < models.size() && i < k_candidate_cap; ++i) out.body_candidates.push_back(models[i]->path);
  if (models.size() == 1) {
    out.body = models.front()->path;
    out.body_provider = models.front()->provider;
  } else if (models.empty()) {
    out.notes.push_back("no body model sits under " + out.actor_root);
  } else {
    out.notes.push_back(std::to_string(models.size()) + " models sit under " + out.actor_root + ", so none is chosen");
  }
  return out;
}

constexpr std::uint64_t k_max_asset_bytes = 256ULL * 1024 * 1024;

std::optional<AnimationAssetBytes> read_animation_asset(const archive::AssetResolver& resolver, std::string_view path) {
  const auto key = archive::normalize_asset_path(path);
  if (!key.ends_with(".hkx") && !key.ends_with(".hkt") && !key.ends_with(".nif")) {
    throw archive::AssetPathError("only .hkx, .hkt and .nif files can be extracted: " + std::string(path));
  }
  const auto location = resolver.locate(key);
  if (!location) return std::nullopt;
  AnimationAssetBytes out;
  out.path = key;
  out.provider = animation_provider(resolver, *location);
  if (location->origin == archive::AssetOrigin::loose) {
    out.loose_path = location->loose_path;
    out.size = std::filesystem::file_size(location->loose_path);
  } else {
    out.size = resolver.archives().at(location->archive).entries().at(location->entry).unpacked_size();
  }
  if (out.size > k_max_asset_bytes) {
    throw std::runtime_error(std::string(path) + " is " + std::to_string(out.size) + " bytes, over the " +
                             std::to_string(k_max_asset_bytes) + " byte limit for an animation asset");
  }
  if (location->origin == archive::AssetOrigin::archive) out.bytes = resolver.read(*location);
  return out;
}

}  // namespace fo4::hkx
