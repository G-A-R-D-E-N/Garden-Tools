#include "fo4/hkx/animation_sources.hpp"

#include "fo4/io/path_text.hpp"
#include "fo4/loadorder/mo2_profile.hpp"
#include "fo4/text/encoding.hpp"

#include <algorithm>
#include <set>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace fo4::hkx {
namespace {

namespace fs = std::filesystem;

std::string folded_name(const fs::path& path) { return text::fold(io::utf8(path.filename())); }

std::vector<fs::path> top_level_archives(const fs::path& dir) {
  std::vector<std::pair<std::string, fs::path>> found;
  std::error_code ec;
  for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
    std::error_code file_ec;
    if (it->is_regular_file(file_ec) && text::fold(io::utf8(it->path().extension())) == ".ba2") {
      found.emplace_back(folded_name(it->path()), it->path());
    }
  }
  std::sort(found.begin(), found.end());
  std::vector<fs::path> paths;
  for (auto& item : found) paths.push_back(std::move(item.second));
  return paths;
}

bool has_vortex_marker(const fs::path& dir) {
  std::error_code ec;
  for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
    std::error_code file_ec;
    if (!it->is_regular_file(file_ec)) continue;
    const auto name = folded_name(it->path());
    if (name.starts_with("vortex.deployment") && name.ends_with(".json")) return true;
  }
  return false;
}

std::vector<fs::path> data_archives_low_to_high(const fs::path& data, const std::vector<std::string>& plugins) {
  const auto by_plugin = archive::archive_load_order(data, {}, plugins);
  std::set<std::string> placed;
  for (const auto& path : by_plugin) placed.insert(folded_name(path));
  const auto all = top_level_archives(data);
  std::vector<fs::path> ordered;
  for (auto it = all.rbegin(); it != all.rend(); ++it) {
    if (!placed.contains(folded_name(*it))) ordered.push_back(*it);
  }
  ordered.insert(ordered.end(), by_plugin.begin(), by_plugin.end());
  return ordered;
}

bool is_plain_name(const std::string& name) {
  return !name.empty() && name != "." && name != ".." && name.find_first_of("/\\:") == std::string::npos;
}

}  // namespace

AnimationSources open_animation_sources(const AnimationSourceRequest& request) {
  AnimationSources out;
  fs::path data = request.data_dir;
  std::vector<fs::path> mod_roots_low_to_high;
  std::vector<std::string> plugins;
  std::error_code ec;

  if (!request.mo2_instance.empty()) {
    if (!fs::is_regular_file(request.mo2_instance / "ModOrganizer.ini", ec)) {
      throw AnimationSourceError("not a Mod Organizer 2 instance, no ModOrganizer.ini in " +
                                 io::utf8(request.mo2_instance) +
                                 ". Vortex mod lists are not supported: pass only the deployed Data folder to index what Vortex linked there");
    }
    const auto instance = loadorder::read_mo2_instance(request.mo2_instance);
    out.profile = request.profile.empty() ? instance.selected_profile : request.profile;
    if (!is_plain_name(out.profile)) throw AnimationSourceError("the profile name must be a single folder name");
    const auto modlist = instance.profiles_dir / io::from_utf8(out.profile) / "modlist.txt";
    if (!fs::is_regular_file(modlist, ec)) {
      throw AnimationSourceError("profile '" + out.profile + "' has no modlist.txt at " + io::utf8(modlist));
    }
    loadorder::Mo2Profile profile;
    try {
      profile = loadorder::read_mo2_profile(instance, out.profile);
    } catch (const AnimationSourceError&) {
      throw;
    } catch (const std::invalid_argument& error) {
      throw AnimationSourceError(error.what());
    } catch (const std::runtime_error& error) {
      throw AnimationSourceError(error.what());
    }
    const auto roots = loadorder::mo2_asset_roots_low_to_high(instance, profile);
    const std::size_t skip = instance.data_dir.empty() ? 0 : 1;
    for (std::size_t i = skip; i < roots.size(); ++i) {
      if (fs::is_directory(roots[i], ec)) {
        mod_roots_low_to_high.push_back(roots[i]);
      } else if (roots[i] != instance.overwrite_dir) {
        out.notes.push_back("an enabled mod folder is missing: " + io::utf8(roots[i]));
        out.incomplete = true;
      }
    }
    for (const auto& entry : profile.plugins.entries) {
      if (entry.active) plugins.push_back(entry.name);
    }
    if (data.empty()) data = instance.data_dir;
    out.manager = "mo2";
  }

  if (data.empty()) {
    throw AnimationSourceError("no game Data folder: give one, or an MO2 instance whose ModOrganizer.ini names the game");
  }
  if (!fs::is_directory(data, ec)) throw AnimationSourceError("the game Data folder was not found: " + io::utf8(data));
  out.data_dir = data;
  if (out.manager == "none" && has_vortex_marker(data)) {
    out.manager = "vortex";
    out.notes.push_back(
        "a Vortex deployment was found: the Data folder is indexed as deployed, so every loose file shows the Data folder as its provider; the mod behind a file and Vortex's priorities are not read. Index an MO2 instance for per-mod providers");
  }

  std::vector<fs::path> roots_high_to_low(mod_roots_low_to_high.rbegin(), mod_roots_low_to_high.rend());
  roots_high_to_low.push_back(data);
  auto archives = data_archives_low_to_high(data, plugins);
  for (const auto& root : mod_roots_low_to_high) {
    const auto found = top_level_archives(root);
    archives.insert(archives.end(), found.begin(), found.end());
  }

  auto opened = archive::AssetResolver::open_roots(roots_high_to_low, archives, archive::ArchiveFailurePolicy::skip);
  for (const auto& failure : opened.failures) {
    out.notes.push_back("archive " + io::utf8(failure.path.filename()) + " was skipped: " + failure.message);
    out.incomplete = true;
  }
  out.resolver = std::move(opened.resolver);
  return out;
}

}  // namespace fo4::hkx
