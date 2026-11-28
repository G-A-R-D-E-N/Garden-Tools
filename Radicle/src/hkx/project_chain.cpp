#include "fo4/hkx/project_chain.hpp"

#include "fo4/hkx/behavior_graph.hpp"
#include "fo4/hkx/behavior_validate.hpp"
#include "fo4/hkx/layout.hpp"
#include "fo4/hkx/write.hpp"
#include "fo4/io/path_text.hpp"
#include "hkx_read_internal.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace fo4::hkx {
namespace {

using detail::Elements;
using detail::elements_at;
using detail::holds_layout;
using detail::in_section;
using detail::local_target;
using detail::read_string;

constexpr std::size_t k_pointer_size = 8;
constexpr std::size_t k_budget_factor = 16;

std::string_view status_text(HkxStatus status) {
  constexpr std::array<std::string_view, 10> names{
      "Ok",         "Truncated",       "BadMagic",     "UnsupportedLayout", "MissingSection",
      "BadSection", "FixupOutOfRange", "DuplicateFixup", "BadClassName",      "MissingObject",
  };
  const auto at = static_cast<std::size_t>(status);
  return at < names.size() ? names[at] : std::string_view{"unknown"};
}

std::string lower(std::string_view text) {
  std::string out(text);
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return out;
}

std::string join_parts(const std::vector<std::string>& parts) {
  std::string out;
  for (const std::string& part : parts) {
    if (!out.empty()) out.push_back('/');
    out += part;
  }
  return out;
}

// Constraint: components are lowercased and split on both separators; `.` and empty components drop out, and a drive or stream name refuses the whole path.
std::optional<std::vector<std::string>> split_path(std::string_view text) {
  if (text.find(':') != std::string_view::npos) return std::nullopt;
  std::vector<std::string> parts;
  std::string current;
  const auto flush = [&] {
    if (!current.empty() && current != ".") parts.push_back(current);
    current.clear();
  };
  for (const char raw : text) {
    if (raw == '/' || raw == '\\') {
      flush();
    } else {
      current.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(raw))));
    }
  }
  flush();
  return parts;
}

// Requires the base to start with `meshes`; the result stays below it, or the declared path is refused.
std::optional<std::vector<std::string>> join_under_meshes(const std::vector<std::string>& base, std::string_view declared) {
  if (declared.empty() || declared.front() == '/' || declared.front() == '\\') return std::nullopt;
  const auto relative = split_path(declared);
  if (!relative) return std::nullopt;
  std::vector<std::string> parts = base;
  for (const std::string& part : *relative) {
    if (part == "..") {
      if (parts.size() <= 1) return std::nullopt;
      parts.pop_back();
    } else {
      parts.push_back(part);
    }
  }
  if (parts.size() < 2 || parts.front() != "meshes") return std::nullopt;
  return parts;
}

std::string with_hkx_extension(const std::string& path) {
  const std::size_t slash = path.rfind('/');
  const std::size_t name_at = slash == std::string::npos ? 0 : slash + 1;
  const std::size_t dot = path.rfind('.');
  if (dot == std::string::npos || dot < name_at) return path + ".hkx";
  std::string out = path;
  out.replace(dot, std::string::npos, ".hkx");
  return out;
}

struct Found {
  archive::AssetLocation location;
  std::string source;
  std::string path;
};

// Constraint: a lookup that throws is reported through `error` and reads as not found, never as a crash of the whole report.
std::optional<Found> lookup(const archive::AssetResolver& assets, const std::string& path, std::string& error) {
  std::vector<std::string> candidates{path};
  if (const std::string swapped = with_hkx_extension(path); swapped != path) candidates.push_back(swapped);
  for (const std::string& candidate : candidates) {
    try {
      const auto location = assets.locate(candidate);
      if (!location) continue;
      std::string source = "loose";
      if (location->origin == archive::AssetOrigin::archive) {
        source = io::utf8(assets.archives().at(location->archive).path().filename());
      }
      return Found{*location, std::move(source), candidate};
    } catch (const std::exception& ex) {
      error = ex.what();
      return std::nullopt;
    }
  }
  return std::nullopt;
}

std::optional<std::vector<std::byte>> read_found(const archive::AssetResolver& assets, const Found& found,
                                                 std::string& error) {
  try {
    return assets.read(found.location);
  } catch (const std::exception& ex) {
    error = ex.what();
    return std::nullopt;
  }
}

const HkxObjectView* find_object(const HkxPackfile& packfile, std::string_view class_name) {
  for (const HkxObjectView& object : packfile.objects) {
    if (object.class_name == class_name) return &object;
  }
  return nullptr;
}

struct Budget {
  std::size_t left{0};
};

// Constraint: one pooled string may be pointed at many times, so the decoded bytes in total are capped at 16 times the section.
HkxStatus string_at(const HkxSection& section, std::size_t field, Budget& budget, std::string& out) {
  out.clear();
  if (!in_section(section, field, k_pointer_size)) return HkxStatus::FixupOutOfRange;
  const auto target = local_target(section, field);
  if (!target) return HkxStatus::Ok;
  if (const auto status = read_string(section, *target, out); status != HkxStatus::Ok) return status;
  if (out.size() + 1 > budget.left) return HkxStatus::FixupOutOfRange;
  budget.left -= out.size() + 1;
  return HkxStatus::Ok;
}

HkxStatus string_array_at(const HkxSection& section, std::size_t field, Budget& budget, std::vector<std::string>& out) {
  Elements elements;
  if (const auto status = elements_at(section, field, k_pointer_size, k_pointer_size, elements); status != HkxStatus::Ok) {
    return status;
  }
  for (std::size_t i = 0; i < elements.count; ++i) {
    std::string text;
    if (const auto status = string_at(section, elements.first + i * k_pointer_size, budget, text); status != HkxStatus::Ok) {
      return status;
    }
    if (!text.empty()) out.push_back(std::move(text));
  }
  return HkxStatus::Ok;
}

struct Located {
  const HkxObjectView* object{nullptr};
  const HkxClassLayout* layout{nullptr};
  const HkxSection* section{nullptr};
};

HkxStatus locate_object(const HkxPackfile& packfile, std::string_view class_name, Located& out) {
  const HkxObjectView* object = find_object(packfile, class_name);
  if (object == nullptr) return HkxStatus::MissingObject;
  if (packfile.pointer_size != k_pointer_size) return HkxStatus::UnsupportedLayout;
  const HkxClassLayout* layout = find_layout(object->class_name, object->signature);
  if (layout == nullptr) return HkxStatus::UnsupportedLayout;
  if (!holds_layout(*object, *layout)) return HkxStatus::Truncated;
  out = {object, layout, &packfile.sections[packfile.data_section]};
  return HkxStatus::Ok;
}

std::optional<std::size_t> member_at(const Located& located, std::string_view member) {
  const auto offset = find_member_offset(*located.layout, member);
  if (!offset) return std::nullopt;
  return static_cast<std::size_t>(located.object->offset) + *offset;
}

bool holds_project_data(const std::vector<std::byte>& bytes, std::string& refusal) {
  refusal.clear();
  HkxPackfile packfile;
  if (const auto status = read_packfile(bytes, packfile); status != HkxStatus::Ok) {
    refusal = describe_packfile_refusal(bytes);
    if (refusal.empty()) refusal = std::string(status_text(status));
    return false;
  }
  return find_object(packfile, "hkbProjectStringData") != nullptr;
}

std::string animation_key(std::string_view name) {
  std::string out = lower(name);
  std::replace(out.begin(), out.end(), '/', '\\');
  const std::size_t slash = out.rfind('\\');
  const std::size_t dot = out.rfind('.');
  if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) out.erase(dot);
  return out;
}

HkxChainLink resolve_declared(const archive::AssetResolver& assets, const std::vector<std::string>& root, std::string role,
                              const std::string& declared) {
  HkxChainLink link;
  link.role = std::move(role);
  link.declared = declared;
  const auto parts = join_under_meshes(root, declared);
  if (!parts) {
    link.blocked = true;
    link.note = "outside the allowed folders";
    return link;
  }
  const std::string path = join_parts(*parts);
  std::string error;
  if (const auto found = lookup(assets, path, error)) {
    link.resolved = found->path;
    link.source = found->source;
    link.exists = true;
    return link;
  }
  link.resolved = path;
  if (!error.empty()) link.note = "could not look up: " + error;
  return link;
}

std::string nameless_text(const archive::AssetListResult& listing) {
  return std::to_string(listing.nameless_archives.size()) + " archive(s) carry no file names, so the listing may miss files";
}

std::string leaf_of(const std::vector<std::string>& parts) { return parts.empty() ? std::string{} : parts.back(); }

bool is_project_subfolder(const std::string& leaf) {
  return leaf == "behaviors" || leaf == "characters" || leaf == "characterassets" || leaf == "animations";
}

class Chain {
public:
  Chain(const archive::AssetResolver& assets, HkxProjectChain& chain) : assets_(assets), chain_(chain) {}

  void run(std::string_view any_project_hkx);

private:
  const archive::AssetResolver& assets_;
  HkxProjectChain& chain_;
  std::vector<std::string> root_;

  std::optional<Found> find_project(const std::string& input, const std::vector<std::string>& directory);
  void follow(const Found& project);
  HkxChainLink resolve(std::string role, const std::string& declared);
  std::optional<std::vector<std::byte>> bytes_of(const HkxChainLink& link, std::string& error);
  void add_link(HkxChainLink link);
  void read_character(const HkxChainLink& character);
  void note_skeleton(HkxChainLink& link);
  void resolve_animation(const std::string& declared);
};

HkxChainLink Chain::resolve(std::string role, const std::string& declared) {
  return resolve_declared(assets_, root_, std::move(role), declared);
}

std::optional<std::vector<std::byte>> Chain::bytes_of(const HkxChainLink& link, std::string& error) {
  const auto found = lookup(assets_, link.resolved, error);
  if (!found) return std::nullopt;
  return read_found(assets_, *found, error);
}

void Chain::add_link(HkxChainLink link) {
  if (link.blocked) {
    chain_.problems.push_back(link.role + " path is outside the allowed folders: " + link.declared);
  } else if (!link.exists) {
    chain_.problems.push_back(link.role + " file is missing: " + link.declared);
  }
  chain_.links.push_back(std::move(link));
}

void Chain::note_skeleton(HkxChainLink& link) {
  std::string error;
  const auto bytes = bytes_of(link, error);
  if (!bytes) {
    link.note = "could not read: " + error;
    return;
  }
  HkxPackfile packfile;
  HkxSkeleton skeleton;
  auto status = read_packfile(*bytes, packfile);
  if (status == HkxStatus::Ok) status = read_skeleton(packfile, skeleton);
  if (status != HkxStatus::Ok) {
    link.note = "could not read: " + std::string(status_text(status));
    return;
  }
  link.note = std::to_string(skeleton.bone_names.size()) + " bones";
}

void Chain::resolve_animation(const std::string& declared) {
  HkxChainLink link = resolve("animation", declared);
  if (link.blocked) {
    chain_.problems.push_back("animation path is outside the allowed folders: " + declared);
  } else if (!link.exists) {
    const std::string lender = borrowed_from(declared);
    chain_.problems.push_back(lender.empty() ? "missing animation: " + declared + ". It is not loose under " +
                                                   chain_.root + " nor inside any archive."
                                             : "missing animation, borrowed from " + lender + ": " + declared +
                                                   ". Extract " + lender + " alongside this character and it resolves.");
  }
  chain_.animations.push_back(std::move(link));
}

void Chain::read_character(const HkxChainLink& character) {
  std::string error;
  const auto bytes = bytes_of(character, error);
  if (!bytes) {
    chain_.problems.push_back("could not read the character file: " + error);
    return;
  }
  HkxPackfile packfile;
  HkxCharacterLinks links;
  auto status = read_packfile(*bytes, packfile);
  if (status == HkxStatus::Ok) status = read_character_links(packfile, links);
  if (status == HkxStatus::MissingObject) {
    chain_.problems.push_back("the character file has no hkbCharacterStringData");
    return;
  }
  if (status != HkxStatus::Ok) {
    chain_.problems.push_back("could not read the character file: " + std::string(status_text(status)));
    return;
  }

  if (links.behavior.empty()) {
    chain_.problems.push_back("the character names no behaviour file");
  } else {
    add_link(resolve("behaviour", links.behavior));
  }
  if (links.rig.empty()) {
    chain_.problems.push_back("the character names no skeleton");
  } else {
    HkxChainLink rig = resolve("skeleton", links.rig);
    if (rig.exists) note_skeleton(rig);
    add_link(std::move(rig));
  }
  if (links.animations.empty()) chain_.warnings.push_back("the character declares no animations");
  if (links.animations.size() > k_max_declared_animations) {
    chain_.problems.push_back("the character declares " + std::to_string(links.animations.size()) +
                              " animations; only the first " + std::to_string(k_max_declared_animations) + " are resolved");
    links.animations.resize(k_max_declared_animations);
  }
  for (const std::string& animation : links.animations) resolve_animation(animation);
}

std::optional<Found> Chain::find_project(const std::string& input, const std::vector<std::string>& directory) {
  std::string error;
  const auto given = lookup(assets_, input, error);
  if (!given) {
    chain_.problems.push_back(error.empty() ? "the file is missing: " + input : "could not look up " + input + ": " + error);
    return std::nullopt;
  }
  std::string refusal;
  if (const auto bytes = read_found(assets_, *given, error); bytes && holds_project_data(*bytes, refusal)) {
    root_ = directory;
    chain_.root = join_parts(root_);
    return given;
  } else if (!bytes) {
    chain_.warnings.push_back("could not read " + input + ": " + error);
  } else if (!refusal.empty()) {
    chain_.warnings.push_back("could not read " + input + " as a packfile: " + refusal);
  }

  root_ = is_project_subfolder(leaf_of(directory)) && directory.size() > 1
              ? std::vector<std::string>(directory.begin(), directory.end() - 1)
              : directory;
  const std::string prefix = join_parts(root_);
  chain_.root = prefix;
  std::vector<Found> candidates;
  try {
    const auto listing = assets_.list_report(prefix, ".hkx", false);
    if (listing.incomplete()) chain_.problems.push_back("the listing of " + prefix + " is incomplete: " + nameless_text(listing));
    for (const archive::AssetListing& entry : listing.entries) {
      if (entry.path.size() <= prefix.size() + 1 || entry.path.find('/', prefix.size() + 1) != std::string::npos) continue;
      std::string entry_error;
      const auto found = lookup(assets_, entry.path, entry_error);
      const auto bytes = found ? read_found(assets_, *found, entry_error) : std::nullopt;
      if (!bytes) {
        chain_.problems.push_back("could not read " + entry.path + ": " + entry_error);
        continue;
      }
      if (holds_project_data(*bytes, refusal)) {
        candidates.push_back(*found);
      } else if (!refusal.empty()) {
        chain_.warnings.push_back("could not read " + entry.path + " as a packfile: " + refusal);
      }
    }
  } catch (const std::exception& ex) {
    chain_.problems.push_back("could not list " + prefix + ": " + ex.what());
    return std::nullopt;
  }
  if (candidates.empty()) {
    chain_.problems.push_back("no project .hkx containing hkbProjectStringData directly under " + prefix);
    return std::nullopt;
  }
  if (candidates.size() > 1) {
    std::string names;
    for (const Found& candidate : candidates) {
      if (!names.empty()) names += ", ";
      names += candidate.path.substr(prefix.size() + 1);
    }
    chain_.problems.push_back("ambiguous project files containing hkbProjectStringData: " + names);
    return std::nullopt;
  }
  return candidates.front();
}

void Chain::follow(const Found& project) {
  HkxChainLink link;
  link.role = "project";
  link.declared = project.path.substr(project.path.rfind('/') + 1);
  link.resolved = project.path;
  link.source = project.source;
  link.exists = true;
  chain_.links.push_back(std::move(link));

  std::string error;
  const auto bytes = read_found(assets_, project, error);
  if (!bytes) {
    chain_.problems.push_back("could not read the project file: " + error);
    return;
  }
  HkxPackfile packfile;
  std::vector<std::string> characters;
  auto status = read_packfile(*bytes, packfile);
  if (status == HkxStatus::Ok) status = read_project_characters(packfile, characters);
  if (status != HkxStatus::Ok) {
    chain_.problems.push_back("could not read the project file: " + std::string(status_text(status)));
    return;
  }
  if (characters.empty()) {
    chain_.problems.push_back("the project names no character file");
    return;
  }
  if (characters.size() > 1) {
    chain_.warnings.push_back("the project names " + std::to_string(characters.size()) +
                              " characters; only the first, " + characters.front() + ", is followed");
  }
  HkxChainLink character = resolve("character", characters.front());
  const bool readable = character.exists;
  add_link(character);
  if (readable) read_character(character);
}

void Chain::run(std::string_view any_project_hkx) {
  auto parts = split_path(any_project_hkx);
  if (parts && !parts->empty() && parts->front() == "data") parts->erase(parts->begin());
  const bool usable = parts && parts->size() >= 2 && parts->front() == "meshes" &&
                      std::find(parts->begin(), parts->end(), "..") == parts->end();
  if (!usable) {
    chain_.problems.push_back("the project path must be a data-relative .hkx path below meshes: " +
                              std::string(any_project_hkx));
    return;
  }
  const std::vector<std::string> directory(parts->begin(), parts->end() - 1);
  const auto project = find_project(join_parts(*parts), directory);
  if (project) follow(*project);
}

}  // namespace

HkxStatus read_project_characters(const HkxPackfile& packfile, std::vector<std::string>& out) {
  out.clear();
  Located located;
  if (const auto status = locate_object(packfile, "hkbProjectStringData", located); status != HkxStatus::Ok) return status;
  const auto field = member_at(located, "characterFilenames");
  if (!field) return HkxStatus::UnsupportedLayout;
  Budget budget{located.section->data.size() * k_budget_factor + 4096};
  std::vector<std::string> names;
  if (const auto status = string_array_at(*located.section, *field, budget, names); status != HkxStatus::Ok) return status;
  out = std::move(names);
  return HkxStatus::Ok;
}

HkxStatus read_character_links(const HkxPackfile& packfile, HkxCharacterLinks& out) {
  out = {};
  Located located;
  if (const auto status = locate_object(packfile, "hkbCharacterStringData", located); status != HkxStatus::Ok) return status;
  const HkxClassLayout* bundle = find_layout("hkbAssetBundleStringData");
  if (bundle == nullptr) return HkxStatus::UnsupportedLayout;
  const auto bundle_names = find_member_offset(*bundle, "assetNames");
  const auto rig = member_at(located, "rigName");
  const auto behavior = member_at(located, "behaviorFilename");
  const auto bundles = member_at(located, "animationBundleNameData");
  if (!bundle_names || !rig || !behavior || !bundles) return HkxStatus::UnsupportedLayout;

  const HkxSection& section = *located.section;
  Budget budget{section.data.size() * k_budget_factor + 4096};
  HkxCharacterLinks links;
  if (const auto status = string_at(section, *rig, budget, links.rig); status != HkxStatus::Ok) return status;
  if (const auto status = string_at(section, *behavior, budget, links.behavior); status != HkxStatus::Ok) return status;

  Elements elements;
  if (const auto status = elements_at(section, *bundles, k_pointer_size, bundle->size, elements); status != HkxStatus::Ok) {
    return status;
  }
  std::set<std::string> seen;
  for (std::size_t i = 0; i < elements.count; ++i) {
    std::vector<std::string> names;
    const std::size_t at = elements.first + i * bundle->size + *bundle_names;
    if (const auto status = string_array_at(section, at, budget, names); status != HkxStatus::Ok) return status;
    for (std::string& name : names) {
      if (seen.insert(name).second) links.animations.push_back(std::move(name));
    }
  }
  out = std::move(links);
  return HkxStatus::Ok;
}

std::string borrowed_from(std::string_view declared) {
  std::vector<std::string> parts;
  std::string current;
  for (const char ch : declared) {
    if (ch == '/' || ch == '\\') {
      if (!current.empty()) parts.push_back(current);
      current.clear();
    } else {
      current.push_back(ch);
    }
  }
  if (!current.empty()) parts.push_back(current);
  for (std::size_t i = parts.size(); i-- > 0;) {
    if (parts[i] == "..") return i + 1 < parts.size() ? parts[i + 1] : std::string{};
  }
  return {};
}

HkxProjectChain resolve_project_chain(const archive::AssetResolver& assets, std::string_view any_project_hkx) {
  HkxProjectChain chain;
  Chain(assets, chain).run(any_project_hkx);
  return chain;
}

namespace {

void add_finding(HkxProjectCheck& check, const std::string& file, std::string level, std::string code, std::string message) {
  check.findings.push_back({file, std::move(level), std::move(code), std::move(message)});
}

void check_graph(HkxProjectCheck& check, const std::string& path, const HkxGraphModel& model) {
  for (const std::string& line : model.diagnostics) add_finding(check, path, "warning", "graph.read_diagnostic", line);
  for (const HkxGraphIssue& issue : validate_behavior_model(model).issues) {
    std::string message = issue.message;
    if (!issue.table.empty()) message = issue.table + "[" + std::to_string(issue.index) + "]: " + message;
    add_finding(check, path, issue.severity == HkxGraphIssueSeverity::Error ? "error" : "warning", issue.code, std::move(message));
  }
}

void check_clip_animations(HkxProjectCheck& check, const archive::AssetResolver& assets, const HkxProjectChain& chain,
                           const std::vector<std::string>& root, const std::string& path, const HkxGraphModel& model) {
  std::set<std::string> declared;
  for (const HkxChainLink& animation : chain.animations) declared.insert(animation_key(animation.declared));
  std::set<std::string> seen;
  for (const HkxGraphClip& clip : model.clips) {
    if (clip.animation.empty() || !seen.insert(animation_key(clip.animation)).second) continue;
    const HkxChainLink link = resolve_declared(assets, root, "animation", clip.animation);
    if (link.blocked) {
      add_finding(check, path, "error", "chain.clip_animation_blocked", "clip animation is outside the allowed folders: " + clip.animation);
    } else if (!link.exists) {
      add_finding(check, path, "error", "chain.clip_animation_absent",
                  "clip animation is not loose under " + chain.root + " nor inside any archive: " + clip.animation);
    } else if (!declared.empty() && declared.count(animation_key(clip.animation)) == 0) {
      add_finding(check, path, "warning", "chain.clip_animation_undeclared",
                  "clip animation exists but the character does not declare it: " + clip.animation);
    }
  }
}

std::string hex8(std::uint32_t value) {
  char text[16];
  std::snprintf(text, sizeof text, "%08x", static_cast<unsigned>(value));
  return text;
}

// Constraint: only a file this build can read is checked, so an unreadable file is never also reported as a loss.
// Invariant: the returned text is a repack refusal, which is informational and never a loss; it is empty when the writer did not refuse.
std::string add_round_trip_losses(const HkxPackfile& packfile, std::span<const std::byte> bytes, std::vector<HkxRoundTripLoss>& out) {
  std::set<std::pair<std::uint32_t, std::string>> seen;
  for (const HkxObjectView& object : packfile.objects) {
    const std::string name(object.class_name);
    if (!seen.emplace(object.signature, name).second) continue;
    const HkxClassLayout* layout = find_layout(name);
    if (layout == nullptr) {
      out.push_back({"UnsupportedClass", name + " is a class this build has no definition for"});
    } else if (layout->signature != object.signature) {
      out.push_back({"UnsupportedClass", name + " is signed 0x" + hex8(object.signature) + " in the file and 0x" + hex8(layout->signature) + " here"});
    }
  }
  const HkxRepackReport repack = repack_check(bytes);
  const std::string status(status_text(repack.status));
  switch (repack.verdict) {
    case HkxRepackVerdict::Equal:
    case HkxRepackVerdict::ReadFailed: break;
    case HkxRepackVerdict::Refused: return "the writer cannot lay this file out again: " + status;
    case HkxRepackVerdict::RewriteFailed: out.push_back({"RewriteFailed", "laying this file out again failed: " + status}); break;
    case HkxRepackVerdict::RereadFailed: out.push_back({"RereadFailed", "the file laid out again cannot be read: " + status}); break;
    case HkxRepackVerdict::Differs: out.push_back({"RepackDiffers", "laying this file out again changes it: " + repack.difference}); break;
  }
  return {};
}

void check_behavior_file(HkxProjectCheck& check, HkxFileCheck& file, const archive::AssetResolver& assets,
                         const HkxProjectChain& chain, const std::vector<std::string>& root) {
  const std::string& path = file.path;
  std::string error;
  const auto found = lookup(assets, path, error);
  const auto bytes = found ? read_found(assets, *found, error) : std::nullopt;
  if (!bytes) {
    file.error = "could not read: " + (error.empty() ? std::string("the file is missing") : error);
    add_finding(check, path, "error", "file.unreadable", file.error);
    return;
  }
  HkxPackfile packfile;
  if (const auto status = read_packfile(*bytes, packfile); status != HkxStatus::Ok) {
    std::string reason = describe_packfile_refusal(*bytes);
    if (reason.empty()) reason = std::string(status_text(status));
    file.error = "cannot be read as a packfile: " + reason;
    add_finding(check, path, "error", "file.not_packfile", file.error);
    return;
  }
  ++check.read;
  if (const std::string refused = add_round_trip_losses(packfile, *bytes, file.round_trip); !refused.empty()) {
    add_finding(check, path, "warning", "repack.refused", refused);
  }
  HkxGraphModel model;
  const auto status = read_behavior_model(packfile, model);
  if (status == HkxStatus::MissingObject) {
    add_finding(check, path, "warning", "graph.absent", "holds no hkbBehaviorGraph");
  } else if (status != HkxStatus::Ok) {
    file.error = "the behaviour graph was refused: " + std::string(status_text(status));
    add_finding(check, path, "error", "graph.refused", file.error);
  } else {
    check_graph(check, path, model);
    check_clip_animations(check, assets, chain, root, path, model);
  }
}

}  // namespace

HkxProjectCheck check_project_behaviors(const archive::AssetResolver& assets, const HkxProjectChain& chain,
                                        std::size_t file_limit) {
  HkxProjectCheck check;
  if (chain.root.empty()) return check;
  const std::vector<std::string> root = split_path(chain.root).value_or(std::vector<std::string>{});

  std::vector<std::string> files;
  for (const HkxChainLink& link : chain.links) {
    if (link.role == "behaviour" && link.exists) files.push_back(link.resolved);
  }
  const std::string folder = chain.root + "/behaviors";
  try {
    const auto listing = assets.list_report(folder, ".hkx", false);
    if (listing.incomplete()) {
      add_finding(check, folder, "error", "check.listing_incomplete", "the listing is incomplete: " + nameless_text(listing));
    }
    for (const archive::AssetListing& entry : listing.entries) {
      if (std::find(files.begin(), files.end(), entry.path) == files.end()) files.push_back(entry.path);
    }
  } catch (const std::exception& ex) {
    add_finding(check, folder, "error", "check.listing_failed", std::string("could not list the behaviour folder: ") + ex.what());
  }

  for (const std::string& path : files) {
    if (check.files >= file_limit) {
      check.truncated = true;
      break;
    }
    ++check.files;
    HkxFileCheck file;
    file.path = path;
    file.name = path.substr(path.rfind('/') == std::string::npos ? 0 : path.rfind('/') + 1);
    file.first_finding = check.findings.size();
    check_behavior_file(check, file, assets, chain, root);
    file.finding_count = check.findings.size() - file.first_finding;
    if (file.error.empty()) {
      for (std::size_t i = file.first_finding; i < check.findings.size(); ++i) {
        (check.findings[i].level == "error" ? file.errors : file.warnings) += 1;
      }
    }
    check.per_file.push_back(std::move(file));
  }
  return check;
}

std::size_t project_finding_count(const HkxProjectCheck& check) {
  return check.per_file.empty() ? check.findings.size() : check.per_file.front().first_finding;
}

std::size_t unreadable_files(const HkxProjectCheck& check) {
  return static_cast<std::size_t>(std::count_if(check.per_file.begin(), check.per_file.end(),
                                                [](const HkxFileCheck& file) { return !file.error.empty(); }));
}

std::size_t round_trip_losses(const HkxProjectCheck& check) {
  std::size_t total = 0;
  for (const HkxFileCheck& file : check.per_file) total += file.round_trip.size();
  return total;
}

std::size_t files_with_round_trip_losses(const HkxProjectCheck& check) {
  return static_cast<std::size_t>(std::count_if(check.per_file.begin(), check.per_file.end(),
                                                [](const HkxFileCheck& file) { return !file.round_trip.empty(); }));
}

namespace {

std::string counted(std::size_t count, std::string_view word) {
  return std::to_string(count) + " " + std::string(word) + (count == 1 ? "" : "s");
}

}  // namespace

std::string describe_project_check(const HkxProjectCheck& check) {
  if (check.per_file.empty()) return "no behaviour files were found in this project";
  std::size_t errors = 0;
  std::size_t warnings = 0;
  for (const HkxFileCheck& file : check.per_file) {
    errors += file.errors;
    warnings += file.warnings;
  }
  const std::size_t unreadable = unreadable_files(check);
  std::string text = std::to_string(check.per_file.size() - unreadable) + " of " + std::to_string(check.per_file.size()) +
                     " behaviour files read: " + counted(errors, "error") + ", " + counted(warnings, "warning");
  if (const std::size_t losses = round_trip_losses(check); losses > 0) {
    text += ", " + counted(losses, "round-trip issue") + " in " + counted(files_with_round_trip_losses(check), "file");
  }
  if (unreadable > 0) text += ", " + std::to_string(unreadable) + " could not be unpacked";
  return text;
}

namespace {

struct ReportRow {
  std::string path;
  std::string name;
  std::string status;
  std::string severity;
  std::string where;
  std::string message;
  std::string blocks_save;
};

// Invariant: an unreadable file shows only its error, then its losses, then its findings; a file with none of them is one clean row.
std::vector<ReportRow> report_rows(const HkxProjectCheck& check) {
  std::vector<ReportRow> rows;
  const auto add_findings = [&](const std::string& path, const std::string& name, std::size_t first, std::size_t count,
                                const char* status) {
    for (std::size_t i = first; i < first + count; ++i) {
      const HkxCheckFinding& finding = check.findings[i];
      rows.push_back({path, name, status, finding.level, finding.code, finding.message, ""});
    }
  };
  for (const HkxFileCheck& file : check.per_file) {
    const std::size_t before = rows.size();
    if (!file.error.empty()) rows.push_back({file.path, file.name, "unreadable", "error", "", file.error, ""});
    for (const HkxRoundTripLoss& loss : file.round_trip) {
      rows.push_back({file.path, file.name, "round-trip-loss", "error", "", loss.message, "true"});
    }
    if (file.error.empty()) add_findings(file.path, file.name, file.first_finding, file.finding_count, "finding");
    if (rows.size() == before) rows.push_back({file.path, file.name, "clean", "", "", "", ""});
  }
  for (std::size_t i = 0; i < project_finding_count(check); ++i) {
    add_findings(check.findings[i].file, check.findings[i].file, i, 1, "project-finding");
  }
  return rows;
}

std::string csv_cell(std::string_view text) {
  std::string value;
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '\r') {
      if (i + 1 < text.size() && text[i + 1] == '\n') ++i;
      value.push_back('\n');
    } else {
      value.push_back(text[i]);
    }
  }
  std::size_t lead = 0;
  while (lead < value.size() && (value[lead] == ' ' || value[lead] == '\t')) ++lead;
  const bool formula = !value.empty() && (value[0] == '\t' || (lead < value.size() && std::string_view("=+-@").find(value[lead]) != std::string_view::npos));
  const auto blank = [](char ch) { return std::isspace(static_cast<unsigned char>(ch)) != 0; };
  const bool padded = !value.empty() && (blank(value.front()) || blank(value.back()));
  const std::string cell = formula ? "'" + value : value;
  if (!padded && cell.find_first_of(",\"\n") == std::string::npos) return cell;
  std::string quoted = "\"";
  for (const char ch : cell) {
    if (ch == '"') quoted.push_back('"');
    quoted.push_back(ch);
  }
  quoted.push_back('"');
  return quoted;
}

void csv_line(std::string& out, const std::vector<std::string_view>& cells) {
  for (std::size_t i = 0; i < cells.size(); ++i) {
    if (i > 0) out.push_back(',');
    out += csv_cell(cells[i]);
  }
  out.push_back('\n');
}

std::string html_text(std::string_view text) {
  std::string out;
  for (const char ch : text) {
    switch (ch) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      case '\'': out += "&#39;"; break;
      default: out.push_back(ch);
    }
  }
  return out;
}

std::string html_card(std::string_view label, std::size_t value) {
  return "<div class=\"card\"><span>" + html_text(label) + "</span><strong>" + std::to_string(value) + "</strong></div>";
}

constexpr std::string_view k_html_head =
    "<!doctype html>\n<html lang=\"en\">\n<head>\n  <meta charset=\"utf-8\">\n"
    "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
    "  <title>Behaviour Graph Studio project report</title>\n  <style>\n"
    "    :root { color-scheme: light dark; font-family: system-ui, sans-serif; }\n"
    "    body { max-width: 1400px; margin: 2rem auto; padding: 0 1rem; }\n"
    "    h1 { margin-bottom: .25rem; }\n"
    "    .summary { display: flex; flex-wrap: wrap; gap: .75rem; margin: 1.25rem 0; }\n"
    "    .card { border: 1px solid #7777; border-radius: .5rem; padding: .75rem 1rem; min-width: 8rem; }\n"
    "    .card strong { display: block; font-size: 1.5rem; }\n"
    "    table { width: 100%; border-collapse: collapse; font-size: .9rem; }\n"
    "    th, td { border-bottom: 1px solid #7775; padding: .5rem; text-align: left; vertical-align: top; }\n"
    "    th { position: sticky; top: 0; background: Canvas; }\n"
    "    .error { font-weight: 700; }\n"
    "    code { white-space: pre-wrap; overflow-wrap: anywhere; }\n"
    "  </style>\n</head>\n<body>";

}  // namespace

std::string render_project_report(const HkxProjectChain& chain, const HkxProjectCheck& check, HkxReportFormat format) {
  const std::vector<ReportRow> rows = report_rows(check);
  std::string out;
  if (format == HkxReportFormat::Csv) {
    csv_line(out, {"project", "file", "path", "status", "severity", "object_id", "where", "message", "blocks_save"});
    for (const ReportRow& row : rows) {
      csv_line(out, {chain.root, row.name, row.path, row.status, row.severity, "", row.where, row.message, row.blocks_save});
    }
    return out;
  }

  const std::size_t unreadable = unreadable_files(check);
  std::size_t errors = 0;
  std::size_t warnings = 0;
  for (const HkxFileCheck& file : check.per_file) {
    errors += file.errors;
    warnings += file.warnings;
  }
  out += k_html_head;
  out += "<h1>Behaviour Graph Studio project report</h1>\n";
  out += "<p><strong>Project:</strong> <code>" + html_text(chain.root) + "</code></p>\n";
  out += "<p>" + html_text(describe_project_check(check)) + "</p>\n";
  if (check.truncated) out += "<p>Stopped at the file limit, so this report is incomplete.</p>\n";
  out += "<div class=\"summary\">" + html_card("Files", check.per_file.size()) +
         html_card("Readable", check.per_file.size() - unreadable) + html_card("Unreadable", unreadable) +
         html_card("Errors", errors) + html_card("Warnings", warnings) +
         html_card("Round-trip issues", round_trip_losses(check)) + "</div>\n";
  out += "<table><thead><tr><th>File</th><th>Status</th><th>Severity</th><th>Object</th><th>Where</th><th>Message</th>"
         "<th>Blocks save</th></tr></thead><tbody>\n";
  for (const ReportRow& row : rows) {
    out += row.severity == "error" ? "<tr class=\"error\">" : "<tr>";
    out += "<td><strong>" + html_text(row.name) + "</strong><br><code>" + html_text(row.path) + "</code></td><td>" +
           html_text(row.status) + "</td><td>" + html_text(row.severity) + "</td><td></td><td><code>" +
           html_text(row.where) + "</code></td><td>" + html_text(row.message) + "</td><td>" +
           (row.blocks_save == "true" ? "yes" : "") + "</td></tr>\n";
  }
  out += "</tbody></table>\n</body>\n</html>\n";
  return out;
}

}  // namespace fo4::hkx
