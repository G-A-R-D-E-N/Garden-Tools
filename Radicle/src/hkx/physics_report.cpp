#include "fo4/hkx/physics.hpp"

#include <algorithm>
#include <cstdio>
#include <utility>

namespace fo4::hkx {
namespace {

bool ends_with(std::string_view text, std::string_view suffix) {
  return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
}

bool starts_with(std::string_view text, std::string_view prefix) {
  return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

std::string trim_message(const std::string& message) {
  constexpr const char* kNone = "no detail was reported";
  const auto blank = [](char ch) { return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' || ch == '\f' || ch == '\v'; };
  std::size_t start = 0;
  while (start < message.size() && blank(message[start])) ++start;
  if (start == message.size()) return kNone;
  std::size_t line = 0;
  while (line < message.size() && message[line] == '\n') ++line;
  std::size_t end = message.find('\n', line);
  if (end == std::string::npos) end = message.size();
  std::size_t from = line;
  while (from < end && blank(message[from])) ++from;
  while (end > from && blank(message[end - 1])) --end;
  if (from == end) return kNone;
  return message.substr(from, end - from);
}

std::string entry(std::int32_t count, const char* one, const char* many) {
  if (count == 1) return std::string("1 ") + one + " entry";
  return std::to_string(count) + " " + many + " entries";
}

HkxClassCounts legacy_classes(const HkxPhysicsLoadFacts& facts) {
  HkxClassCounts legacy;
  for (const auto& [name, count] : facts.class_counts) {
    if (is_legacy_physics_class(name)) legacy[name] = count;
  }
  return legacy;
}

HkxPhysicsLoadReport build(HkxPhysicsLoadStatus status, const HkxPhysicsLoadFacts& facts,
                           std::vector<HkxPhysicsLoadFinding> findings, HkxPhysicsLoadFinding primary) {
  findings.insert(findings.begin(), std::move(primary));
  HkxPhysicsLoadReport report;
  report.status = status;
  report.findings = std::move(findings);
  report.class_counts = facts.class_counts;
  report.file_version = facts.file_version;
  report.pointer_size = facts.pointer_size;
  if (status == HkxPhysicsLoadStatus::ParseError) report.failing_stage = facts.parse_stage;
  report.body_count = facts.body_count;
  report.usable_bodies = facts.usable_bodies;
  report.skipped_bodies = facts.skipped_bodies;
  report.skipped_constraints = facts.skipped_constraints;
  report.unsupported_shape_bodies = facts.unsupported_shape_bodies;
  report.unsupported_shape_classes = facts.unsupported_shape_classes;
  report.mappings = facts.mappings;
  report.shape_count = facts.shape_count;
  report.constraint_count = facts.constraint_count;
  return report;
}

void add_extra_findings(std::vector<HkxPhysicsLoadFinding>& findings, const HkxPhysicsLoadFacts& facts,
                        HkxPhysicsLoadStatus status, const HkxClassCounts& legacy) {
  using Severity = HkxPhysicsLoadSeverity;
  if (facts.unsupported_shape_bodies > 0 && status != HkxPhysicsLoadStatus::UnsupportedShapes) {
    findings.push_back({Severity::Info,
                        std::to_string(facts.unsupported_shape_bodies) + " of " + std::to_string(facts.body_count) +
                            " bodies use shapes FO4IDE cannot preview yet (" +
                            describe_physics_classes(facts.unsupported_shape_classes) + ").",
                        "Those bodies are still listed with their frames, but they are drawn with no collision hull."});
  }
  if (facts.skipped_bodies > 0 && status != HkxPhysicsLoadStatus::NoBodies) {
    findings.push_back({Severity::Info,
                        entry(facts.skipped_bodies, "body", "bodies") +
                            " could not be read, so those bodies are missing from the preview.",
                        {}});
  }
  if (facts.skipped_constraints > 0 && status != HkxPhysicsLoadStatus::UnsupportedShapes) {
    findings.push_back({Severity::Info,
                        entry(facts.skipped_constraints, "constraint", "constraints") +
                            " could not be read, so those joints are missing from the preview.",
                        {}});
  }
  if (!legacy.empty() && (status == HkxPhysicsLoadStatus::Ok || status == HkxPhysicsLoadStatus::NoBodies)) {
    findings.push_back({Severity::Info,
                        "This file also carries older hkp objects (" + describe_physics_classes(legacy) +
                            "). Fallout 4 loads the hknp ragdoll, so they are ignored.",
                        {}});
  }
}

std::vector<std::string> incomplete_reasons(const HkxPhysicsLoadFacts& facts) {
  std::vector<std::string> reasons;
  if (facts.unsupported_shape_bodies > 0) {
    reasons.push_back(std::to_string(facts.unsupported_shape_bodies) + " of " + std::to_string(facts.body_count) +
                      " bodies use shapes FO4IDE cannot preview yet (" +
                      describe_physics_classes(facts.unsupported_shape_classes) + ")");
  }
  if (facts.skipped_bodies > 0) reasons.push_back(entry(facts.skipped_bodies, "body", "bodies") + " could not be read");
  if (facts.skipped_constraints > 0) {
    reasons.push_back(entry(facts.skipped_constraints, "constraint", "constraints") + " could not be read");
  }
  return reasons;
}

HkxClassCounts filtered(const HkxClassCounts& counts, bool (*keep)(std::string_view)) {
  HkxClassCounts kept;
  for (const auto& [name, count] : counts) {
    if (keep(name)) kept[name] = count;
  }
  return kept;
}

bool is_physics_name(std::string_view name) { return starts_with(name, "hknp") || starts_with(name, "hkp"); }

std::string format_optional(const std::optional<std::int32_t>& value) {
  return value ? std::to_string(*value) : "unknown";
}

bool looks_like_xml(std::span<const std::byte> bytes) {
  const std::size_t limit = std::min<std::size_t>(bytes.size(), 64);
  for (std::size_t at = 0; at < limit; ++at) {
    const auto value = static_cast<unsigned>(bytes[at]);
    if (value != 0x20 && value != 0x09 && value != 0x0A && value != 0x0D && value != 0xEF && value != 0xBB &&
        value != 0xBF) {
      return value == static_cast<unsigned>('<');
    }
  }
  return false;
}

std::uint32_t u32_le(std::span<const std::byte> bytes, std::size_t at) {
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i) value |= static_cast<std::uint32_t>(bytes[at + i]) << (8 * i);
  return value;
}

std::string hex8(std::uint32_t value) {
  char buffer[16];
  std::snprintf(buffer, sizeof buffer, "%08X", static_cast<unsigned>(value));
  return buffer;
}

}  // namespace

std::int32_t HkxPhysicsLoadFacts::count_of(const std::string& class_name) const {
  const auto found = class_counts.find(class_name);
  return found == class_counts.end() ? 0 : found->second;
}

std::int32_t HkxPhysicsLoadReport::count_of(const std::string& class_name) const {
  const auto found = class_counts.find(class_name);
  return found == class_counts.end() ? 0 : found->second;
}

std::string HkxPhysicsLoadReport::status_label() const {
  switch (status) {
    case HkxPhysicsLoadStatus::Ok: return "OK";
    case HkxPhysicsLoadStatus::SkeletonOnly: return "SKELETON ONLY";
    case HkxPhysicsLoadStatus::LegacyPhysics: return "LEGACY PHYSICS";
    case HkxPhysicsLoadStatus::UnsupportedShapes: return "UNSUPPORTED SHAPES";
    case HkxPhysicsLoadStatus::MapperMissing: return "MAPPER MISSING";
    case HkxPhysicsLoadStatus::NoBodies: return "NO BODIES";
    case HkxPhysicsLoadStatus::WrongContainer: return "WRONG CONTAINER";
    case HkxPhysicsLoadStatus::ParseError: return "PARSE ERROR";
  }
  return "UNKNOWN";
}

std::string HkxPhysicsLoadReport::status_slug() const {
  switch (status) {
    case HkxPhysicsLoadStatus::Ok: return "ok";
    case HkxPhysicsLoadStatus::SkeletonOnly: return "skeleton-only";
    case HkxPhysicsLoadStatus::LegacyPhysics: return "legacy-physics";
    case HkxPhysicsLoadStatus::UnsupportedShapes: return "unsupported-shapes";
    case HkxPhysicsLoadStatus::MapperMissing: return "mapper-missing";
    case HkxPhysicsLoadStatus::NoBodies: return "no-bodies";
    case HkxPhysicsLoadStatus::WrongContainer: return "wrong-container";
    case HkxPhysicsLoadStatus::ParseError: return "parse-error";
  }
  return "unknown";
}

HkxPhysicsLoadSeverity HkxPhysicsLoadReport::severity() const {
  return findings.empty() ? HkxPhysicsLoadSeverity::Error : findings.front().severity;
}

std::string HkxPhysicsLoadReport::headline() const {
  return findings.empty() ? "Physics could not be loaded (" + status_label() + ")." : findings.front().message;
}

std::string HkxPhysicsLoadReport::hint() const {
  for (const HkxPhysicsLoadFinding& finding : findings) {
    if (!finding.hint.empty()) return finding.hint;
  }
  return {};
}

std::string HkxPhysicsLoadReport::summary() const {
  const std::string text = hint();
  return text.empty() ? headline() : headline() + " " + text;
}

HkxClassCounts HkxPhysicsLoadReport::shape_classes() const {
  return filtered(class_counts, [](std::string_view name) { return ends_with(name, "Shape"); });
}

HkxClassCounts HkxPhysicsLoadReport::constraint_classes() const {
  return filtered(class_counts, [](std::string_view name) { return ends_with(name, "ConstraintData"); });
}

HkxClassCounts HkxPhysicsLoadReport::physics_classes() const { return physics_classes_of(class_counts); }

std::string HkxPhysicsLoadReport::describe_bodies() const {
  return std::to_string(usable_bodies) + " usable, " + std::to_string(skipped_bodies) + " skipped of " +
         std::to_string(body_count);
}

std::string HkxPhysicsLoadReport::describe_mapper() const {
  if (!mapper_present()) return "absent";
  return mappings > 0 ? "present (" + std::to_string(mappings) + " mappings)" : "present, no usable mappings";
}

std::string HkxPhysicsLoadReport::diagnostics(const std::string& path, std::optional<std::int64_t> file_size,
                                              const std::string& product) const {
  std::string text;
  const auto line = [&text](const std::string& value) {
    text += value;
    text += '\n';
  };
  if (!product.empty()) line(product);
  line("file: " + path);
  line("size: " + (file_size ? std::to_string(*file_size) + " bytes" : std::string("unknown")));
  line("file version: " + format_optional(file_version));
  line("pointer size: " + format_optional(pointer_size));
  line("status: " + status_label() + " (" + status_slug() + ")");
  if (!failing_stage.empty()) line("failing stage: " + failing_stage);
  line("bodies: " + describe_bodies());
  line("shapes: " + std::to_string(shape_count) + " read");
  line("constraints: " + std::to_string(constraint_count) + " read, " + std::to_string(skipped_constraints) +
       " skipped");
  line("ragdoll class: " + std::string(kPhysicsRagdollClass) + " x" +
       std::to_string(count_of(std::string(kPhysicsRagdollClass))));
  line("mapper: " + describe_mapper());
  line(std::string(kPhysicsMaterialClass) + ": " + (material_present() ? "present" : "absent"));
  line("findings:");
  if (findings.empty()) line("  (none)");
  for (const HkxPhysicsLoadFinding& finding : findings) {
    const char* severity_text = finding.severity == HkxPhysicsLoadSeverity::Info      ? "info"
                                : finding.severity == HkxPhysicsLoadSeverity::Warning ? "warning"
                                                                                      : "error";
    line(std::string("  [") + severity_text + "] " + finding.message);
    if (!finding.hint.empty()) line("    next: " + finding.hint);
  }
  line("shape classes: " + describe_physics_classes(shape_classes()));
  line("constraint classes: " + describe_physics_classes(constraint_classes()));
  line("class counts:");
  for (const auto& [name, count] : class_counts) line("  " + name + " x" + std::to_string(count));
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
  return text;
}

std::string describe_physics_classes(const HkxClassCounts& counts) {
  if (counts.empty()) return "none";
  std::vector<std::pair<std::string, std::int32_t>> ordered(counts.begin(), counts.end());
  std::stable_sort(ordered.begin(), ordered.end(),
                   [](const auto& a, const auto& b) { return a.second > b.second; });
  std::string text;
  for (const auto& [name, count] : ordered) {
    if (!text.empty()) text += ", ";
    text += name + " x" + std::to_string(count);
  }
  return text;
}

HkxClassCounts physics_classes_of(const HkxClassCounts& counts) { return filtered(counts, is_physics_name); }

bool is_physics_shape_class(std::string_view class_name) { return ends_with(class_name, "Shape"); }

bool is_physics_constraint_class(std::string_view class_name) { return ends_with(class_name, "ConstraintData"); }

bool is_legacy_physics_class(std::string_view class_name) {
  if (class_name == "hkpPhysicsSystem" || class_name == "hkpPhysicsData") return true;
  if (starts_with(class_name, "hkpRigidBody")) return true;
  return starts_with(class_name, "hkp") && is_physics_shape_class(class_name);
}

std::optional<HkxPhysicsClassFamily> physics_class_family(std::string_view class_name) {
  if (starts_with(class_name, "hcl")) return HkxPhysicsClassFamily::Cloth;
  if (starts_with(class_name, "hknp")) return HkxPhysicsClassFamily::NewPhysics;
  if (starts_with(class_name, "hkp")) return HkxPhysicsClassFamily::LegacyPhysics;
  return std::nullopt;
}

std::string physics_container_problem(std::span<const std::byte> bytes, std::optional<std::int32_t>& file_version,
                                      std::optional<std::int32_t>& pointer_size) {
  file_version.reset();
  pointer_size.reset();
  constexpr std::size_t kHeaderSize = 0x40;
  constexpr std::uint32_t kMagic0 = 0x57E0E057u;
  constexpr std::uint32_t kMagic1 = 0x10C0C010u;

  if (bytes.empty()) return "the file is empty";
  if (bytes.size() < kHeaderSize) {
    return "the file is only " + std::to_string(bytes.size()) + " bytes, too small to hold a packfile header";
  }
  if (looks_like_xml(bytes)) {
    return "the bytes are XML, which is an unpacked tagfile rather than a packed Fallout 4 file";
  }
  if (bytes[4] == std::byte{'T'} && bytes[5] == std::byte{'A'} && bytes[6] == std::byte{'G'} &&
      bytes[7] == std::byte{'0'}) {
    return "the bytes are a binary tagfile, which is not a packed Fallout 4 file";
  }

  const std::uint32_t magic0 = u32_le(bytes, 0);
  const std::uint32_t magic1 = u32_le(bytes, 4);
  if (magic0 != kMagic0 || magic1 != kMagic1) {
    return "the packfile magic does not match (it starts with 0x" + hex8(magic0) + " 0x" + hex8(magic1) + ")";
  }

  const auto version = static_cast<std::int32_t>(u32_le(bytes, 0x0C));
  file_version = version;
  if (version != kFallout4FileVersion) {
    return "it is a version " + std::to_string(version) + " packfile, and Fallout 4 uses version " +
           std::to_string(kFallout4FileVersion);
  }

  const auto pointer = static_cast<std::int32_t>(bytes[0x10]);
  pointer_size = pointer;
  if (pointer != kFallout4PointerSize) {
    return "it uses " + std::to_string(pointer) + "-byte pointers, and Fallout 4 uses " +
           std::to_string(kFallout4PointerSize) + "-byte pointers";
  }
  if (static_cast<int>(bytes[0x11]) != 1) {
    return "it is a big-endian packfile, which only the little-endian layout is read here";
  }
  if (static_cast<int>(bytes[0x12]) != 0 || static_cast<int>(bytes[0x13]) != 1) {
    return "its layout rules are " + std::to_string(static_cast<int>(bytes[0x10])) + " " +
           std::to_string(static_cast<int>(bytes[0x11])) + " " + std::to_string(static_cast<int>(bytes[0x12])) + " " +
           std::to_string(static_cast<int>(bytes[0x13])) + ", which are not the Fallout 4 rules 8 1 0 1";
  }
  return {};
}

HkxPhysicsLoadReport classify_physics_load(const HkxPhysicsLoadFacts& facts) {
  using Severity = HkxPhysicsLoadSeverity;
  using Status = HkxPhysicsLoadStatus;
  const std::string ragdoll_class(kPhysicsRagdollClass);

  std::vector<HkxPhysicsLoadFinding> findings;

  if (!facts.container_notice.empty()) {
    findings.push_back({Severity::Info,
                        "This file's container is unusual for Fallout 4 but still reads: " + facts.container_notice + ".",
                        "Fallout 4 ships packfile version 11 with 8-byte pointers; a file that reads anyway is still "
                        "measured here."});
  }

  if (!facts.container_problem.empty()) {
    return build(Status::WrongContainer, facts, std::move(findings),
                 {Severity::Error, "This file is not a Fallout 4 physics packfile: " + facts.container_problem + ".",
                  "FO4IDE reads the packed format Fallout 4 ships (packfile version 11 with 8-byte pointers). Re-export "
                  "the file with your exporter's Fallout 4 settings."});
  }

  if (!facts.parse_stage.empty()) {
    return build(Status::ParseError, facts, std::move(findings),
                 {Severity::Error,
                  "Reading this file failed at the " + facts.parse_stage + " stage: " + trim_message(facts.parse_message),
                  "The file may be truncated or written in a layout FO4IDE does not describe; re-export it and try "
                  "again."});
  }

  const HkxClassCounts legacy = legacy_classes(facts);
  const bool has_ragdoll = facts.has(ragdoll_class);

  if (!legacy.empty() && !has_ragdoll) {
    return build(Status::LegacyPhysics, facts, std::move(findings),
                 {Severity::Error,
                  "This file holds physics in the older hkp format (" + describe_physics_classes(legacy) +
                      "), not the Fallout 4 format, so the ragdoll in it is ignored. Fallout 4 loads a ragdoll from " +
                      ragdoll_class + " with hknp bodies.",
                  "Fallout 4 needs hknpRagdollData and hknp shapes; re-export with physics in the Fallout 4 format "
                  "rather than the legacy one."});
  }

  if (!has_ragdoll) {
    const HkxClassCounts other_physics = physics_classes_of(facts.class_counts);
    const bool has_skeleton = facts.has(std::string(kPhysicsSkeletonClass));
    const std::string hint =
        !other_physics.empty()
            ? "The file does carry other physics classes (" + describe_physics_classes(other_physics) +
                  "), but a ragdoll still needs hknpRagdollData."
            : "Fallout 4 keeps a ragdoll in the skeleton file itself, so check the sibling skeleton the game loads "
              "with this animation.";
    return build(Status::SkeletonOnly, facts, std::move(findings),
                 {has_skeleton ? Severity::Warning : Severity::Info,
                  has_skeleton ? "This file has a skeleton but no ragdoll. Fallout 4 ragdolls are stored as " +
                                     ragdoll_class + "; re-export with physics included."
                               : "This file holds no skeleton and therefore no ragdoll; Fallout 4 ragdolls are stored "
                                 "as " +
                                     ragdoll_class + " inside a skeleton file.",
                  hint});
  }

  if (facts.usable_bodies == 0) {
    add_extra_findings(findings, facts, Status::NoBodies, legacy);
    return build(Status::NoBodies, facts, std::move(findings),
                 {Severity::Error,
                  facts.body_count == 0
                      ? "This file has a " + ragdoll_class +
                            " ragdoll that declares no rigid bodies at all, so there is nothing to preview."
                      : "This file has a " + ragdoll_class + " ragdoll, but " + std::to_string(facts.skipped_bodies) +
                            " of its " + std::to_string(facts.body_count) +
                            " bodies could not be read, so no rigid body is left to preview.",
                  "Each body needs a readable position and orientation in its hknpBodyCinfo; re-export the ragdoll so "
                  "its body frames are written."});
  }

  if (facts.mappings == 0) {
    const bool mapper_present = facts.has(std::string(kPhysicsMapperClass));
    add_extra_findings(findings, facts, Status::MapperMissing, legacy);
    return build(Status::MapperMissing, facts, std::move(findings),
                 {Severity::Warning,
                  mapper_present ? "An " + std::string(kPhysicsMapperClass) +
                                       " is present but none of its mappings link this ragdoll to the animation "
                                       "skeleton, so the ragdoll cannot follow the animation."
                                 : "This file has a readable ragdoll, but no " + std::string(kPhysicsMapperClass) +
                                       " links the animation skeleton to it, so the ragdoll cannot follow the "
                                       "animation.",
                  "Re-export the skeleton with the ragdoll mapper included (hkaSkeletonMapper)."});
  }

  const std::vector<std::string> incomplete = incomplete_reasons(facts);
  if (!incomplete.empty()) {
    add_extra_findings(findings, facts, Status::UnsupportedShapes, legacy);
    std::string joined;
    for (std::size_t i = 0; i < incomplete.size(); ++i) {
      if (i > 0) joined += "; ";
      joined += incomplete[i];
    }
    return build(Status::UnsupportedShapes, facts, std::move(findings),
                 {Severity::Warning, "The ragdoll is readable but not complete: " + joined + ".",
                  "FO4IDE previews capsule shapes (hknpCapsuleShape) only; the other bodies are still listed with their "
                  "frames, but they are drawn with no collision hull."});
  }

  add_extra_findings(findings, facts, Status::Ok, legacy);
  return build(Status::Ok, facts, std::move(findings),
               {Severity::Info,
                "This file carries a readable ragdoll: " + std::to_string(facts.usable_bodies) + " rigid bodies, " +
                    std::to_string(facts.shape_count) + " shapes and " + std::to_string(facts.constraint_count) +
                    " constraints.",
                {}});
}

}  // namespace fo4::hkx
