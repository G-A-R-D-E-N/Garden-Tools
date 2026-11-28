#include "fo4/schema/record_flags.hpp"

#include <algorithm>
#include <iterator>

namespace fo4::schema {
namespace {

#include "fo4/schema/generated_record_flags.inc"

bool same_signature(const RecordFlagDescriptor& row, Signature signature) {
  return row.signature == signature;
}

}  // namespace

std::span<const RecordFlagDescriptor> record_flags(Signature signature) {
  const auto first = std::find_if(std::begin(k_record_flags), std::end(k_record_flags),
                                  [signature](const RecordFlagDescriptor& row) {
                                    return same_signature(row, signature);
                                  });
  if (first == std::end(k_record_flags)) return {};
  const auto last = std::find_if(first, std::end(k_record_flags),
                                 [signature](const RecordFlagDescriptor& row) {
                                   return !same_signature(row, signature);
                                 });
  return {first, last};
}

const RecordFlagDescriptor* record_flag_descriptor(std::string_view path) {
  const auto it = std::find_if(std::begin(k_record_flags), std::end(k_record_flags),
                               [path](const RecordFlagDescriptor& row) { return row.path == path; });
  return it == std::end(k_record_flags) ? nullptr : &*it;
}

bool record_flag_set(const Record& record, const RecordFlagDescriptor& descriptor) {
  return (record.header.flags.value() & descriptor.mask) != 0;
}

std::optional<bool> record_flag_value(const Record& record, std::string_view path) {
  const auto* descriptor = record_flag_descriptor(path);
  if (descriptor == nullptr || descriptor->signature != record.header.signature) return std::nullopt;
  return record_flag_set(record, *descriptor);
}

}  // namespace fo4::schema
