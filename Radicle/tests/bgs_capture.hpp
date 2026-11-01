#pragma once

#include <fo4/avm2/host.hpp>
#include <fo4/avm2/interpreter.hpp>
#include <fo4/avm2/runtime.hpp>
#include <fo4/avm2/value.hpp>
#include <fo4/swf/swf.hpp>

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bgs_capture {

struct Row {
  int seq{0};
  std::size_t frame{0};
  bool to_swf{false};
  std::string method;
  std::vector<fo4::avm2::Value> args;
  fo4::avm2::Value answer;
};

std::vector<Row> parse_capture(std::string_view text);
std::optional<std::string> read_capture_file(const std::filesystem::path& path);

struct Difference {
  int seq{0};
  std::string reason;
};

struct Outcome {
  std::size_t compared{0};
  std::optional<Difference> first;
  std::string replay_error;

  bool matched() const noexcept { return !first.has_value() && replay_error.empty(); }
};

Outcome replay_capture(fo4::avm2::Runtime& runtime, fo4::avm2::Interpreter& interpreter,
                       fo4::avm2::HostCallbacks callbacks, const fo4::swf::SwfFile& file,
                       const std::vector<Row>& rows);

}  // namespace bgs_capture
