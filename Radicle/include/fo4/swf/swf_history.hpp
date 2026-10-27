#pragma once

#include "fo4/swf/swf.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace fo4::swf {

// Constraint: a command must be replayable from the original bytes, so it stores the plan, not a snapshot.
struct SwfEditCommand {
  SwfTagPlan plan;
  std::uint64_t revision{0};
};

// Invariant: the applied prefix is commands_[:applied_] and the redo tail is the rest; a refused edit moves neither.
class SwfEditHistory {
 public:
  SwfEditHistory() = default;

  // Requires the bytes the editor opened; a failure must leave the history closed and empty.
  bool open(std::span<const std::byte> original_bytes, std::string& message);

  // Requires the plan to use top-level tag positions; success appends a command and drops the redo tail.
  bool apply(const SwfTagPlan& plan, std::string& message);
  bool undo(std::string& message);
  bool redo(std::string& message);

  const SwfFile& file() const noexcept { return file_; }
  std::uint64_t revision() const noexcept { return revision_; }
  std::size_t undo_count() const noexcept { return applied_; }
  std::size_t redo_count() const noexcept { return commands_.size() - applied_; }
  const std::vector<std::byte>& original_bytes() const noexcept { return original_; }
  bool write_bytes(std::vector<std::byte>& out) const;

 private:
  // Constraint: replay always starts from original_bytes so undo cannot depend on the current state.
  bool replay(std::size_t count, std::string& message);

  std::vector<std::byte> original_;
  SwfFile file_;
  std::vector<SwfEditCommand> commands_;
  std::size_t applied_{0};
  std::uint64_t revision_{0};
};

}  // namespace fo4::swf
