#include "fo4/hkx/weapon_subgraph.hpp"

#include "fo4/hkx/subgraph.hpp"
#include "fo4/plugin_core/reader.hpp"

#include <algorithm>
#include <cctype>
#include <exception>
#include <set>
#include <utility>

namespace fo4::hkx {
namespace {

constexpr std::string_view k_weapon_marker = "animations\\weapon\\";

std::string lower(std::string_view text) {
  std::string out(text);
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return out;
}

std::string upper(std::string_view text) {
  std::string out(text);
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
  return out;
}

std::string backslashed(std::string_view text) {
  std::string out(text);
  std::replace(out.begin(), out.end(), '/', '\\');
  return out;
}

std::string cstring(const std::vector<std::byte>& data) {
  std::size_t length = 0;
  while (length < data.size() && data[length] != std::byte{0}) ++length;
  return std::string(reinterpret_cast<const char*>(data.data()), length);
}

std::uint32_t little_u32(const std::vector<std::byte>& data) {
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i) value |= static_cast<std::uint32_t>(std::to_integer<unsigned>(data[i])) << (8 * i);
  return value;
}

std::string file_stem(std::string_view path) {
  const std::string text = backslashed(path);
  const std::size_t slash = text.rfind('\\');
  std::string name = slash == std::string::npos ? text : text.substr(slash + 1);
  const std::size_t dot = name.rfind('.');
  if (dot != std::string::npos) name.erase(dot);
  return name;
}

bool starts_with_ignore_case(std::string_view text, std::string_view prefix) {
  return text.size() >= prefix.size() && lower(text.substr(0, prefix.size())) == prefix;
}

}  // namespace

std::vector<RaceAnimBlock> race_animation_blocks(const Record& race) {
  std::vector<RaceAnimBlock> blocks;
  if (race.header.signature.view() != "RACE") return blocks;

  RaceAnimBlock current;
  bool has_behavior = false;
  const auto close = [&] {
    if (current.set_id != 0 && has_behavior && !current.prefixes.empty()) blocks.push_back(std::move(current));
    current = RaceAnimBlock{};
    has_behavior = false;
  };
  for (const auto& subrecord : race.subrecords) {
    const auto signature = subrecord.signature.view();
    if (signature == "SAKD" && subrecord.data.size() >= 4) {
      close();
      current.set_id = little_u32(subrecord.data);
    } else if (signature == "SGNM" && !has_behavior) {
      current.behavior = cstring(subrecord.data);
      has_behavior = true;
    } else if (signature == "SAPT" && has_behavior) {
      current.prefixes.push_back(cstring(subrecord.data));
    } else if (signature == "SRAF" && subrecord.data.size() >= 4) {
      current.flags = little_u32(subrecord.data);
    } else if (signature != "STKD") {
      close();
    }
  }
  close();
  return blocks;
}

std::vector<WeaponTypeSet> weapon_type_sets(std::span<const RaceAnimBlock> blocks) {
  struct Accumulated {
    WeaponTypeSet set;
    std::set<std::string> seen;
  };
  std::map<std::string, Accumulated> by_type;
  for (const RaceAnimBlock& block : blocks) {
    if (lower(file_stem(block.behavior)).find("weapon") == std::string::npos) continue;
    std::string type;
    std::vector<std::string> prefixes;
    for (const std::string& raw : block.prefixes) {
      const std::string path = backslashed(raw);
      const std::size_t at = lower(path).find(k_weapon_marker);
      if (at == std::string::npos || at == 0) continue;
      std::string relative = path.substr(at);
      const std::size_t embedded = lower(relative).find("actors\\");
      if (embedded != std::string::npos && embedded > 0) relative.resize(embedded);
      if (relative.size() < k_weapon_marker.size()) continue;
      const std::size_t type_end = relative.find('\\', k_weapon_marker.size());
      const std::string folder = type_end == std::string::npos
                                     ? relative.substr(k_weapon_marker.size())
                                     : relative.substr(k_weapon_marker.size(), type_end - k_weapon_marker.size());
      if (folder.empty()) continue;
      if (type.empty()) type = folder;
      prefixes.push_back(relative);
    }
    if (type.empty()) continue;
    Accumulated& known = by_type[upper(type)];
    if (known.set.type.empty()) known.set.type = type;
    for (std::string& prefix : prefixes) {
      if (known.seen.insert(lower(prefix)).second) known.set.prefixes.push_back(std::move(prefix));
    }
  }
  std::vector<WeaponTypeSet> sets;
  sets.reserve(by_type.size());
  for (auto& entry : by_type) sets.push_back(std::move(entry.second.set));
  return sets;
}

namespace {

void note_probe_failure(WeaponSetCache& cache, std::string reason) {
  cache.probe_failed = true;
  ++cache.probe_failures;
  if (cache.probe_reason.empty()) cache.probe_reason = reason;
  cache.last_probe_reason = std::move(reason);
}

}  // namespace

const WeaponSets& weapon_sets(const archive::AssetResolver& assets, WeaponSetCache& cache) {
  if (cache.sets) return *cache.sets;
  WeaponSets result;
  try {
    const auto master = assets.locate_loose("Fallout4.esm");
    if (!master) {
      result.reason = "Fallout4.esm was not found in any loose root, so the weapon types are inferred from the project's folders";
    } else {
      const Plugin plugin = read_plugin_file(*master);
      std::vector<RaceAnimBlock> blocks;
      for (const Record* record : collect_records(plugin)) {
        if (record->header.signature.view() != "RACE") continue;
        ++result.races;
        for (RaceAnimBlock& block : race_animation_blocks(*record)) blocks.push_back(std::move(block));
      }
      result.blocks = blocks.size();
      result.sets = weapon_type_sets(blocks);
      if (result.sets.empty()) result.reason = "Fallout4.esm names no weapon animation sets, so the weapon types are inferred from the project's folders";
    }
  } catch (const std::exception& ex) {
    result.sets.clear();
    result.reason = std::string("Fallout4.esm could not be read, so the weapon types are inferred from the project's folders: ") + ex.what();
    note_probe_failure(cache, result.reason);
  }
  cache.sets = std::move(result);
  return *cache.sets;
}

namespace {

struct ProbeOutcome {
  bool found{false};
  bool failed{false};
};

ProbeOutcome probe_under(const archive::AssetResolver& assets, WeaponSetCache& cache, const std::string& root, const std::string& relative) {
  ProbeOutcome outcome;
  std::string error;
  const std::string path = root.empty() ? relative : root + "\\" + relative;
  outcome.found = animation_lookup(assets, path, error);
  if (error.empty()) return outcome;
  outcome.failed = true;
  note_probe_failure(cache, "could not look up meshes\\" + path + ": " + error);
  return outcome;
}

const std::vector<std::string>& weapon_folders(const archive::AssetResolver& assets, WeaponSetCache& cache, const std::string& root) {
  const auto found = cache.folders.find(root);
  if (found != cache.folders.end()) return found->second;
  std::set<std::string> names;
  std::string forward = "meshes/";
  if (!root.empty()) {
    std::string folder = lower(root);
    std::replace(folder.begin(), folder.end(), '\\', '/');
    forward += folder + "/";
  }
  forward += "animations/weapon";
  try {
    for (const archive::AssetListing& entry : assets.list(forward, ".hkx", false)) {
      const std::string path = lower(entry.path);
      if (path.size() <= forward.size() + 1 || path.compare(0, forward.size(), forward) != 0 || path[forward.size()] != '/') continue;
      const std::size_t start = forward.size() + 1;
      const std::size_t end = path.find('/', start);
      if (end != std::string::npos && end > start) names.insert(path.substr(start, end - start));
    }
  } catch (const std::exception& ex) {
    note_probe_failure(cache, "could not list " + forward + ": " + ex.what());
  }
  return cache.folders.emplace(root, std::vector<std::string>(names.begin(), names.end())).first->second;
}

}  // namespace

std::string weapon_gaps_more_message(std::size_t extra) {
  return "per-weapon coverage: " + std::to_string(extra) +
         " more missing clip(s) for other weapon types (extract the named animations under Animations\\Weapon)";
}

WeaponCheck check_weapon_subgraph(const archive::AssetResolver& assets, WeaponSetCache& cache, std::string_view project_root,
                                  std::span<const HkxGraphClip> clips) {
  WeaponCheck result;
  const std::string root = backslashed(project_root);

  std::map<std::string, std::string> referenced;
  struct Leaf {
    std::string leaf;
    std::size_t node{0};
  };
  std::vector<Leaf> generic;
  std::map<std::string, std::size_t> generic_at;
  for (const HkxGraphClip& clip : clips) {
    const std::string animation = backslashed(clip.animation);
    if (lower(animation).find(k_weapon_marker) != std::string::npos) result.weapon_subgraph = true;

    std::vector<std::string> parts;
    std::size_t begin = 0;
    while (begin <= animation.size()) {
      const std::size_t stop = animation.find('\\', begin);
      parts.push_back(animation.substr(begin, stop == std::string::npos ? std::string::npos : stop - begin));
      if (stop == std::string::npos) break;
      begin = stop + 1;
    }
    for (std::size_t i = 0; i + 1 < parts.size(); ++i) {
      if (lower(parts[i]) == "weapon" && !parts[i + 1].empty()) referenced.emplace(upper(parts[i + 1]), parts[i + 1]);
    }
    if (parts.size() == 2 && starts_with_ignore_case(animation, "animations\\")) {
      const std::string key = upper(parts[1]);
      const auto known = generic_at.find(key);
      if (known == generic_at.end()) {
        generic_at.emplace(key, generic.size());
        generic.push_back({parts[1], clip.node});
      } else {
        generic[known->second].node = clip.node;
      }
    }
  }
  if (referenced.empty() || generic.empty()) return result;

  const WeaponSets& known = weapon_sets(assets, cache);
  if (!known.sets.empty()) {
    result.basis = "race_sets";
    for (const WeaponTypeSet& set : known.sets) {
      if (set.prefixes.empty()) continue;
      for (const Leaf& leaf : generic) {
        bool undecided = false;
        bool in_chain = false;
        for (const std::string& prefix : set.prefixes) {
          const ProbeOutcome probe = probe_under(assets, cache, root, prefix + "\\" + leaf.leaf);
          if (probe.failed) {
            undecided = true;
            break;
          }
          if (probe.found) {
            in_chain = true;
            break;
          }
        }
        if (undecided || in_chain) continue;
        const ProbeOutcome generic_probe = probe_under(assets, cache, root, "Animations\\" + leaf.leaf);
        if (generic_probe.failed || generic_probe.found) continue;
        result.gaps.push_back({leaf.leaf,
                               "per-weapon coverage: '" + set.type + "' cannot resolve " + leaf.leaf +
                                   ": the engine searched its " + std::to_string(set.prefixes.size()) + "-prefix chain starting at " +
                                   set.prefixes.front() + "\\" + leaf.leaf + " and found no copy, and no generic Animations\\" +
                                   leaf.leaf + " fallback exists either - playing this clip for this weapon type is a crash",
                               leaf.node, true});
      }
    }
    return result;
  }

  const std::vector<std::string>& folders = weapon_folders(assets, cache, root);
  if (folders.empty()) return result;
  std::vector<const Leaf*> per_weapon;
  for (const Leaf& leaf : generic) {
    bool covered = false;
    for (const std::string& type : folders) {
      const ProbeOutcome probe = probe_under(assets, cache, root, "Animations\\Weapon\\" + type + "\\" + leaf.leaf);
      if (probe.failed) break;
      if (probe.found) {
        covered = true;
        break;
      }
    }
    if (covered) per_weapon.push_back(&leaf);
  }
  if (per_weapon.empty()) return result;

  result.basis = "folders";
  for (const auto& type_entry : referenced) {
    const std::string& type = type_entry.second;
    for (const Leaf* leaf : per_weapon) {
      const ProbeOutcome own = probe_under(assets, cache, root, "Animations\\Weapon\\" + type + "\\" + leaf->leaf);
      if (own.failed || own.found) continue;
      const ProbeOutcome generic_probe = probe_under(assets, cache, root, "Animations\\" + leaf->leaf);
      if (generic_probe.failed) continue;
      const bool generic_exists = generic_probe.found;
      std::string message = "per-weapon coverage: '" + type + "' lacks " + leaf->leaf + " under Animations\\Weapon\\" + type;
      if (generic_exists) {
        message += "; the generic Animations\\" + leaf->leaf +
                   " copy exists, so the engine falls back and the clip still plays (extract a per-weapon copy to override it)";
      } else {
        message += ", and no generic Animations\\" + leaf->leaf +
                   " copy exists either - playing this clip for this weapon type is a crash (extract the animation under Animations\\Weapon\\" +
                   type + ")";
      }
      result.gaps.push_back({leaf->leaf, std::move(message), leaf->node, !generic_exists});
    }
  }
  return result;
}

std::optional<std::string> resolve_weapon_clip(const archive::AssetResolver& assets, WeaponSetCache& cache, std::string_view project_root,
                                               std::string_view leaf, bool* failed) {
  const std::string root = backslashed(project_root);
  const std::string name(leaf);
  for (const WeaponTypeSet& set : weapon_sets(assets, cache).sets) {
    for (const std::string& prefix : set.prefixes) {
      const std::string candidate = prefix + "\\" + name;
      const ProbeOutcome probe = probe_under(assets, cache, root, candidate);
      if (probe.failed) {
        if (failed != nullptr) *failed = true;
        return std::nullopt;
      }
      if (probe.found) return root.empty() ? candidate : root + "\\" + candidate;
    }
  }
  for (const std::string& type : weapon_folders(assets, cache, root)) {
    const std::string candidate = "Animations\\Weapon\\" + type + "\\" + name;
    const ProbeOutcome probe = probe_under(assets, cache, root, candidate);
    if (probe.failed) {
      if (failed != nullptr) *failed = true;
      return std::nullopt;
    }
    if (probe.found) return root.empty() ? candidate : root + "\\" + candidate;
  }
  return std::nullopt;
}

}  // namespace fo4::hkx
