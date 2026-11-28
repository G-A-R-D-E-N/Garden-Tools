#include "fo4/plugin/edit_history.hpp"

#include "fo4/plugin/visit.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <utility>
#include <variant>

namespace fo4 {
namespace {

std::size_t record_bytes(const Record& record) {
  std::size_t bytes = sizeof(Record) + record.subrecords.capacity() * sizeof(Subrecord);
  for (const auto& subrecord : record.subrecords) bytes += subrecord.data.capacity();
  return bytes;
}

std::size_t group_bytes(const Group& group) {
  std::size_t bytes = sizeof(Group) + group.children.capacity() * sizeof(GroupChild);
  for (const auto& child : group.children) {
    if (const auto* record = std::get_if<Record>(&child)) bytes += record_bytes(*record);
    else bytes += group_bytes(std::get<Group>(child));
  }
  return bytes;
}

std::size_t snapshot_bytes(const Plugin& plugin) {
  std::size_t bytes = sizeof(Plugin) + plugin.groups.capacity() * sizeof(Group);
  bytes += record_bytes(plugin.tes4) + plugin.tes4_repaired_bytes.capacity();
  for (const auto& group : plugin.groups) bytes += group_bytes(group);
  bytes += plugin.header.author.capacity() + plugin.header.description.capacity();
  return bytes;
}

}  // namespace

PluginEditHistory::PluginEditHistory(std::size_t byte_budget) : byte_budget_(byte_budget) {}

bool PluginEditHistory::bound_to(const PluginKey& key) const noexcept {
  return !plugin_.has_value() || *plugin_ == key;
}

void PluginEditHistory::record(const Plugin& before) {
  if (!bound_to(before.key)) {
    throw std::invalid_argument("edit history belongs to a different plugin");
  }
  plugin_ = before.key;
  const auto bytes = snapshot_bytes(before);
  undo_.push_back(Entry{before, bytes});
  redo_.clear();
  redo_bytes_ = 0;
  undo_bytes_ += bytes;
  trim_undo();
  ++revision_;
}

std::optional<Plugin> PluginEditHistory::undo(const Plugin& current) {
  if (!bound_to(current.key)) {
    throw std::invalid_argument("edit history belongs to a different plugin");
  }
  if (undo_.empty()) return std::nullopt;
  const auto bytes = snapshot_bytes(current);
  redo_.push_back(Entry{current, bytes});
  redo_bytes_ += bytes;
  Entry previous = std::move(undo_.back());
  undo_.pop_back();
  undo_bytes_ -= previous.bytes;
  ++revision_;
  return std::move(previous.plugin);
}

std::optional<Plugin> PluginEditHistory::redo(const Plugin& current) {
  if (!bound_to(current.key)) {
    throw std::invalid_argument("edit history belongs to a different plugin");
  }
  if (redo_.empty()) return std::nullopt;
  const auto bytes = snapshot_bytes(current);
  undo_.push_back(Entry{current, bytes});
  undo_bytes_ += bytes;
  trim_undo();
  Entry next = std::move(redo_.back());
  redo_.pop_back();
  redo_bytes_ -= next.bytes;
  ++revision_;
  return std::move(next.plugin);
}

std::size_t PluginEditHistory::undo_count() const noexcept { return undo_.size(); }

std::size_t PluginEditHistory::redo_count() const noexcept { return redo_.size(); }
std::size_t PluginEditHistory::undo_bytes() const noexcept { return undo_bytes_; }

void PluginEditHistory::clear() noexcept {
  undo_.clear();
  redo_.clear();
  undo_bytes_ = 0;
  redo_bytes_ = 0;
  plugin_.reset();
  ++revision_;
}

std::uint64_t PluginEditHistory::revision() const noexcept { return revision_; }

void PluginEditHistory::trim_undo() {
  const auto over_budget = [this] {
    return undo_bytes_ + redo_bytes_ > byte_budget_;
  };
  while (undo_.size() > k_edit_history_max_entries ||
         (undo_.size() > 1 && over_budget())) {
    undo_bytes_ -= undo_.front().bytes;
    undo_.pop_front();
  }
  while (redo_.size() > 1 && over_budget()) {
    redo_bytes_ -= redo_.front().bytes;
    redo_.pop_front();
  }
}

}  // namespace fo4
