#include "fo4/schema/formlink_lists.hpp"

#include <string>

namespace fo4::schema {

FormLinkListMutationStatus insert_record_nested_formlink_list_null_element(
    Plugin& plugin,
    const FormKey& record_key,
    const NestedFormLinkListDescriptor& descriptor,
    std::size_t container_index,
    std::size_t index) {
  if (!descriptor.nullable) {
    throw FormLinkListError(
        std::string(descriptor.container_path) + "\\" + std::string(descriptor.member) +
        " does not allow null FormLink elements");
  }
  return insert_record_nested_formlink_list_element(
      plugin, record_key, descriptor, container_index, index, FormKey{});
}

}  // namespace fo4::schema
