#pragma once

#include "fo4/plugin/container.hpp"

#include <cstddef>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fo4 {

struct SemanticFieldCell {
  std::string path;
  std::string value;
  bool available{false};
  bool reference{false};
  bool writable{true};
};

struct SemanticFieldAddress {
  std::string_view path;
  std::size_t index{0};
};

struct SemanticFieldError {
  std::string code;
  std::string message;
};

std::optional<SemanticFieldAddress> semantic_field_address(std::string_view path);

std::expected<std::vector<SemanticFieldCell>, SemanticFieldError> semantic_field_cells(
    const Plugin& plugin, const Record& record);

}  // namespace fo4
