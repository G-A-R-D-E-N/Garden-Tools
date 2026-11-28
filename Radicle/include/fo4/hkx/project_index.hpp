#pragma once

#include "fo4/hkx/packfile.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::hkx {

struct HkxProjectFile {
  std::string name;
  bool read_ok{false};
  std::string error;
  std::size_t object_count{0};
  std::size_t behavior_objects{0};
  std::size_t animation_objects{0};
  std::size_t skeleton_objects{0};
  // Constraint: a file with no behavior objects keeps this at zero, so it never groups as a duplicate graph.
  std::uint64_t behavior_hash{0};
  std::vector<std::string> classes;
};

struct HkxProjectIndex {
  std::size_t file_count{0};
  std::size_t read_count{0};
  std::vector<HkxProjectFile> files;
  std::vector<HkxProjectFile> unreadable;
  std::vector<std::string> diagnostics;
};

// Invariant: a file that does not parse is kept with its error, so a scan never drops a file silently.
HkxProjectFile index_project_file(std::string_view name, std::span<const std::byte> bytes);

// Constraint: the query matches the file name or any class name it holds, ignoring case.
std::vector<std::size_t> search_project_index(const HkxProjectIndex& index, std::string_view query);

// Requires at least two files with the same non-zero behavior hash to form a group.
std::vector<std::vector<std::size_t>> duplicate_behavior_groups(const HkxProjectIndex& index);

}  // namespace fo4::hkx
