#pragma once

#include "fo4/formid.hpp"
#include "fo4/plugin/container.hpp"
#include "fo4/signature.hpp"

#include <vector>

namespace fo4 {

enum class RecordChange {
  added,
  removed,
  changed,
};

struct RecordDiff {
  FormKey key;
  Signature signature{};
  RecordChange change{RecordChange::changed};
  bool header_changed{false};
  // source:fo4/plugin/container.hpp Subrecord; each differing signature must appear once.
  std::vector<Signature> changed_subrecords{};
};

std::vector<RecordDiff> diff_plugins(const Plugin& before, const Plugin& after);

}  // namespace fo4
