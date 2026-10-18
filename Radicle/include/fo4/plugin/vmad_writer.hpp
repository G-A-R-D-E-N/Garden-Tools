#pragma once

#include "fo4/plugin/mutation.hpp"
#include "fo4/schema/vmad.hpp"

#include <string_view>
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

enum class VmadStructureMutationStatus {
  updated,
  unchanged,
  already_present,
  record_missing,
  subrecord_missing,
  index_out_of_range,
  not_authorable,
};

VmadStructureMutationStatus add_vmad(Plugin& plugin, const FormKey& record);

VmadStructureMutationStatus add_vmad_script(Plugin& plugin, const FormKey& record,
                                            std::size_t index, std::string_view name,
                                            std::uint8_t flags);

VmadStructureMutationStatus remove_vmad_script(Plugin& plugin, const FormKey& record,
                                               std::size_t index);

VmadStructureMutationStatus add_vmad_property(Plugin& plugin, const FormKey& record,
                                              std::size_t script, std::size_t index,
                                              const VmadProperty& property);

VmadStructureMutationStatus remove_vmad_property(Plugin& plugin, const FormKey& record,
                                                 std::size_t script, std::size_t index);

VmadStructureMutationStatus replace_vmad_property(Plugin& plugin, const FormKey& record,
                                                  std::size_t script, std::size_t index,
                                                  const VmadProperty& property);

}
