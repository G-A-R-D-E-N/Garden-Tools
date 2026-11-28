#pragma once

#include "fo4/plugin/container.hpp"
#include "fo4/plugin_key.hpp"
#include "fo4/schema/ctda.hpp"

#include <cstddef>
#include <expected>
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace fo4 {

struct StripMastersReport {
  std::vector<std::string> targets;
  std::vector<std::string> removed;
  std::vector<std::string> survivors;
  std::vector<std::string> masters_after;
  std::size_t dropped_conditions{0};
  std::size_t touched_records{0};
  bool written{false};
};

struct StripMastersError {
  std::string code;
  std::string message;
};

std::expected<StripMastersReport, StripMastersError> strip_masters_clean(
    const std::filesystem::path& source,
    const std::set<PluginKey>& strip,
    const std::optional<std::filesystem::path>& output);

}  // namespace fo4
