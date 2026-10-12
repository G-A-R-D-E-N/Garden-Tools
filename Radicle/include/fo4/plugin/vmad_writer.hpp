#pragma once

#include "fo4/plugin/mutation.hpp"

#include <variant>

namespace fo4 {

struct VmadObjectValue {
  FormKey target;
  std::int16_t alias{-1};
};

using VmadValue = std::variant<VmadObjectValue, std::string, std::int32_t, float, bool>;

ScalarMutationStatus set_vmad_property(Plugin& plugin, const FormKey& record,
                                      std::size_t script, std::size_t property,
                                      const VmadValue& value, std::size_t element = 0);

}
