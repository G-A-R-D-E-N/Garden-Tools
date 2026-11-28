#pragma once

#include <optional>
#include <string>

#include <fo4/environment/environment.hpp>
#include <fo4/forms/placed_reference.hpp>
#include <fo4/forms/cell_edit.hpp>

namespace fo4ide::plugin::referenceinternal {

std::optional<std::string> referenceEditRefusal(const fo4::env::Environment& env,
                                                const fo4::FormKey& reference,
                                                const fo4::forms::PlacedReferenceEdit& edit);

}  // namespace fo4ide::plugin::referenceinternal
