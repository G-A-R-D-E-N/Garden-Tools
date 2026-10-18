#pragma once

#include "fo4/plugin/container.hpp"

#include <optional>
#include <string_view>

namespace fo4::forms {

enum class WrapperCopyStatus {
  wrapped,
  source_missing,
  nested_source,
  unsupported_signature,
  override_failed,
  leveled_list_failed,
};

struct WrapperCopyResult {
  WrapperCopyStatus status{WrapperCopyStatus::source_missing};
  FormKey copy{};
  FormKey wrapper{};
};

WrapperCopyResult copy_record_as_wrapper(
    Plugin& destination,
    const Plugin& source,
    const FormKey& record,
    std::optional<std::string_view> copy_editor_id = std::nullopt);

}  // namespace fo4::forms
