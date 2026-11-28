#include "fo4/hkx/subgraph_sweep.hpp"

#include "fo4/hkx/behavior_graph.hpp"
#include "fo4/hkx/packfile.hpp"
#include "fo4/hkx/subgraph.hpp"
#include "fo4/io/path_text.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <system_error>

namespace fo4::hkx {
namespace {

constexpr std::string_view k_manifest_folder = "meshes/animtextdata/animationfiledata";

std::string lower(std::string_view text) {
  std::string out(text);
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return out;
}

bool is_digit(char ch) { return ch >= '0' && ch <= '9'; }

std::optional<std::uint64_t> parse_decimal(std::string_view digits) {
  std::uint64_t value = 0;
  const auto [stop, error] = std::from_chars(digits.data(), digits.data() + digits.size(), value);
  if (error != std::errc{} || stop != digits.data() + digits.size()) return std::nullopt;
  return value;
}

std::string source_of(const archive::AssetResolver& assets, const archive::AssetLocation& location) {
  if (location.origin == archive::AssetOrigin::archive) return io::utf8(assets.archives().at(location.archive).path().filename());
  return "loose";
}

enum class BehaviourOutcome : std::uint8_t { skipped, missing, unreadable, read };

struct BehaviourWeapon {
  BehaviourOutcome outcome{BehaviourOutcome::skipped};
  bool weapon{false};
  std::vector<WeaponGap> gaps;
};

BehaviourWeapon inspect_behavior(const archive::AssetResolver& assets, WeaponSetCache& cache, const std::string& behavior_path,
                                 bool want_gaps) {
  BehaviourWeapon result;
  const std::string norm = subgraph_normalize(behavior_path);
  const std::size_t at = norm.rfind("\\behaviors\\");
  if (at == std::string::npos) return result;

  std::vector<std::byte> bytes;
  try {
    const auto location = assets.locate("meshes\\" + norm);
    if (!location) {
      result.outcome = BehaviourOutcome::missing;
      return result;
    }
    bytes = assets.read(*location);
  } catch (const std::exception&) {
    result.outcome = BehaviourOutcome::unreadable;
    return result;
  }
  HkxPackfile packfile;
  HkxGraphModel model;
  if (read_packfile(bytes, packfile) != HkxStatus::Ok) {
    result.outcome = BehaviourOutcome::unreadable;
    return result;
  }
  const HkxStatus status = read_behavior_model(packfile, model);
  if (status == HkxStatus::MissingObject) {
    result.outcome = BehaviourOutcome::read;
    return result;
  }
  if (status != HkxStatus::Ok) {
    result.outcome = BehaviourOutcome::unreadable;
    return result;
  }
  result.outcome = BehaviourOutcome::read;
  for (const HkxGraphClip& clip : model.clips) {
    std::string animation = lower(clip.animation);
    std::replace(animation.begin(), animation.end(), '/', '\\');
    if (animation.find("animations\\weapon\\") != std::string::npos) result.weapon = true;
  }
  if (want_gaps && result.weapon) {
    result.gaps = check_weapon_subgraph(assets, cache, norm.substr(0, at), model.clips).gaps;
  }
  return result;
}

std::vector<std::string> behaviors_with_hint(const SubgraphEntry& entry, const std::optional<OffsetData>& offsets) {
  std::vector<std::string> paths = entry.behavior_paths;
  if (offsets && offsets->hint) {
    const std::string wanted = subgraph_normalize(*offsets->hint);
    const bool known = std::any_of(paths.begin(), paths.end(), [&](const std::string& path) { return subgraph_normalize(path) == wanted; });
    if (!known) paths.push_back(*offsets->hint);
  }
  return paths;
}

SweepFailure failure_of(const SubgraphEntry& entry, std::vector<WeaponGap> gaps) {
  SweepFailure failure;
  failure.id = entry.id;
  failure.behavior = entry.behavior_paths.empty() ? std::string{} : entry.behavior_paths.front();
  failure.gap_total = gaps.size();
  failure.crash_gaps = static_cast<std::size_t>(std::count_if(gaps.begin(), gaps.end(), [](const WeaponGap& gap) { return gap.crash; }));
  if (gaps.size() > k_max_weapon_findings) gaps.resize(k_max_weapon_findings);
  failure.gaps = std::move(gaps);
  return failure;
}

}  // namespace

std::optional<CrashSubgraphId> find_crash_subgraph_id(std::string_view text) {
  const std::string folded = lower(text);
  constexpr std::string_view marker = "animationoffsets";
  for (std::size_t at = folded.find(marker); at != std::string::npos; at = folded.find(marker, at + 1)) {
    std::size_t cursor = at + marker.size();
    if (cursor >= folded.size() || (folded[cursor] != '\\' && folded[cursor] != '/')) continue;
    ++cursor;
    const std::size_t digits_at = cursor;
    while (cursor < folded.size() && is_digit(folded[cursor])) ++cursor;
    if (cursor == digits_at || folded.compare(cursor, 4, ".txt") != 0) continue;
    if (const auto id = parse_decimal(std::string_view(folded).substr(digits_at, cursor - digits_at))) {
      return CrashSubgraphId{*id, "animation_offsets_path"};
    }
  }
  for (std::size_t at = 0; at < folded.size();) {
    if (!is_digit(folded[at])) {
      ++at;
      continue;
    }
    std::size_t stop = at;
    while (stop < folded.size() && is_digit(folded[stop])) ++stop;
    const std::size_t length = stop - at;
    if (length >= 9 && length <= 20) {
      if (const auto id = parse_decimal(std::string_view(folded).substr(at, length))) return CrashSubgraphId{*id, "bare_number"};
    }
    at = stop;
  }
  return std::nullopt;
}

const SubgraphEntry* SubgraphManifestIndex::find(std::uint64_t id) const {
  const auto at = std::lower_bound(entries.begin(), entries.end(), id, [](const SubgraphEntry& entry, std::uint64_t wanted) { return entry.id < wanted; });
  return at != entries.end() && at->id == id ? &*at : nullptr;
}

SubgraphManifestIndex discover_subgraphs(const archive::AssetResolver& assets, std::size_t max_manifests) {
  SubgraphManifestIndex index;
  archive::AssetListResult listing;
  try {
    listing = assets.list_report(k_manifest_folder, ".txt", false);
  } catch (const std::exception& ex) {
    index.incomplete = true;
    index.notes.push_back(std::string("could not list the manifest folder: ") + ex.what());
    return index;
  }
  if (listing.incomplete()) {
    index.incomplete = true;
    index.notes.push_back(std::to_string(listing.nameless_archives.size()) + " archive(s) carry no file names, so the manifest listing may miss files");
  }
  std::sort(listing.entries.begin(), listing.entries.end(),
            [](const archive::AssetListing& a, const archive::AssetListing& b) { return a.path < b.path; });
  if (listing.entries.size() > max_manifests) {
    index.truncated = true;
    index.notes.push_back("only the first " + std::to_string(max_manifests) + " of " + std::to_string(listing.entries.size()) +
                          " manifest files (sorted by path) were read");
    listing.entries.resize(max_manifests);
  }

  std::map<std::uint64_t, SubgraphEntry> by_id;
  for (const archive::AssetListing& file : listing.entries) {
    ++index.manifest_files;
    std::string text;
    try {
      const auto bytes = assets.read(file.location);
      text.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    } catch (const std::exception&) {
      ++index.unreadable;
      continue;
    }
    auto parsed = parse_animation_file_data(text);
    if (!parsed) {
      ++index.unparsed;
      continue;
    }
    if (by_id.count(parsed->id) != 0) {
      ++index.duplicate_ids;
      continue;
    }
    SubgraphEntry entry;
    entry.id = parsed->id;
    entry.manifest = file.path;
    entry.source = source_of(assets, file.location);
    entry.behavior_paths = std::move(parsed->behavior_paths);
    entry.animation_paths = std::move(parsed->animation_paths);
    by_id.emplace(entry.id, std::move(entry));
  }
  index.entries.reserve(by_id.size());
  for (auto& item : by_id) index.entries.push_back(std::move(item.second));
  return index;
}

std::optional<OffsetData> read_offset_data(const archive::AssetResolver& assets, std::uint64_t id) {
  try {
    const auto location = assets.locate("meshes\\animtextdata\\animationoffsets\\" + std::to_string(id) + ".txt");
    if (!location) return std::nullopt;
    const auto bytes = assets.read(*location);
    return OffsetData{source_of(assets, *location), bytes.size(), parse_animation_offset_hint(bytes)};
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

SubgraphSweep sweep_subgraphs(const archive::AssetResolver& assets, const SweepLimits& limits) {
  SubgraphSweep sweep;
  sweep.archives = assets.archives().size();
  sweep.roots = assets.roots().size();
  const SubgraphManifestIndex index = discover_subgraphs(assets, limits.max_manifests);
  sweep.manifests = index.entries.size();
  sweep.duplicate_ids = index.duplicate_ids;
  sweep.truncated_manifests = index.truncated;
  sweep.incomplete = index.incomplete;
  sweep.notes = index.notes;
  sweep.manifests_unread = index.unparsed + index.unreadable;
  if (index.unparsed != 0) sweep.notes.push_back(std::to_string(index.unparsed) + " manifest file(s) could not be parsed");
  if (index.unreadable != 0) sweep.notes.push_back(std::to_string(index.unreadable) + " manifest file(s) could not be read");
  if (index.entries.empty()) {
    sweep.incomplete = true;
    sweep.notes.push_back("no subgraph manifest was read under " + std::string(k_manifest_folder) + ", so nothing was checked");
  }

  WeaponSetCache cache;
  std::map<std::string, BehaviourWeapon> seen;
  for (std::size_t at = 0; at < index.entries.size(); ++at) {
    const SubgraphEntry& entry = index.entries[at];
    const auto offsets = read_offset_data(assets, entry.id);
    const std::vector<std::string> paths = behaviors_with_hint(entry, offsets);

    std::size_t fresh = 0;
    for (const std::string& path : paths) {
      if (seen.count(subgraph_normalize(path)) == 0) ++fresh;
    }
    if (fresh != 0 && seen.size() + fresh > limits.max_behaviors) {
      sweep.truncated_behaviors = true;
      sweep.unchecked = index.entries.size() - at;
      break;
    }

    bool weapon = false;
    std::vector<WeaponGap> gaps;
    for (const std::string& path : paths) {
      const std::string key = subgraph_normalize(path);
      auto cached = seen.find(key);
      if (cached == seen.end()) {
        cached = seen.emplace(key, inspect_behavior(assets, cache, path, true)).first;
        switch (cached->second.outcome) {
          case BehaviourOutcome::read: ++sweep.behaviors_read; break;
          case BehaviourOutcome::missing: ++sweep.behaviors_missing; break;
          case BehaviourOutcome::unreadable: ++sweep.behaviors_unreadable; break;
          case BehaviourOutcome::skipped: ++sweep.behaviors_skipped; break;
        }
      }
      weapon = weapon || cached->second.weapon;
      gaps.insert(gaps.end(), cached->second.gaps.begin(), cached->second.gaps.end());
    }
    if (!weapon) continue;
    ++sweep.weapon_checked;
    if (gaps.empty()) continue;
    ++sweep.failures_total;
    if (sweep.failures.size() < limits.max_failures) {
      sweep.failures.push_back(failure_of(entry, std::move(gaps)));
    } else {
      sweep.truncated_failures = true;
    }
  }
  if (cache.probe_failed) {
    sweep.incomplete = true;
    sweep.notes.push_back("a per-weapon coverage lookup could not be read, so a missing clip was not counted as a gap: " + cache.probe_reason);
  }
  if (sweep.behaviors_skipped != 0) {
    sweep.notes.push_back(std::to_string(sweep.behaviors_skipped) + " behaviour path(s) are not under a Behaviors folder and were not opened");
  }
  if (cache.sets) {
    sweep.weapon_sets = cache.sets->sets.size();
    sweep.weapon_sets_reason = cache.sets->reason;
  }
  return sweep;
}

SubgraphDiff diff_subgraph_coverage(const archive::AssetResolver& vanilla, const archive::AssetResolver& modded,
                                    const DiffLimits& limits) {
  SubgraphDiff diff;
  const SubgraphManifestIndex before = discover_subgraphs(vanilla, limits.max_manifests);
  const SubgraphManifestIndex after = discover_subgraphs(modded, limits.max_manifests);
  diff.vanilla_manifests = before.entries.size();
  diff.modded_manifests = after.entries.size();
  diff.truncated_manifests = before.truncated || after.truncated;
  diff.incomplete = before.incomplete || after.incomplete;
  diff.manifests_unread = before.unparsed + before.unreadable + after.unparsed + after.unreadable;
  for (const std::string& note : before.notes) diff.notes.push_back("vanilla: " + note);
  for (const std::string& note : after.notes) diff.notes.push_back("modded: " + note);
  if (before.entries.empty()) {
    diff.incomplete = true;
    diff.notes.push_back("vanilla: no subgraph manifest was read under " + std::string(k_manifest_folder) + ", so every modded id would read as new");
  }
  if (after.entries.empty()) {
    diff.incomplete = true;
    diff.notes.push_back("modded: no subgraph manifest was read under " + std::string(k_manifest_folder) + ", so nothing was compared");
  }

  std::set<std::uint64_t> before_ids;
  std::set<std::uint64_t> after_ids;
  std::set<std::string> before_behaviors;
  std::set<std::string> after_behaviors;
  for (const SubgraphEntry& entry : before.entries) {
    before_ids.insert(entry.id);
    for (const std::string& path : entry.behavior_paths) before_behaviors.insert(subgraph_normalize(path));
  }
  for (const SubgraphEntry& entry : after.entries) {
    after_ids.insert(entry.id);
    for (const std::string& path : entry.behavior_paths) after_behaviors.insert(subgraph_normalize(path));
  }

  std::vector<std::uint64_t> new_ids;
  std::vector<std::uint64_t> gone_ids;
  std::set_difference(after_ids.begin(), after_ids.end(), before_ids.begin(), before_ids.end(), std::back_inserter(new_ids));
  std::set_difference(before_ids.begin(), before_ids.end(), after_ids.begin(), after_ids.end(), std::back_inserter(gone_ids));
  std::vector<std::string> new_behaviors;
  std::set_difference(after_behaviors.begin(), after_behaviors.end(), before_behaviors.begin(), before_behaviors.end(),
                      std::back_inserter(new_behaviors));

  std::map<std::string, std::size_t> cluster;
  for (const std::uint64_t id : new_ids) {
    const SubgraphEntry* entry = after.find(id);
    if (entry != nullptr && !entry->behavior_paths.empty()) ++cluster[subgraph_normalize(entry->behavior_paths.front())];
  }
  std::vector<std::pair<std::string, std::size_t>> clusters(cluster.begin(), cluster.end());
  std::stable_sort(clusters.begin(), clusters.end(), [](const auto& a, const auto& b) { return a.second > b.second; });

  WeaponSetCache cache;
  std::vector<std::string> weapon_behaviors;
  for (std::size_t at = 0; at < new_behaviors.size(); ++at) {
    if (at >= limits.max_weapon_reads) {
      diff.truncated_weapon_reads = true;
      diff.weapon_unread = new_behaviors.size() - at;
      break;
    }
    const BehaviourWeapon result = inspect_behavior(modded, cache, new_behaviors[at], false);
    if (result.outcome == BehaviourOutcome::missing) ++diff.weapon_missing;
    if (result.outcome == BehaviourOutcome::unreadable) ++diff.weapon_unreadable;
    if (result.outcome == BehaviourOutcome::skipped) ++diff.weapon_skipped;
    if (result.outcome == BehaviourOutcome::read && result.weapon) weapon_behaviors.push_back(new_behaviors[at]);
  }

  diff.new_ids_total = new_ids.size();
  diff.gone_ids_total = gone_ids.size();
  diff.new_behaviors_total = new_behaviors.size();
  diff.clusters_total = clusters.size();
  diff.new_weapon_total = weapon_behaviors.size();
  const auto cap = [&](auto& values) {
    if (values.size() > limits.max_listed) {
      values.resize(limits.max_listed);
      diff.truncated_lists = true;
    }
  };
  cap(new_ids);
  cap(gone_ids);
  cap(new_behaviors);
  cap(clusters);
  cap(weapon_behaviors);
  diff.new_ids = std::move(new_ids);
  diff.gone_ids = std::move(gone_ids);
  diff.new_behaviors = std::move(new_behaviors);
  diff.clusters = std::move(clusters);
  diff.new_weapon_behaviors = std::move(weapon_behaviors);
  return diff;
}

SubgraphSweep run_subgraph_sweep(const AnimationSourceRequest& request, const SweepLimits& limits) {
  const AnimationSources sources = open_animation_sources(request);
  SubgraphSweep sweep = sweep_subgraphs(sources.resolver, limits);
  sweep.incomplete = sweep.incomplete || sources.incomplete;
  sweep.notes.insert(sweep.notes.begin(), sources.notes.begin(), sources.notes.end());
  return sweep;
}

SubgraphDiff run_subgraph_diff(const AnimationSourceRequest& request, const DiffLimits& limits) {
  if (request.mo2_instance.empty()) {
    throw AnimationSourceError("a coverage diff compares the game's Data folder with a modded load order, so it needs an MO2 instance");
  }
  const AnimationSources modded = open_animation_sources(request);
  AnimationSourceRequest plain;
  plain.data_dir = modded.data_dir;
  const AnimationSources vanilla = open_animation_sources(plain);
  SubgraphDiff diff = diff_subgraph_coverage(vanilla.resolver, modded.resolver, limits);
  diff.incomplete = diff.incomplete || modded.incomplete || vanilla.incomplete;
  diff.notes.insert(diff.notes.begin(), modded.notes.begin(), modded.notes.end());
  return diff;
}

CrashSubgraph analyze_crash_subgraph(const AnimationSourceRequest& request, const CrashInput& input, const CrashLimits& limits) {
  CrashSubgraph result;
  std::string text = input.text;
  if (!input.log.empty() && !input.text.empty()) {
    result.refusal = "give either a crash log file or its text, not both";
    return result;
  }
  if (!input.log.empty()) {
    std::error_code ec;
    const std::uintmax_t size = std::filesystem::file_size(input.log, ec);
    if (ec) {
      result.refusal = "the crash log could not be read: " + io::utf8(input.log);
      return result;
    }
    if (size > limits.max_log_bytes) {
      result.refusal = "the crash log is " + std::to_string(size) + " bytes, over the " + std::to_string(limits.max_log_bytes) + " byte limit";
      return result;
    }
    std::ifstream stream(input.log, std::ios::binary);
    text.resize(static_cast<std::size_t>(size));
    if (stream) stream.read(text.data(), static_cast<std::streamsize>(size));
    if (!stream) {
      result.refusal = "the crash log could not be read: " + io::utf8(input.log);
      return result;
    }
  }
  if (text.empty()) {
    result.refusal = input.log.empty() ? "give a crash log file or its text" : "the crash log is empty: " + io::utf8(input.log);
    return result;
  }
  if (text.size() > limits.max_log_bytes) {
    result.refusal = "the crash text is over the " + std::to_string(limits.max_log_bytes) + " byte limit";
    return result;
  }

  const auto found = find_crash_subgraph_id(text);
  if (!found) {
    result.notes.push_back("no AnimTextData id was found in the crash input");
    return result;
  }
  result.id_found = true;
  result.id = found->id;
  result.matched = found->matched;
  result.confirmed = found->matched == "animation_offsets_path";
  if (!result.confirmed) result.notes.push_back("the id is the first run of 9 to 20 digits in the input, not a confirmed AnimationOffsets path");

  const AnimationSources sources = open_animation_sources(request);
  result.notes.insert(result.notes.end(), sources.notes.begin(), sources.notes.end());
  result.incomplete = sources.incomplete;
  const archive::AssetResolver& assets = sources.resolver;

  std::optional<SubgraphEntry> entry;
  if (const auto manifest = read_subgraph_manifest(assets, result.id)) {
    if (auto parsed = parse_animation_file_data(manifest->text); parsed && parsed->id == result.id) {
      SubgraphEntry value;
      value.id = parsed->id;
      value.manifest = subgraph_manifest_path(result.id);
      value.source = manifest->source;
      value.behavior_paths = std::move(parsed->behavior_paths);
      value.animation_paths = std::move(parsed->animation_paths);
      entry = std::move(value);
    } else if (const auto declared = animation_file_data_id(manifest->text); declared && *declared == result.id) {
      SubgraphEntry value;
      value.id = *declared;
      value.manifest = subgraph_manifest_path(result.id);
      value.source = manifest->source;
      entry = std::move(value);
    }
  }
  if (!entry) {
    const SubgraphManifestIndex index = discover_subgraphs(assets, 20000);
    if (index.truncated) {
      result.incomplete = true;
      result.notes.push_back("the manifest search was cut short, so a missing manifest is not proven");
    }
    if (index.incomplete) {
      result.incomplete = true;
      result.notes.insert(result.notes.end(), index.notes.begin(), index.notes.end());
    }
    if (const SubgraphEntry* known = index.find(result.id)) entry = *known;
  }

  if (entry) {
    result.manifests = 1;
    result.manifest_found = true;
    result.manifest = entry->manifest;
    result.source = entry->source;
    result.behavior_paths = entry->behavior_paths;
    result.animation_total = entry->animation_paths.size();
    for (const std::string& declared : entry->animation_paths) {
      if (animation_exists(assets, declared)) {
        ++result.animation_present;
      } else {
        ++result.missing_total;
        if (result.missing.size() < limits.max_missing_listed) result.missing.push_back(declared);
      }
    }
  } else {
    result.incomplete = true;
    result.notes.push_back("no manifest carries subgraph id " + std::to_string(result.id) +
                           ", so its animations and behaviours were not checked");
  }

  const auto offsets = read_offset_data(assets, result.id);
  if (offsets) {
    result.offsets_found = true;
    result.offsets_source = offsets->source;
    result.offsets_bytes = offsets->bytes;
    if (offsets->hint) result.offsets_hint = *offsets->hint;
  }

  std::vector<std::string> paths = result.behavior_paths;
  if (offsets && offsets->hint) {
    SubgraphEntry shell;
    shell.behavior_paths = paths;
    paths = behaviors_with_hint(shell, offsets);
  }
  if (result.manifest_found && paths.empty()) {
    result.incomplete = true;
    result.notes.push_back("the manifest names no behaviour files and no offsets hint added one, so no weapon gap was checked");
  }
  WeaponSetCache cache;
  std::vector<WeaponGap> gaps;
  for (const std::string& path : paths) {
    const BehaviourWeapon checked = inspect_behavior(assets, cache, path, true);
    switch (checked.outcome) {
      case BehaviourOutcome::read: ++result.behaviors_checked; break;
      case BehaviourOutcome::missing: result.unchecked_behaviors.push_back(path + ": missing"); break;
      case BehaviourOutcome::unreadable: result.unchecked_behaviors.push_back(path + ": unreadable"); break;
      case BehaviourOutcome::skipped: result.unchecked_behaviors.push_back(path + ": not under a Behaviors folder"); break;
    }
    result.weapon_subgraph = result.weapon_subgraph || checked.weapon;
    gaps.insert(gaps.end(), checked.gaps.begin(), checked.gaps.end());
  }
  if (cache.probe_failed) {
    result.incomplete = true;
    result.notes.push_back("a per-weapon coverage lookup could not be read, so a missing clip was not counted as a gap: " + cache.probe_reason);
  }
  if (!result.unchecked_behaviors.empty()) {
    result.incomplete = true;
    result.notes.push_back(std::to_string(result.unchecked_behaviors.size()) + " named behaviour(s) were not read, so no weapon gap was checked for them (see unchecked_behaviors)");
  }
  result.gap_total = gaps.size();
  if (gaps.size() > k_max_weapon_findings) gaps.resize(k_max_weapon_findings);
  result.gaps = std::move(gaps);
  return result;
}

}  // namespace fo4::hkx
