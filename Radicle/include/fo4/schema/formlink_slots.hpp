#pragma once

#include "fo4/plugin/container.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/signature.hpp"

#include <cstddef>
#include <optional>
#include <span>

namespace fo4::schema {

std::optional<FormLinkFieldDescriptor> formlink_field_at(
    std::span<const FormLinkFieldDescriptor> fields,
    const Record& record,
    Signature subrecord,
    std::size_t ordinal,
    std::size_t offset);

}
