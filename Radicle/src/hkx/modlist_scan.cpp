#include "fo4/hkx/modlist_scan.hpp"

#include "fo4/hkx/behavior_graph.hpp"
#include "fo4/hkx/packfile.hpp"
#include "fo4/hkx/project_chain.hpp"
#include "fo4/hkx/weapon_subgraph.hpp"
#include "fo4/io/path_text.hpp"

#include <algorithm>
#include <cctype>
#include <exception>
#include <optional>
#include <set>
#include <system_error>

namespace fo4::hkx {
namespace {

std::string lower(std::string_view text) {
  std::string out(text);
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return out;
}

bool under(const std::filesystem::path& file, const std::filesystem::path& root) {
  const std::filesystem::path relative = file.lexically_normal().lexically_relative(root.lexically_normal());
  return !relative.empty() && *relative.begin() != "..";
}

bool is_mod_owned(const archive::AssetResolver& assets, const archive::AssetListing& entry, const std::vector<std::filesystem::path>& mods) {
  const std::filesystem::path where = entry.location.origin == archive::AssetOrigin::archive
                                          ? assets.archives().at(entry.location.archive).path()
                                          : entry.location.loose_path;
  return std::any_of(mods.begin(), mods.end(), [&](const std::filesystem::path& root) { return under(where, root); });
}

std::string source_of(const archive::AssetResolver& assets, const archive::AssetLocation& location) {
  if (location.origin == archive::AssetOrigin::archive) return io::utf8(assets.archives().at(location.archive).path().filename());
  return "loose";
}

class Scan {
public:
  Scan(const archive::AssetResolver& assets, const ModlistScanLimits& limits, ModlistScan& out) : assets_(assets), limits_(limits), out_(out) {}

  void run(const std::vector<std::string>& paths);

private:
  const archive::AssetResolver& assets_;
  const ModlistScanLimits& limits_;
  ModlistScan& out_;
  WeaponSetCache weapons_;
  std::set<std::string> animations_seen_;

  void add(std::string kind, const std::string& path, const std::string& source, std::vector<std::string> details);
  void unreadable(const char* kind, const std::string& path, std::string reason);
  void losses(const std::vector<std::byte>& bytes, const std::string& path, const std::string& source);
  void character(const std::string& path, const std::string& root, const std::vector<std::byte>& bytes, const std::string& source);
  void behavior(const std::string& path, const std::string& root, const std::vector<std::byte>& bytes, const std::string& source);
  std::optional<std::string> resolve_clip(const std::string& root, const std::string& animation, std::string& error);
};

void Scan::add(std::string kind, const std::string& path, const std::string& source, std::vector<std::string> details) {
  ++out_.findings_total;
  if (out_.findings.size() >= limits_.max_findings) {
    out_.truncated_findings = true;
    return;
  }
  ModlistFinding finding;
  finding.kind = std::move(kind);
  finding.path = path;
  finding.source = source;
  finding.detail_total = details.size();
  if (details.size() > limits_.max_details) details.resize(limits_.max_details);
  finding.details = std::move(details);
  out_.findings.push_back(std::move(finding));
}

void Scan::unreadable(const char* kind, const std::string& path, std::string reason) {
  ++out_.unreadable;
  add(kind, path, std::string{}, {std::move(reason)});
}

void Scan::losses(const std::vector<std::byte>& bytes, const std::string& path, const std::string& source) {
  const std::vector<HkxRoundTripLoss> found = project_round_trip_losses(bytes);
  if (found.empty()) return;
  out_.round_trip_losses += found.size();
  std::vector<std::string> details;
  for (const HkxRoundTripLoss& loss : found) details.push_back(loss.kind + ": " + loss.message);
  add("roundtrip-loss", path, source, std::move(details));
}

void Scan::character(const std::string& path, const std::string& root, const std::vector<std::byte>& bytes, const std::string& source) {
  HkxPackfile packfile;
  if (read_packfile(bytes, packfile) != HkxStatus::Ok) {
    unreadable("unreadable-character", path, "cannot be read as a packfile");
    return;
  }
  HkxCharacterLinks links;
  const HkxStatus status = read_character_links(packfile, links);
  if (status == HkxStatus::MissingObject) return;
  if (status != HkxStatus::Ok) {
    unreadable("unreadable-character", path, "unsupported HKX class layout");
    return;
  }
  if (links.animations.empty()) return;
  ++out_.checked;
  std::vector<std::string> gone;
  for (const std::string& declared : links.animations) {
    const HkxChainLink link = resolve_declared_animation(assets_, root, declared);
    if (link.exists) continue;
    if (!link.blocked && !link.note.empty()) {
      unreadable("unreadable-character", path, declared + ": " + link.note);
      continue;
    }
    gone.push_back(declared);
  }
  if (gone.empty()) return;
  ++out_.broken;
  out_.missing += gone.size();
  add("missing-animations", path, source, std::move(gone));
}

std::optional<std::string> Scan::resolve_clip(const std::string& root, const std::string& animation, std::string& error) {
  error.clear();
  const HkxChainLink direct = resolve_declared_animation(assets_, root, animation);
  if (direct.exists) return direct.resolved;
  if (!direct.blocked && !direct.note.empty()) {
    error = animation + ": " + direct.note;
    return std::nullopt;
  }
  std::string flat = animation;
  std::replace(flat.begin(), flat.end(), '\\', '/');
  std::vector<std::string> parts;
  std::size_t begin = 0;
  while (begin <= flat.size()) {
    const std::size_t stop = flat.find('/', begin);
    const std::string part = flat.substr(begin, stop == std::string::npos ? std::string::npos : stop - begin);
    if (!part.empty()) parts.push_back(part);
    if (stop == std::string::npos) break;
    begin = stop + 1;
  }
  if (parts.size() != 2 || lower(parts[0]) != "animations") return std::nullopt;
  const std::string relative_root = root.size() > 7 ? root.substr(7) : std::string{};
  bool failed = false;
  const auto found = resolve_weapon_clip(assets_, weapons_, relative_root, parts[1], &failed);
  if (found) return "meshes\\" + *found;
  if (failed) error = animation + ": " + weapons_.probe_reason;
  return std::nullopt;
}

void Scan::behavior(const std::string& path, const std::string& root, const std::vector<std::byte>& bytes, const std::string& source) {
  HkxPackfile packfile;
  if (read_packfile(bytes, packfile) != HkxStatus::Ok) {
    unreadable("unreadable-behaviour", path, "cannot be read as a packfile");
    return;
  }
  HkxGraphModel model;
  const HkxStatus status = read_behavior_model(packfile, model);
  if (status == HkxStatus::MissingObject) return;
  if (status != HkxStatus::Ok) {
    unreadable("unreadable-behaviour", path, "unsupported HKX class layout");
    return;
  }
  std::vector<const HkxGraphClip*> clips;
  for (const HkxGraphClip& clip : model.clips) {
    const bool blank = std::all_of(clip.animation.begin(), clip.animation.end(), [](unsigned char ch) { return std::isspace(ch) != 0; });
    if (!blank) clips.push_back(&clip);
  }
  if (clips.empty()) return;
  ++out_.checked;

  std::vector<std::string> gone;
  for (const HkxGraphClip* clip : clips) {
    std::string error;
    const auto resolved = resolve_clip(root, clip->animation, error);
    if (!resolved) {
      if (!error.empty()) {
        unreadable("unreadable-behaviour", path, error);
        continue;
      }
      const std::string name = clip->node < model.nodes.size() ? model.nodes[clip->node].name : std::string{};
      gone.push_back("clip '" + name + "' plays '" + clip->animation + "' - resolves nowhere");
      continue;
    }
    if (!animations_seen_.insert(lower(*resolved)).second) continue;
    if (animations_seen_.size() > limits_.max_animations) {
      out_.truncated_animations = true;
      continue;
    }
    try {
      if (const auto location = assets_.locate(*resolved)) losses(assets_.read(*location), *resolved, source_of(assets_, *location));
    } catch (const std::exception& ex) {
      out_.incomplete = true;
      out_.notes.push_back("could not read " + *resolved + " to check its round trip: " + ex.what());
    }
  }
  if (gone.empty()) return;
  ++out_.broken;
  out_.missing += gone.size();
  add("missing-clip-animations", path, source, std::move(gone));
}

void Scan::run(const std::vector<std::string>& paths) {
  const bool characters = out_.mode == ModlistScanMode::characters;
  const std::string folder = characters ? "/characters/" : "/behaviors/";
  for (const std::string& path : paths) {
    ++out_.scanned;
    std::vector<std::byte> bytes;
    std::string source;
    try {
      const auto location = assets_.locate(path);
      if (!location) {
        unreadable(characters ? "unreadable-character" : "unreadable-behaviour", path, "winning copy could not be read");
        continue;
      }
      bytes = assets_.read(*location);
      source = source_of(assets_, *location);
    } catch (const std::exception& ex) {
      unreadable(characters ? "unreadable-character" : "unreadable-behaviour", path, ex.what());
      continue;
    }
    losses(bytes, path, source);
    const std::size_t at = path.find(folder);
    if (at == std::string::npos) continue;
    const std::string root = path.substr(0, at);
    if (characters) {
      character(path, root, bytes, source);
    } else {
      behavior(path, root, bytes, source);
    }
  }
}

}  // namespace

ModlistScan scan_modlist(const archive::AssetResolver& assets, const std::filesystem::path& data_dir, ModlistScanMode mode,
                         const ModlistScanLimits& limits) {
  ModlistScan out;
  out.mode = mode;
  std::vector<std::filesystem::path> mods;
  for (const std::filesystem::path& root : assets.roots()) {
    std::error_code ec;
    if (data_dir.empty() || !std::filesystem::equivalent(root, data_dir, ec)) mods.push_back(root);
  }
  out.mod_roots = mods.size();
  if (mods.empty()) {
    out.incomplete = true;
    out.notes.push_back("no mod root was found besides the Data folder, so there is nothing to scan");
    return out;
  }

  const std::string folder = mode == ModlistScanMode::characters ? "/characters/" : "/behaviors/";
  std::set<std::string> owned;
  try {
    const archive::AssetListResult listing = assets.list_report("meshes", ".hkx", true);
    if (listing.incomplete()) {
      out.incomplete = true;
      out.notes.push_back(std::to_string(listing.nameless_archives.size()) + " archive(s) carry no file names, so the listing may miss files");
    }
    for (const archive::AssetListing& entry : listing.entries) {
      if (entry.path.find(folder) != std::string::npos && is_mod_owned(assets, entry, mods)) owned.insert(entry.path);
    }
  } catch (const std::exception& ex) {
    out.incomplete = true;
    out.notes.push_back(std::string("could not list the meshes folder: ") + ex.what());
    return out;
  }
  std::vector<std::string> paths(owned.begin(), owned.end());
  out.candidates = paths.size();
  if (paths.empty()) {
    out.incomplete = true;
    out.notes.push_back("no mod-owned file was found under " + folder + " in the meshes folder, so nothing was scanned");
  }
  if (paths.size() > limits.max_files) {
    out.truncated_files = true;
    paths.resize(limits.max_files);
  }
  Scan(assets, limits, out).run(paths);
  return out;
}

ModlistScan run_modlist_scan(const AnimationSourceRequest& request, ModlistScanMode mode, const ModlistScanLimits& limits) {
  if (request.mo2_instance.empty()) throw AnimationSourceError("a modlist scan is about the mods, so it needs an MO2 instance");
  const AnimationSources sources = open_animation_sources(request);
  ModlistScan out = scan_modlist(sources.resolver, sources.data_dir, mode, limits);
  out.incomplete = out.incomplete || sources.incomplete;
  out.notes.insert(out.notes.begin(), sources.notes.begin(), sources.notes.end());
  return out;
}

}  // namespace fo4::hkx
