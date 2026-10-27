#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"

#include "field_writer_internal.hpp"
#include "record_edit.hpp"

#include "fo4/schema/scalar.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

namespace fo4 {
namespace detail {

std::vector<SubrecordGroupSpan> group_spans(const Record& record, const schema::SubrecordGroupDescriptor& group) {
  const auto member_index = [](std::span<const schema::SubrecordGroupMemberDescriptor> members,
                               Signature signature) -> std::optional<std::size_t> {
    for (std::size_t m = 0; m < members.size(); ++m) {
      if (members[m].subrecord == signature) return m;
    }
    return std::nullopt;
  };
  const auto refuse = [&](const std::string& reason) {
    return schema::SubrecordGroupError(std::string(group.path) + " " + reason);
  };
  const auto check_size = [&](std::span<const schema::SubrecordGroupMemberDescriptor> members,
                              std::size_t member, const Subrecord& sub) {
    if (members[member].payload_size != 0 && sub.data.size() != members[member].payload_size) {
      throw refuse(sub.signature.to_string() + " size differs from its member size");
    }
  };

  std::size_t lead = 0;
  while (lead < group.members.size() && group.members[lead].leading) ++lead;

  std::vector<SubrecordGroupSpan> spans;
  const auto& subs = record.subrecords;
  std::size_t first = 0;
  // Constraint: a counted group's runs follow its count subrecord, so a member signature before it (the LENS record DNAM) belongs to no run.
  if (group.count_subrecord != Signature{}) {
    const auto counter = std::find_if(subs.begin(), subs.end(), [&](const Subrecord& sub) {
      return sub.signature == group.count_subrecord;
    });
    if (counter != subs.end()) first = static_cast<std::size_t>(counter - subs.begin()) + 1;
  }
  const auto consume_run = [&](std::size_t& end) {
    SubrecordGroupSpan nested{};
    nested.first = end;
    std::vector<std::size_t> counts(group.run.members.size());
    std::size_t last = 0;
    counts[0] = 1;
    check_size(group.run.members, 0, subs[end]);
    ++end;
    while (end < subs.size()) {
      const auto member = member_index(group.run.members, subs[end].signature);
      if (!member || *member == 0) break;
      if (*member < last || (*member == last && !group.run.members[*member].repeatable)) {
        throw refuse("has " + subs[end].signature.to_string() + " out of order or repeated");
      }
      last = *member;
      ++counts[*member];
      check_size(group.run.members, *member, subs[end]);
      ++end;
    }
    for (std::size_t m = 0; m < group.run.members.size(); ++m) {
      if (group.run.members[m].required && counts[m] == 0) {
        throw refuse("has a run without required " + group.run.members[m].subrecord.to_string());
      }
    }
    nested.end = end;
    return nested;
  };
  const bool uncounted_runs = !group.run.members.empty() && group.run.count_member == Signature{};
  while (first < subs.size()) {
    if (uncounted_runs && subs[first].signature == group.run.members[0].subrecord) {
      SubrecordGroupSpan span{};
      span.first = first;
      std::size_t end = first;
      while (end < subs.size() && subs[end].signature == group.run.members[0].subrecord) {
        span.runs.push_back(consume_run(end));
      }
      std::vector<std::size_t> counts(group.members.size());
      // Constraint: an uncounted span closes on at most one trailing member, so its group declares only one.
      if (end < subs.size()) {
        const auto member = member_index(group.members, subs[end].signature);
        if (member) {
          ++counts[*member];
          check_size(group.members, *member, subs[end]);
          ++end;
        }
      }
      for (std::size_t m = 0; m < group.members.size(); ++m) {
        if (group.members[m].required && counts[m] == 0) {
          throw refuse("has a run without required " + group.members[m].subrecord.to_string());
        }
      }
      span.end = end;
      spans.push_back(std::move(span));
      first = end;
      continue;
    }
    const auto leader = member_index(group.members, subs[first].signature);
    if (!leader) {
      if (member_index(group.run.members, subs[first].signature)) {
        throw refuse("count does not match its nested runs");
      }
      ++first;
      continue;
    }
    if (*leader > lead) {
      if (group.members[*leader].skippable) {
        ++first;
        continue;
      }
      throw refuse("has " + subs[first].signature.to_string() + " outside a leader run");
    }
    std::vector<std::size_t> counts(group.members.size());
    counts[*leader] = 1;
    std::size_t last = *leader;
    std::size_t end = first + 1;
    SubrecordGroupSpan span{};
    span.first = first;
    check_size(group.members, *leader, subs[first]);
    const auto nested_runs = [&](const Subrecord& member) {
      const auto& run = group.run;
      if (run.count_member != member.signature) return;
      if (member.data.size() != sizeof(std::uint32_t)) {
        throw refuse(member.signature.to_string() + " run count is not four bytes");
      }
      const auto total = static_cast<std::size_t>(read_counter(member.data));
      for (std::size_t k = 0; k < total; ++k) {
        if (end >= subs.size() || subs[end].signature != run.members[0].subrecord) {
          throw refuse("count does not match its nested runs");
        }
        span.runs.push_back(consume_run(end));
      }
    };
    nested_runs(subs[first]);
    while (end < subs.size()) {
      const auto member = member_index(group.members, subs[end].signature);
      if (!member || (counts[lead] != 0 && *member <= lead)) break;
      if (*member < last || (*member == last && !group.members[*member].repeatable)) {
        throw refuse("has " + subs[end].signature.to_string() + " out of order or repeated");
      }
      last = *member;
      ++counts[*member];
      check_size(group.members, *member, subs[end]);
      const auto& consumed = subs[end];
      ++end;
      nested_runs(consumed);
    }
    for (std::size_t m = 0; m < group.members.size(); ++m) {
      if (group.members[m].required && counts[m] == 0) {
        throw refuse("has a run without required " + group.members[m].subrecord.to_string());
      }
    }
    span.end = end;
    spans.push_back(std::move(span));
    first = end;
  }
  return spans;
}

}  // namespace detail

namespace {

using detail::find_mutable_record;
using detail::group_spans;
using detail::mark_record_modified;
using detail::read_counter;

void require_counted_array_record(const Record& record, const schema::CountedArrayDescriptor& array) {
  if (record.header.signature != array.record) {
    throw schema::CountedArrayError(std::string(array.path) + " does not apply to " +
                                    record.header.signature.to_string() + " records");
  }
}

void write_counter(std::vector<std::byte>& data, std::uint32_t value) {
  for (std::size_t i = 0; i < 4; ++i) {
    data[i] = std::byte{static_cast<std::uint8_t>(value >> (8 * i))};
  }
}

struct CountedArraySubrecords {
  std::size_t payload;
  std::optional<std::size_t> counter;
  std::size_t count;
};

std::optional<CountedArraySubrecords> counted_array_subrecords(const Record& record,
                                                               const schema::CountedArrayDescriptor& array) {
  if (array.segment_count != 0) {
    throw schema::CountedArrayError(std::string(array.path) +
                                    " is a header-counted segment and cannot be inserted or removed");
  }
  std::optional<std::size_t> payload;
  std::optional<std::size_t> counter;
  for (std::size_t i = 0; i < record.subrecords.size(); ++i) {
    const auto signature = record.subrecords[i].signature;
    if (signature == array.subrecord) {
      if (payload) {
        throw schema::CountedArrayError(std::string(array.path) + " has duplicate " +
                                        array.subrecord.to_string() + " subrecords");
      }
      payload = i;
    } else if (signature == array.count_subrecord) {
      if (counter) {
        throw schema::CountedArrayError(std::string(array.path) + " has duplicate " +
                                        array.count_subrecord.to_string() + " subrecords");
      }
      counter = i;
    }
  }
  if (!payload) return std::nullopt;

  const auto& data = record.subrecords[*payload].data;
  if (array.count_subrecord == Signature{}) {
    if (data.size() % array.stride != 0) {
      throw schema::CountedArrayError(std::string(array.path) + " " + array.subrecord.to_string() +
                                      " payload is not a whole number of elements");
    }
    return CountedArraySubrecords{*payload, std::nullopt, data.size() / array.stride};
  }
  if (!counter) return std::nullopt;

  const auto& count_data = record.subrecords[*counter].data;
  if (count_data.size() != 4) {
    throw schema::CountedArrayError(std::string(array.path) + " count subrecord must be four bytes");
  }
  const auto count = static_cast<std::size_t>(read_counter(count_data));
  if (data.size() % array.stride != 0 || data.size() / array.stride != count) {
    throw schema::CountedArrayError(std::string(array.path) + " count does not match its " +
                                    array.subrecord.to_string() + " payload");
  }
  return CountedArraySubrecords{*payload, *counter, count};
}

void require_subrecord_group_record(const Record& record, const schema::SubrecordGroupDescriptor& group) {
  if (record.header.signature != group.record) {
    throw schema::SubrecordGroupError(std::string(group.path) + " does not apply to " +
                                      record.header.signature.to_string() + " records");
  }
}

std::size_t group_run_counter(const Record& record, const SubrecordGroupSpan& span, Signature count_member) {
  for (std::size_t i = span.first; i < span.end; ++i) {
    if (record.subrecords[i].signature == count_member) return i;
  }
  return record.subrecords.size();
}

std::size_t first_successor_index(const Record& record, std::span<const Signature> successors) {
  const auto& subs = record.subrecords;
  for (std::size_t i = 0; i < subs.size(); ++i) {
    if (std::find(successors.begin(), successors.end(), subs[i].signature) != successors.end()) return i;
  }
  return subs.size();
}

std::optional<std::size_t> group_counter(const Record& record,
                                         const schema::SubrecordGroupDescriptor& group,
                                         std::size_t runs) {
  std::optional<std::size_t> counter;
  for (std::size_t i = 0; i < record.subrecords.size(); ++i) {
    if (record.subrecords[i].signature != group.count_subrecord) continue;
    if (counter) {
      throw schema::SubrecordGroupError(std::string(group.path) + " has duplicate " +
                                        group.count_subrecord.to_string() + " subrecords");
    }
    counter = i;
  }
  if (!counter) return std::nullopt;

  const auto& data = record.subrecords[*counter].data;
  if (data.size() != sizeof(std::uint32_t)) {
    throw schema::SubrecordGroupError(std::string(group.path) + " count subrecord has the wrong size");
  }
  if (static_cast<std::size_t>(read_counter(data)) != runs) {
    throw schema::SubrecordGroupError(std::string(group.path) + " count does not match its runs");
  }
  return counter;
}

}  // namespace

CountedArrayMutationStatus insert_record_counted_element(Plugin& plugin,
                                                         const FormKey& record_key,
                                                         const schema::CountedArrayDescriptor& array,
                                                         std::size_t index) {
  assign_record_form_keys(plugin);
  auto* target = find_mutable_record(plugin, record_key);
  if (target == nullptr) return CountedArrayMutationStatus::record_missing;
  require_counted_array_record(*target, array);
  const auto found = counted_array_subrecords(*target, array);
  if (!found) return CountedArrayMutationStatus::subrecord_missing;
  if (index > found->count) return CountedArrayMutationStatus::index_out_of_range;

  auto& data = target->subrecords[found->payload].data;
  data.insert(data.begin() + static_cast<std::ptrdiff_t>(index * array.stride), array.stride, std::byte{0});
  if (found->counter) {
    write_counter(target->subrecords[*found->counter].data, static_cast<std::uint32_t>(found->count + 1));
  }
  mark_record_modified(plugin, *target);
  return CountedArrayMutationStatus::updated;
}

CountedArrayMutationStatus remove_record_counted_element(Plugin& plugin,
                                                         const FormKey& record_key,
                                                         const schema::CountedArrayDescriptor& array,
                                                         std::size_t index) {
  assign_record_form_keys(plugin);
  auto* target = find_mutable_record(plugin, record_key);
  if (target == nullptr) return CountedArrayMutationStatus::record_missing;
  require_counted_array_record(*target, array);
  const auto found = counted_array_subrecords(*target, array);
  if (!found) return CountedArrayMutationStatus::subrecord_missing;
  if (index >= found->count) return CountedArrayMutationStatus::index_out_of_range;
  if (!found->counter && found->count == 1) return CountedArrayMutationStatus::would_empty;

  auto& data = target->subrecords[found->payload].data;
  const auto first = data.begin() + static_cast<std::ptrdiff_t>(index * array.stride);
  data.erase(first, first + static_cast<std::ptrdiff_t>(array.stride));
  if (found->counter) {
    write_counter(target->subrecords[*found->counter].data, static_cast<std::uint32_t>(found->count - 1));
  }
  mark_record_modified(plugin, *target);
  return CountedArrayMutationStatus::updated;
}

std::optional<std::vector<SubrecordGroupSpan>> subrecord_group_spans(
    const Plugin& plugin,
    const FormKey& record_key,
    const schema::SubrecordGroupDescriptor& group) {
  const auto* record = find_record(plugin, record_key);
  if (record == nullptr) return std::nullopt;
  require_subrecord_group_record(*record, group);
  return group_spans(*record, group);
}

SubrecordGroupMutationStatus insert_record_subrecord_group(Plugin& plugin,
                                                           const FormKey& record_key,
                                                           const schema::SubrecordGroupDescriptor& group,
                                                           std::size_t index) {
  assign_record_form_keys(plugin);
  auto* target = find_mutable_record(plugin, record_key);
  if (target == nullptr) return SubrecordGroupMutationStatus::record_missing;
  require_subrecord_group_record(*target, group);
  if (!group.run.members.empty() && group.run.count_member == Signature{}) {
    throw schema::SubrecordGroupError(std::string(group.path) + " spans uncounted sub-runs");
  }
  const auto spans = group_spans(*target, group);
  const bool counted = group.count_subrecord != Signature{};
  const auto counter = group_counter(*target, group, spans.size());
  if (counted && !counter && (!spans.empty() || group.requires_existing_count)) return SubrecordGroupMutationStatus::subrecord_missing;
  if (index > spans.size()) return SubrecordGroupMutationStatus::index_out_of_range;

  const std::size_t position = index < spans.size()  ? spans[index].first
                               : !counter            ? first_successor_index(*target, group.successors)
                               : spans.empty()       ? *counter + 1
                                                     : spans.back().end;
  std::vector<Subrecord> created;
  if (counted && !counter) {
    Subrecord count;
    count.signature = group.count_subrecord;
    count.data.assign(sizeof(std::uint32_t), std::byte{0});
    created.push_back(std::move(count));
  }
  for (const auto& member : group.members) {
    if (member.localized && plugin.header.tes4_flags.is_localized()) {
      throw schema::SubrecordGroupError(std::string(group.path) + " cannot be inserted into a localized plugin");
    }
    if (!member.required && member.default_payload.empty()) continue;
    Subrecord subrecord;
    subrecord.signature = member.subrecord;
    if (member.default_payload.empty()) {
      subrecord.data.assign(member.payload_size, std::byte{0});
    } else {
      subrecord.data.reserve(member.default_payload.size());
      for (const auto byte : member.default_payload) {
        subrecord.data.push_back(std::byte{byte});
      }
    }
    created.push_back(std::move(subrecord));
  }
  target->subrecords.insert(target->subrecords.begin() + static_cast<std::ptrdiff_t>(position),
                            std::make_move_iterator(created.begin()), std::make_move_iterator(created.end()));
  const std::size_t count_index = !counter                ? position
                                  : *counter < position  ? *counter
                                                         : *counter + created.size();
  if (counted) {
    write_counter(target->subrecords[count_index].data, static_cast<std::uint32_t>(spans.size() + 1));
  }
  mark_record_modified(plugin, *target);
  return SubrecordGroupMutationStatus::updated;
}

SubrecordGroupMutationStatus remove_record_subrecord_group(Plugin& plugin,
                                                           const FormKey& record_key,
                                                           const schema::SubrecordGroupDescriptor& group,
                                                           std::size_t index) {
  assign_record_form_keys(plugin);
  auto* target = find_mutable_record(plugin, record_key);
  if (target == nullptr) return SubrecordGroupMutationStatus::record_missing;
  require_subrecord_group_record(*target, group);
  if (!group.run.members.empty() && group.run.count_member == Signature{}) {
    throw schema::SubrecordGroupError(std::string(group.path) + " spans uncounted sub-runs");
  }
  const auto spans = group_spans(*target, group);
  const bool counted = group.count_subrecord != Signature{};
  const auto counter = group_counter(*target, group, spans.size());
  if (counted && !counter) return SubrecordGroupMutationStatus::subrecord_missing;
  if (index >= spans.size()) return SubrecordGroupMutationStatus::index_out_of_range;

  const auto span = spans[index];
  if (counted) {
    write_counter(target->subrecords[*counter].data, static_cast<std::uint32_t>(spans.size() - 1));
  }
  const auto first = target->subrecords.begin() + static_cast<std::ptrdiff_t>(span.first);
  target->subrecords.erase(first, first + static_cast<std::ptrdiff_t>(span.end - span.first));
  mark_record_modified(plugin, *target);
  return SubrecordGroupMutationStatus::updated;
}

SubrecordGroupMutationStatus insert_record_subrecord_group_run(Plugin& plugin,
                                                               const FormKey& record_key,
                                                               const schema::SubrecordGroupDescriptor& group,
                                                               std::size_t run,
                                                               std::size_t index) {
  assign_record_form_keys(plugin);
  auto* target = find_mutable_record(plugin, record_key);
  if (target == nullptr) return SubrecordGroupMutationStatus::record_missing;
  require_subrecord_group_record(*target, group);
  if (group.run.count_member == Signature{}) {
    throw schema::SubrecordGroupError(std::string(group.path) + " has no counted sub-run");
  }
  const auto spans = group_spans(*target, group);
  if (run >= spans.size()) return SubrecordGroupMutationStatus::index_out_of_range;
  const auto& span = spans[run];
  if (index > span.runs.size()) return SubrecordGroupMutationStatus::index_out_of_range;
  const auto counter = group_run_counter(*target, span, group.run.count_member);
  if (counter == target->subrecords.size()) {
    throw schema::SubrecordGroupError(std::string(group.path) + " has no " +
                                      group.run.count_member.to_string() + " run count");
  }

  const std::size_t position = index < span.runs.size() ? span.runs[index].first
                               : span.runs.empty()      ? counter + 1
                                                        : span.runs.back().end;
  std::vector<Subrecord> created;
  for (const auto& member : group.run.members) {
    if (member.localized && plugin.header.tes4_flags.is_localized()) {
      throw schema::SubrecordGroupError(std::string(group.path) + " cannot be inserted into a localized plugin");
    }
    if (!member.required && member.default_payload.empty()) continue;
    Subrecord subrecord;
    subrecord.signature = member.subrecord;
    if (member.default_payload.empty()) {
      subrecord.data.assign(member.payload_size, std::byte{0});
    } else {
      subrecord.data.reserve(member.default_payload.size());
      for (const auto byte : member.default_payload) {
        subrecord.data.push_back(std::byte{byte});
      }
    }
    created.push_back(std::move(subrecord));
  }
  target->subrecords.insert(target->subrecords.begin() + static_cast<std::ptrdiff_t>(position),
                            std::make_move_iterator(created.begin()), std::make_move_iterator(created.end()));
  write_counter(target->subrecords[counter].data, static_cast<std::uint32_t>(span.runs.size() + 1));
  mark_record_modified(plugin, *target);
  return SubrecordGroupMutationStatus::updated;
}

SubrecordGroupMutationStatus remove_record_subrecord_group_run(Plugin& plugin,
                                                               const FormKey& record_key,
                                                               const schema::SubrecordGroupDescriptor& group,
                                                               std::size_t run,
                                                               std::size_t index) {
  assign_record_form_keys(plugin);
  auto* target = find_mutable_record(plugin, record_key);
  if (target == nullptr) return SubrecordGroupMutationStatus::record_missing;
  require_subrecord_group_record(*target, group);
  if (group.run.count_member == Signature{}) {
    throw schema::SubrecordGroupError(std::string(group.path) + " has no counted sub-run");
  }
  const auto spans = group_spans(*target, group);
  if (run >= spans.size()) return SubrecordGroupMutationStatus::index_out_of_range;
  const auto& span = spans[run];
  if (index >= span.runs.size()) return SubrecordGroupMutationStatus::index_out_of_range;
  const auto counter = group_run_counter(*target, span, group.run.count_member);
  if (counter == target->subrecords.size()) {
    throw schema::SubrecordGroupError(std::string(group.path) + " has no " +
                                      group.run.count_member.to_string() + " run count");
  }

  write_counter(target->subrecords[counter].data, static_cast<std::uint32_t>(span.runs.size() - 1));
  const auto first = target->subrecords.begin() + static_cast<std::ptrdiff_t>(span.runs[index].first);
  target->subrecords.erase(first, first + static_cast<std::ptrdiff_t>(span.runs[index].end - span.runs[index].first));
  mark_record_modified(plugin, *target);
  return SubrecordGroupMutationStatus::updated;
}

}
