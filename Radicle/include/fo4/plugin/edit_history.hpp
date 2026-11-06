#pragma once

#include "fo4/plugin/container.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>

namespace fo4 {

inline constexpr std::size_t k_edit_history_max_entries = 50;
inline constexpr std::size_t k_edit_history_byte_budget = std::size_t{256} * 1024 * 1024;

class PluginEditHistory {
public:
  explicit PluginEditHistory(std::size_t byte_budget = k_edit_history_byte_budget);

  void record(const Plugin& before);
  [[nodiscard]] std::optional<Plugin> undo(const Plugin& current);
  [[nodiscard]] std::optional<Plugin> redo(const Plugin& current);

  [[nodiscard]] std::size_t undo_count() const noexcept;
  [[nodiscard]] std::size_t redo_count() const noexcept;
  void clear() noexcept;
  [[nodiscard]] std::uint64_t revision() const noexcept;

private:
  struct Entry {
    Plugin plugin;
    std::size_t bytes{0};
  };

  void trim_undo();

  std::deque<Entry> undo_;
  std::deque<Entry> redo_;
  std::size_t byte_budget_;
  std::size_t undo_bytes_{0};
  std::uint64_t revision_{0};
};

}  // namespace fo4
