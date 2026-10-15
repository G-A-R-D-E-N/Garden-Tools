#pragma once

#include "fo4/plugin/container.hpp"

#include <cstddef>
#include <vector>

namespace fo4 {

struct MasterUsage {
  std::vector<std::size_t> references;
  std::size_t records_with_incomplete_vmad{0};
  std::size_t unclaimed_link_payloads{0};
  std::size_t undecodable_records{0};

  [[nodiscard]] bool complete() const noexcept {
    return records_with_incomplete_vmad == 0 && undecodable_records == 0;
  }
};

MasterUsage scan_master_usage(const Plugin& plugin);

std::vector<MasterDescriptor> used_masters(const Plugin& plugin, const MasterUsage& usage);

}  // namespace fo4
