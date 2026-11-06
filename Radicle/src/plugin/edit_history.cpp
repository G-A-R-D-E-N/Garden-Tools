#include "fo4/plugin/edit_history.hpp"

#include "fo4/plugin/visit.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

namespace fo4 {
namespace {

std::size_t snapshot_bytes(const Plugin& plugin) {
  std::size_t bytes = 0;
  visit_records(plugin, [&bytes](const Record& record) {
    for (const auto& subrecord : record.subrecords) bytes += subrecord.data.size();
  });
  return bytes;
}

}  // namespace

PluginEditHistory::PluginEditHistory(std::size_t byte_budget) : byte_budget_(byte_budget) {}

void PluginEditHistory::record(const Plugin& before) {
  redo_.clear();
  const auto bytes = snapshot_bytes(before);
  undo_.push_back(Entry{before, bytes});
  undo_bytes_ += bytes;
  trim_undo();
  ++revision_;
}

std::optional<Plugin> PluginEditHistory::undo(const Plugin& current) {
  if (undo_.empty()) return std::nullopt;
  Entry previous = std::move(undo_.back());
  undo_.pop_back();
  undo_bytes_ -= previous.bytes;
  redo_.push_back(Entry{current, snapshot_bytes(current)});
  ++revision_;
  return std::move(previous.plugin);
}

std::optional<Plugin> PluginEditHistory::redo(const Plugin& current) {
  if (redo_.empty()) return std::nullopt;
  Entry next = std::move(redo_.back());
  redo_.pop_back();
  const auto bytes = snapshot_bytes(current);
  undo_.push_back(Entry{current, bytes});
  undo_bytes_ += bytes;
  ++revision_;
  return std::move(next.plugin);
}

std::size_t PluginEditHistory::undo_count() const noexcept { return undo_.size(); }

std::size_t PluginEditHistory::redo_count() const noexcept { return redo_.size(); }

void PluginEditHistory::clear() noexcept {
  undo_.clear();
  redo_.clear();
  undo_bytes_ = 0;
  ++revision_;
}

std::uint64_t PluginEditHistory::revision() const noexcept { return revision_; }

void PluginEditHistory::trim_undo() {
  while (undo_.size() > k_edit_history_max_entries ||
         (undo_.size() > 1 && undo_bytes_ > byte_budget_)) {
    undo_bytes_ -= undo_.front().bytes;
    undo_.pop_front();
  }
}

}  // namespace fo4
