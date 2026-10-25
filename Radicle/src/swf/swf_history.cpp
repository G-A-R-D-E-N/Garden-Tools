#include "fo4/swf/swf_history.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace fo4::swf {

bool SwfEditHistory::open(std::span<const std::byte> original_bytes, std::string& message) {
  message.clear();
  const auto result = read_swf(original_bytes);
  if (!result.ok()) {
    message = "the original bytes did not decode: ";
    message += status_name(result.status);
    if (!result.message.empty()) {
      message += " ";
      message += result.message;
    }
    original_.clear();
    file_ = {};
    commands_.clear();
    applied_ = 0;
    revision_ = 0;
    return false;
  }
  original_.assign(original_bytes.begin(), original_bytes.end());
  file_ = result.file;
  commands_.clear();
  applied_ = 0;
  revision_ = 0;
  return true;
}

bool SwfEditHistory::replay(std::size_t count, std::string& message) {
  if (count > commands_.size()) {
    message = "the history holds fewer commands than the replay asks for";
    return false;
  }
  const auto result = read_swf(original_);
  if (!result.ok()) {
    message = "the original bytes stopped decoding";
    return false;
  }
  SwfFile next = result.file;
  for (std::size_t index = 0; index < count; ++index) {
    SwfTagPlanResult plan_result;
    if (!apply_tag_plan(next, commands_[index].plan, plan_result)) {
      message = "command " + std::to_string(index + 1) + " was refused during replay";
      return false;
    }
  }
  file_ = std::move(next);
  applied_ = count;
  ++revision_;
  if (applied_ != 0) {
    commands_[applied_ - 1].revision = revision_;
  }
  return true;
}

bool SwfEditHistory::apply(const SwfTagPlan& plan, std::string& message) {
  message.clear();
  if (original_.empty()) {
    message = "the history is not open";
    return false;
  }
  {
    SwfFile probe = file_;
    SwfTagPlanResult probe_result;
    if (!apply_tag_plan(probe, plan, probe_result)) {
      message = "the tag plan was refused";
      return false;
    }
    if (!probe_result.changed) {
      return true;
    }
  }
  const std::size_t previous_count = commands_.size();
  const std::size_t previous_applied = applied_;
  const std::uint64_t previous_revision = revision_;
  commands_.resize(applied_);
  SwfEditCommand command;
  command.plan = plan;
  commands_.push_back(std::move(command));
  if (!replay(applied_ + 1, message)) {
    commands_.resize(previous_count);
    applied_ = previous_applied;
    revision_ = previous_revision;
    return false;
  }
  return true;
}

bool SwfEditHistory::undo(std::string& message) {
  message.clear();
  if (original_.empty()) {
    message = "the history is not open";
    return false;
  }
  if (applied_ == 0) {
    message = "there is nothing to undo";
    return false;
  }
  return replay(applied_ - 1, message);
}

bool SwfEditHistory::redo(std::string& message) {
  message.clear();
  if (original_.empty()) {
    message = "the history is not open";
    return false;
  }
  if (applied_ == commands_.size()) {
    message = "there is nothing to redo";
    return false;
  }
  return replay(applied_ + 1, message);
}

bool SwfEditHistory::write_bytes(std::vector<std::byte>& out) const {
  if (original_.empty()) {
    out.clear();
    return false;
  }
  out = write_swf(file_);
  return true;
}

}  // namespace fo4::swf
