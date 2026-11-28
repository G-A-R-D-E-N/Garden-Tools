#pragma once

#include "fo4/archive/assets.hpp"
#include "fo4/hkx/packfile.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::hkx {

// Requires an x64 packfile; the first hkbProjectStringData supplies characterFilenames, and a file with none comes back as MissingObject.
HkxStatus read_project_characters(const HkxPackfile& packfile, std::vector<std::string>& out);

struct HkxCharacterLinks {
  std::string behavior;
  std::string rig;
  // Constraint: the flattened assetNames of animationBundleNameData, de-duplicated in file order; the class has no animationNames member in Fallout 4.
  std::vector<std::string> animations;
};

// Requires an x64 packfile; a file with no hkbCharacterStringData comes back as MissingObject, and a class with no layout for the file's signature as UnsupportedLayout.
HkxStatus read_character_links(const HkxPackfile& packfile, HkxCharacterLinks& out);

struct HkxChainLink {
  std::string role;
  std::string declared;
  // Invariant: resolved is empty for a blocked link, which is never looked up.
  std::string resolved;
  // Invariant: source is "loose", an archive file name, or empty when the link is absent.
  std::string source;
  bool exists{false};
  bool blocked{false};
  std::string note;
};

struct HkxProjectChain {
  std::string root;
  std::vector<HkxChainLink> links;
  std::vector<HkxChainLink> animations;
  std::vector<std::string> problems;
  // Invariant: a warning never makes a chain fail; a problem always does.
  std::vector<std::string> warnings;
};

// Constraint: at most this many declared animations are resolved; a character that declares more is cut and reports a problem.
inline constexpr std::size_t k_max_declared_animations = 10000;

// Constraint: paths are data-relative and must stay below meshes; a declared link that leaves it is blocked and never opened, and `..` is collapsed before the resolver sees the path.
HkxProjectChain resolve_project_chain(const archive::AssetResolver& assets, std::string_view any_project_hkx);

// Constraint: the component after the last `..` of a declared path names the project it borrows from; empty when the path borrows nothing.
std::string borrowed_from(std::string_view declared);

struct HkxCheckFinding {
  std::string file;
  // Invariant: level is "error" or "warning".
  std::string level;
  // Invariant: code is a stable dotted name a consumer may switch on; graph rules keep the code validate_behavior_model gave them.
  std::string code;
  std::string message;
};

// Invariant: kind is UnsupportedClass (the BGS kind) or RewriteFailed, RereadFailed, RepackDiffers (from repack_check); every loss blocks a native save.
// Invariant: a repack_check refusal is not a loss; it is the warning finding repack.refused, because its rate on vanilla files is unmeasured.
struct HkxRoundTripLoss {
  std::string kind;
  std::string message;
};

struct HkxFileCheck {
  std::string path;
  std::string name;
  // Invariant: empty when the file was read; otherwise the reason it could not be unpacked.
  std::string error;
  // Invariant: errors and warnings count this file's findings and never its round-trip losses.
  std::size_t errors{0};
  std::size_t warnings{0};
  // Invariant: the file's findings are the contiguous range [first_finding, first_finding + finding_count) of HkxProjectCheck::findings, which holds an unreadable file's own error too.
  std::size_t first_finding{0};
  std::size_t finding_count{0};
  std::vector<HkxRoundTripLoss> round_trip;
};

struct HkxProjectCheck {
  std::size_t files{0};
  std::size_t read{0};
  std::vector<HkxCheckFinding> findings;
  // Invariant: one entry per file counted in `files`, in scan order.
  std::vector<HkxFileCheck> per_file;
  bool truncated{false};
};

// Invariant: a scan that stops at `file_limit` says so through `truncated`, so a short list never reads as a small project.
// Constraint: runs validate_behavior_model on every behaviour graph, checks each clip animation against the project and reports round-trip losses per file without writing anything.
HkxProjectCheck check_project_behaviors(const archive::AssetResolver& assets, const HkxProjectChain& chain,
                                        std::size_t file_limit);

// Invariant: the findings before the first file's range belong to no file (a behaviours listing that failed or was cut short).
std::size_t project_finding_count(const HkxProjectCheck& check);
std::size_t unreadable_files(const HkxProjectCheck& check);
std::size_t round_trip_losses(const HkxProjectCheck& check);
std::size_t files_with_round_trip_losses(const HkxProjectCheck& check);

// Invariant: the sentence and its plurals are those of BGS ProjectCheck.Result.ToString; errors and warnings are the per-file sums.
std::string describe_project_check(const HkxProjectCheck& check);

enum class HkxReportFormat : std::uint8_t { Csv, Html };

// Constraint: output is LF-terminated text, spreadsheet-formula cells are neutralised and HTML text is encoded; project-level findings follow the file rows.
std::string render_project_report(const HkxProjectChain& chain, const HkxProjectCheck& check, HkxReportFormat format);

}  // namespace fo4::hkx
