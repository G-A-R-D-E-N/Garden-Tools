#pragma once

#include "fo4/plugin/container.hpp"
#include "fo4/signature.hpp"

#include <cstddef>
#include <vector>

namespace fo4 {

enum class SubrecordCopyStatus {
  copied,
  record_missing,
  source_missing,
  source_occurrence_missing,
  occurrence_unreachable,
  owner_not_master,
  not_authorable,
  unknown_order,
  no_default,
  localized_mismatch,
};

SubrecordCopyStatus copy_record_subrecord(Plugin& destination,
                                          const FormKey& destination_key,
                                          const Plugin& source,
                                          const FormKey& source_key,
                                          Signature subrecord,
                                          std::size_t occurrence,
                                          std::vector<PluginKey>* missing_owners = nullptr);

}  // namespace fo4
