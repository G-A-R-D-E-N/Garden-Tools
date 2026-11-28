#pragma once

#include "fo4/plugin/container.hpp"
#include "fo4/schema/localized_fields.hpp"

#include <span>

namespace fo4::forms {

enum class MessageButtonsWrite {
  written,
  record_missing,
  unsupported_record,
  too_many_buttons,
  invalid_text,
  localization_mismatch,
  unexpected_layout,
  no_insertion_point,
};

// Constraint: replaces every ITXT and its CTDA, CIS1 and CIS2 rows, ORs the MessageBox bit into DNAM keeping its other bits, at most 65535 buttons; each text must be an id in a localized plugin and non-empty NUL-free inline text otherwise, and a refusal changes nothing.
MessageButtonsWrite set_message_buttons(Plugin& plugin,
                                        const FormKey& record_key,
                                        std::span<const schema::LocalizedString> texts);

}  // namespace fo4::forms
