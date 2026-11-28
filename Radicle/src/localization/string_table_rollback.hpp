#pragma once

#include <filesystem>
#include <span>
#include <string>

namespace fo4::localization::detail {

struct StringTableRollback {
  std::filesystem::path destination;
  std::filesystem::path backup;
  bool placed{false};
};

std::string rollback_string_tables(std::span<const StringTableRollback> steps);

}
