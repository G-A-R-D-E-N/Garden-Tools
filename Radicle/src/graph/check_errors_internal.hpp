#pragma once

#include "check_dialogue.hpp"
#include "fo4/graph/check_errors.hpp"
#include "fo4/schema/form_links.hpp"

#include <cstddef>
#include <map>
#include <span>
#include <utility>
#include <vector>

namespace fo4 {

struct WinnerInfo {
  Signature signature{};
  bool deleted{false};
  PluginKey plugin;
};

using WinnerMap = std::map<FormKey, WinnerInfo>;

struct FindingSink {
  std::size_t limit{0};
  std::vector<CheckFinding> findings;
  bool truncated{false};
  PluginKey truncated_plugin;

  [[nodiscard]] bool full() const noexcept { return limit != 0 && findings.size() >= limit; }
  void add(CheckFinding finding) {
    if (full()) {
      truncated = true;
      if (truncated_plugin.filename().empty()) truncated_plugin = finding.plugin;
      return;
    }
    findings.push_back(std::move(finding));
  }
};

void check_links(const Plugin& plugin,
                 const FormLinkCollection& detailed,
                 const std::map<FormKey, const Record*>& records,
                 const WinnerMap& winners,
                 const PluginKey& game_master,
                 FindingSink& sink);

void check_dialogue_links(const Plugin& plugin,
                          std::span<const Record* const> records,
                          const DialogueTopics& topics,
                          const WinnerMap& winners,
                          FindingSink& sink);

void check_conditions(const Plugin& plugin,
                      std::span<const Record* const> records,
                      FindingSink& sink);

void check_preflight(const Plugin& plugin,
                     std::span<const Record* const> records,
                     FindingSink& sink);

}  // namespace fo4
