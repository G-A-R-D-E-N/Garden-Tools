#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"

#include "field_writer_internal.hpp"
#include "fo4/plugin/record_edit.hpp"

#include "fo4/schema/record_constraints.hpp"
#include "fo4/schema/scalar.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace fo4 {
namespace detail {

std::vector<SubrecordGroupSpan> group_spans(const Record& record,
                                            const schema::SubrecordGroupDescriptor& group,
                                            const SubrecordGroupSpan* within) {
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
  if (within != nullptr && group.parent.empty()) throw refuse("has no parent group to scope it");
  if (group.greedy) {
    const auto is_leader = [&](Signature signature) {
      return lead == 0 ? signature == group.members[0].subrecord
                       : member_index(group.members.first(lead), signature).has_value();
    };
    const auto is_stop = [&](Signature signature) {
      return std::find(group.stops.begin(), group.stops.end(), signature) != group.stops.end();
    };
    const std::size_t limit = within != nullptr ? std::min(within->end, subs.size()) : subs.size();
    for (std::size_t first = within != nullptr ? within->first : 0; first < limit;) {
      if (!is_leader(subs[first].signature)) {
        ++first;
        continue;
      }
      const auto leader = member_index(group.members, subs[first].signature);
      check_size(group.members, *leader, subs[first]);
      std::size_t end = first + 1;
      while (end < limit && !is_leader(subs[end].signature) && !is_stop(subs[end].signature)) {
        const bool closed = group.closer != Signature{} && subs[end].signature == group.closer;
        ++end;
        if (closed) break;
      }
      SubrecordGroupSpan span{};
      span.first = first;
      span.end = end;
      spans.push_back(std::move(span));
      first = end;
    }
    return spans;
  }
  std::size_t first = 0;
  std::optional<std::size_t> counted_runs;
  // Constraint: only a group with leading members treats a missing counter as no spans; other groups still scan, so writes can refuse runs that have no counter.
  if (group.count_subrecord != Signature{}) {
    const auto counter = std::find_if(subs.begin(), subs.end(), [&](const Subrecord& sub) {
      return sub.signature == group.count_subrecord;
    });
    if (counter == subs.end()) {
      if (lead > 0) return spans;
    } else {
      if (counter->data.size() != sizeof(std::uint32_t)) {
        throw refuse(group.count_subrecord.to_string() + " count subrecord is not four bytes");
      }
      counted_runs = static_cast<std::size_t>(read_counter(counter->data));
      first = static_cast<std::size_t>(counter - subs.begin()) + 1;
    }
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
    // Constraint: past the counted runs nothing is a run, so a group member there refuses as a count mismatch instead of starting a run.
    if (counted_runs && spans.size() == *counted_runs) {
      if (member_index(group.members, subs[first].signature) ||
          member_index(group.run.members, subs[first].signature)) {
        throw refuse("count does not match its runs");
      }
      break;
    }
    if (uncounted_runs && subs[first].signature == group.run.members[0].subrecord) {
      SubrecordGroupSpan span{};
      span.first = first;
      std::size_t end = first;
      while (end < subs.size() && subs[end].signature == group.run.members[0].subrecord) {
        span.runs.push_back(consume_run(end));
      }
      std::vector<std::size_t> counts(group.members.size());
      // Invariant: an uncounted span takes at most one trailing group member after its last run.
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
    // Constraint: with no counter, a first member without its required next member is a record-level subrecord sharing the signature, as LENS DNAM does.
    if (!counted_runs && group.count_subrecord != Signature{} && *leader == 0 &&
        group.members.size() > 1 && group.members[1].required &&
        (first + 1 >= subs.size() || subs[first + 1].signature != group.members[1].subrecord)) {
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

Subrecord default_subrecord(const schema::SubrecordGroupMemberDescriptor& member, std::uint16_t form_version) {
  Subrecord subrecord;
  subrecord.signature = member.subrecord;
  const bool older = !member.older_default.empty() && form_version < member.older_before;
  const auto bytes = older ? member.older_default : member.default_payload;
  if (bytes.empty()) {
    subrecord.data.assign(member.payload_size, std::byte{0});
  } else {
    for (const auto byte : bytes) subrecord.data.push_back(std::byte{byte});
  }
  return subrecord;
}

void require_counted_array_record(const Record& record, const schema::CountedArrayDescriptor& array) {
  if (record.header.signature != array.record) {
    throw schema::CountedArrayError(std::string(array.path) + " does not apply to " +
                                    record.header.signature.to_string() + " records");
  }
}

std::uint32_t read_counter_at(const std::vector<std::byte>& data, std::size_t offset) {
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[offset + i])) << (8 * i);
  }
  return value;
}

void write_counter_at(std::vector<std::byte>& data, std::size_t offset, std::uint32_t value) {
  for (std::size_t i = 0; i < 4; ++i) {
    data[offset + i] = std::byte{static_cast<std::uint8_t>(value >> (8 * i))};
  }
}

void write_counter(std::vector<std::byte>& data, std::uint32_t value) {
  write_counter_at(data, 0, value);
}

struct CountedArraySubrecords {
  std::size_t payload;
  std::optional<std::size_t> counter;
  std::size_t count;
  // Invariant: header_counted arrays keep their count in a u32 word of the payload
// Constraint: itself, at header_count_offset, and their elements start at element_offset.
  std::size_t element_offset{0};
  std::size_t header_count_offset{0};
  bool header_counted{false};
  std::size_t count_divisor{1};
};

std::optional<CountedArraySubrecords> counted_array_subrecords(const Record& record,
                                                               const schema::CountedArrayDescriptor& array) {
  std::optional<std::size_t> payload;
  std::optional<std::size_t> counter;
  std::size_t occurrences = 0;
  for (std::size_t i = 0; i < record.subrecords.size(); ++i) {
    const auto signature = record.subrecords[i].signature;
    if (signature == array.subrecord) {
      if (array.has_subrecord_occurrence) {
        if (occurrences++ == array.subrecord_occurrence) payload = i;
      } else {
        if (payload) {
          throw schema::CountedArrayError(std::string(array.path) + " has duplicate " +
                                          array.subrecord.to_string() + " subrecords");
        }
        payload = i;
      }
    } else if (signature == array.count_subrecord) {
      if (counter) {
        throw schema::CountedArrayError(std::string(array.path) + " has duplicate " +
                                        array.count_subrecord.to_string() + " subrecords");
      }
      counter = i;
    }
  }
  if (!payload) return std::nullopt;
  if (array.segment_count != 0) {
    const auto& data = record.subrecords[*payload].data;
    schema::FieldAddress address{};
    address.stride = array.stride;
    address.segment = schema::SegmentAddress{array.count_word, array.segment_index,
                                             array.segment_count};
    address.subrecord_occurrence = array.subrecord_occurrence;
    address.has_subrecord_occurrence = array.has_subrecord_occurrence;
    const auto span = schema::header_segment(data, record.header.signature, array.subrecord, address);
    if (!span) {
      throw schema::CountedArrayError(std::string(array.path) + " " + array.subrecord.to_string() +
                                      " payload does not match its header-counted segments");
    }
    const std::size_t count_offset =
        4 * (array.count_word + (array.header_words == 0 ? 1 : 0));
    if (data.size() < count_offset + 4) {
      throw schema::CountedArrayError(std::string(array.path) +
                                      " payload is too short for its header counter");
    }
    const std::size_t divisor = array.count_divisor == 0 ? 1 : array.count_divisor;
    if (read_counter_at(data, count_offset) != span->count * divisor) {
      throw schema::CountedArrayError(std::string(array.path) +
                                      " header counter does not match its segment");
    }
    return CountedArraySubrecords{*payload, std::nullopt, span->count, span->base,
                                  count_offset, true, divisor};
  }

  const auto& data = record.subrecords[*payload].data;
  if (array.count_subrecord == Signature{}) {
    if (data.size() % array.stride != 0) {
      throw schema::CountedArrayError(std::string(array.path) + " " + array.subrecord.to_string() +
                                      " payload is not a whole number of elements");
    }
    return CountedArraySubrecords{*payload, std::nullopt, data.size() / array.stride,
                                  0, 0, false, 1};
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
  return CountedArraySubrecords{*payload, *counter, count, 0, 0, false, 1};
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

using BlockKind = schema::SubrecordGroupBlockError::Kind;

// Invariant: a block runs from its counter to its closer, each element ends at the leader, and the counter equals the element count.
struct BlockLayout {
  std::size_t counter{0};
  std::size_t closer{0};
  std::vector<SubrecordGroupSpan> spans;
};

Signature block_leader(const schema::SubrecordGroupDescriptor& group) {
  for (const auto& member : group.members) {
    if (!member.leading) return member.subrecord;
  }
  return Signature{};
}

std::optional<BlockLayout> block_layout(const Record& record, const schema::SubrecordGroupDescriptor& group) {
  const auto malformed = [&](const char* reason) {
    return schema::SubrecordGroupBlockError(BlockKind::malformed, std::string(group.path) + " block " + reason);
  };
  const auto& subs = record.subrecords;
  std::optional<std::size_t> counter;
  for (std::size_t at = 0; at < subs.size(); ++at) {
    if (subs[at].signature != group.count_subrecord) continue;
    if (counter) throw malformed("has more than one counter");
    counter = at;
  }
  if (!counter) return std::nullopt;
  if (subs[*counter].data.size() != sizeof(std::uint32_t)) throw malformed("counter is not a four-byte count");

  const auto leader = block_leader(group);
  const auto is_member = [&](Signature signature) {
    return std::any_of(group.members.begin(), group.members.end(),
                       [&](const auto& member) { return member.subrecord == signature; });
  };
  BlockLayout layout;
  layout.counter = *counter;
  std::size_t first = *counter + 1;
  std::size_t at = first;
  for (; at < subs.size(); ++at) {
    const auto signature = subs[at].signature;
    if (signature == group.block.closer) break;
    if (signature == leader) {
      if (subs[at].data.size() < group.block.leader_min_size) throw malformed("element is shorter than its header");
      SubrecordGroupSpan span{};
      span.first = first;
      span.end = at + 1;
      layout.spans.push_back(std::move(span));
      first = at + 1;
    } else if (!is_member(signature)) {
      throw malformed("holds a foreign subrecord");
    }
  }
  if (at == subs.size()) throw malformed("has no closer");
  if (first != at) throw malformed("ends without a closing element");
  if (layout.spans.size() != static_cast<std::size_t>(read_counter(subs[*counter].data))) {
    throw malformed("count does not match its elements");
  }
  layout.closer = at;
  return layout;
}

// Constraint: an element whose link word is set and at or above the index would dangle or shift, so the caller must repoint it first.
void require_unlinked(const Record& record, const schema::SubrecordGroupDescriptor& group, const BlockLayout& layout,
                      std::size_t index, bool skip_index) {
  for (std::size_t other = 0; other < layout.spans.size(); ++other) {
    if (skip_index && other == index) continue;
    const auto& data = record.subrecords[layout.spans[other].end - 1].data;
    const auto at = group.block.link_offset;
    const auto link = static_cast<std::uint16_t>(std::to_integer<unsigned>(data[at]) |
                                                 (std::to_integer<unsigned>(data[at + 1]) << 8));
    if (link != group.block.link_none && link >= index) {
      throw schema::SubrecordGroupBlockError(
          BlockKind::referenced, std::string(group.path) + " element " + std::to_string(other) +
                                     " links to element " + std::to_string(link) + ", repoint it first");
    }
  }
}

// Constraint: with no counter the block goes after the last subrecord whose authoring rule is at or before the counter's, or at the record end when the record's rules omit the counter; a record out of authoring order has no insertion point.
std::size_t block_insert_position(const Record& record, const schema::SubrecordGroupDescriptor& group) {
  const auto refuse = [&] {
    return schema::SubrecordGroupBlockError(BlockKind::no_insertion_point,
                                            std::string(group.path) + " has no insertion point in this record");
  };
  const auto constraint = schema::record_authoring_constraint(record.header.signature);
  if (!constraint) throw refuse();
  const auto rules = schema::authoring_subrecords(*constraint);
  const auto rule_index = [&](Signature signature) -> std::optional<std::size_t> {
    for (std::size_t index = 0; index < rules.size(); ++index) {
      if (rules[index].signature == signature) return index;
    }
    return std::nullopt;
  };
  const auto target = rule_index(group.count_subrecord);
  if (!target) return record.subrecords.size();
  std::optional<std::size_t> last;
  std::size_t position = 0;
  for (std::size_t at = 0; at < record.subrecords.size(); ++at) {
    const auto index = rule_index(record.subrecords[at].signature);
    if (!index || (last && *index < *last)) throw refuse();
    last = index;
    if (*index <= *target) position = at + 1;
  }
  return position;
}

struct SegmentRead {
  const std::vector<std::byte>* payload;
  schema::SegmentSpan span;
};

// Constraint: read-only; an unmodelled payload or a form version outside the member rows' range reads as absent.
std::optional<SegmentRead> read_segment(const Record& record, const schema::CountedArrayDescriptor& array) {
  const std::vector<std::byte>* payload = nullptr;
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature != array.subrecord) continue;
    if (payload != nullptr) {
      throw schema::CountedArrayError(std::string(array.path) + " has duplicate " + array.subrecord.to_string() +
                                      " subrecords");
    }
    payload = &subrecord.data;
  }
  if (payload == nullptr) return std::nullopt;
  for (const auto& member : schema::scalar_fields(array.record)) {
    const auto& scalar = member.scalar;
    if (scalar.subrecord != array.subrecord || scalar.address.segment.count == 0 ||
        scalar.address.segment.index != array.segment_index) {
      continue;
    }
    const auto version = record.header.form_version;
    if (version < scalar.min_form_version || (scalar.max_form_version != 0 && version >= scalar.max_form_version)) {
      return std::nullopt;
    }
    break;
  }
  schema::FieldAddress address;
  address.stride = array.stride;
  address.segment = {array.count_word, array.segment_index, array.segment_count};
  const auto span = schema::header_segment(*payload, array.record, array.subrecord, address);
  if (!span) return std::nullopt;
  return SegmentRead{payload, *span};
}

std::string hex_of(std::span<const std::byte> bytes) {
  static constexpr char k_hex[] = "0123456789abcdef";
  std::string out;
  out.reserve(bytes.size() * 2);
  for (const auto byte : bytes) {
    const auto value = std::to_integer<unsigned int>(byte);
    out.push_back(k_hex[value >> 4]);
    out.push_back(k_hex[value & 0x0FU]);
  }
  return out;
}

template <class Error>
std::vector<std::byte> bytes_of_hex(std::string_view hex, std::string_view path) {
  const auto digit = [&](char c) -> unsigned {
    if (c >= '0' && c <= '9') return static_cast<unsigned>(c - '0');
    if (c >= 'a' && c <= 'f') return static_cast<unsigned>(c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return static_cast<unsigned>(c - 'A' + 10);
    throw Error(std::string(path) + " element text is not hexadecimal");
  };
  if (hex.empty() || hex.size() % 2 != 0) {
    throw Error(std::string(path) + " element text must be a non-empty even number of hex digits");
  }
  std::vector<std::byte> out(hex.size() / 2);
  for (std::size_t i = 0; i < out.size(); ++i) {
    out[i] = std::byte{static_cast<std::uint8_t>((digit(hex[2 * i]) << 4) | digit(hex[2 * i + 1]))};
  }
  return out;
}

}  // namespace

std::optional<std::size_t> record_counted_array_size(const Plugin& plugin,
                                                     const FormKey& record_key,
                                                     const schema::CountedArrayDescriptor& array) {
  const auto* record = find_record(plugin, record_key);
  if (record == nullptr) return std::nullopt;
  require_counted_array_record(*record, array);
  if (array.segment_count != 0) {
    const auto segment = read_segment(*record, array);
    return segment ? segment->span.count : 0;
  }
  const auto found = counted_array_subrecords(*record, array);
  return found ? found->count : 0;
}

std::optional<std::string> record_counted_array_element_hex(const Record& record,
                                                            const schema::CountedArrayDescriptor& array,
                                                            std::size_t index) {
  require_counted_array_record(record, array);
  if (array.segment_count != 0) {
    const auto segment = read_segment(record, array);
    if (!segment || index >= segment->span.count) return std::nullopt;
    const std::span<const std::byte> data(*segment->payload);
    return hex_of(data.subspan(segment->span.base + index * array.stride, array.stride));
  }
  const auto found = counted_array_subrecords(record, array);
  if (!found || index >= found->count) return std::nullopt;
  const auto& data = record.subrecords[found->payload].data;
  const auto offset = index * array.stride;
  if (offset + array.stride > data.size()) return std::nullopt;
  return hex_of(std::span<const std::byte>(data).subspan(offset, array.stride));
}

std::optional<std::string> record_subrecord_group_element_hex(const Record& record,
                                                              const schema::SubrecordGroupDescriptor& group,
                                                              std::size_t index,
                                                              const SubrecordGroupSpan* within) {
  require_subrecord_group_record(record, group);
  const auto spans = detail::group_spans(record, group, within);
  if (index >= spans.size()) return std::nullopt;
  const auto& span = spans[index];
  std::vector<std::byte> combined;
  for (std::size_t subrecord = span.first; subrecord < span.end; ++subrecord) {
    const auto& payload = record.subrecords[subrecord].data;
    combined.insert(combined.end(), payload.begin(), payload.end());
  }
  static constexpr char k_hex[] = "0123456789abcdef";
  std::string out;
  out.reserve(combined.size() * 2);
  for (const auto byte : combined) {
    const auto value = std::to_integer<unsigned int>(byte);
    out.push_back(k_hex[value >> 4]);
    out.push_back(k_hex[value & 0x0FU]);
  }
  return out;
}

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
  data.insert(data.begin() + static_cast<std::ptrdiff_t>(found->element_offset + index * array.stride),
              array.stride, std::byte{0});
  if (found->header_counted) {
    write_counter_at(data, found->header_count_offset,
                     static_cast<std::uint32_t>(found->count + 1) *
                         static_cast<std::uint32_t>(found->count_divisor));
  } else if (found->counter) {
    write_counter_at(target->subrecords[*found->counter].data, 0,
                     static_cast<std::uint32_t>(found->count + 1));
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
  if (!found->header_counted && !found->counter && found->count == 1) {
    return CountedArrayMutationStatus::would_empty;
  }

  auto& data = target->subrecords[found->payload].data;
  const auto first = data.begin() + static_cast<std::ptrdiff_t>(found->element_offset + index * array.stride);
  data.erase(first, first + static_cast<std::ptrdiff_t>(array.stride));
  if (found->header_counted) {
    write_counter_at(data, found->header_count_offset,
                     static_cast<std::uint32_t>(found->count - 1) *
                         static_cast<std::uint32_t>(found->count_divisor));
  } else if (found->counter) {
    write_counter_at(target->subrecords[*found->counter].data, 0,
                     static_cast<std::uint32_t>(found->count - 1));
  }
  mark_record_modified(plugin, *target);
  return CountedArrayMutationStatus::updated;
}

CountedArrayMutationStatus set_record_counted_element_hex(Plugin& plugin,
                                                          const FormKey& record_key,
                                                          const schema::CountedArrayDescriptor& array,
                                                          std::size_t index,
                                                          std::string_view hex) {
  assign_record_form_keys(plugin);
  auto* target = find_mutable_record(plugin, record_key);
  if (target == nullptr) return CountedArrayMutationStatus::record_missing;
  require_counted_array_record(*target, array);
  const auto bytes = bytes_of_hex<schema::CountedArrayError>(hex, array.path);
  if (bytes.size() != array.stride) {
    throw schema::CountedArrayError(std::string(array.path) + " element must be " + std::to_string(array.stride) +
                                    " bytes");
  }

  std::vector<std::byte>* data = nullptr;
  std::size_t base = 0;
  std::size_t count = 0;
  if (array.segment_count != 0) {
    const auto segment = read_segment(*target, array);
    if (!segment) return CountedArrayMutationStatus::subrecord_missing;
    for (auto& subrecord : target->subrecords) {
      if (&subrecord.data == segment->payload) data = &subrecord.data;
    }
    base = segment->span.base;
    count = segment->span.count;
  } else {
    const auto found = counted_array_subrecords(*target, array);
    if (!found) return CountedArrayMutationStatus::subrecord_missing;
    if (index == found->count) {
      const auto inserted = insert_record_counted_element(plugin, record_key, array, index);
      if (inserted != CountedArrayMutationStatus::updated) return inserted;
      target = find_mutable_record(plugin, record_key);
    }
    const auto after = counted_array_subrecords(*target, array);
    data = &target->subrecords[after->payload].data;
    count = after->count;
  }
  if (index >= count) return CountedArrayMutationStatus::index_out_of_range;
  const auto offset = base + index * array.stride;
  if (data == nullptr || offset + array.stride > data->size()) {
    throw schema::CountedArrayError(std::string(array.path) + " element lies outside its payload");
  }
  if (std::equal(bytes.begin(), bytes.end(), data->begin() + static_cast<std::ptrdiff_t>(offset))) {
    return CountedArrayMutationStatus::unchanged;
  }
  std::copy(bytes.begin(), bytes.end(), data->begin() + static_cast<std::ptrdiff_t>(offset));
  mark_record_modified(plugin, *target);
  return CountedArrayMutationStatus::updated;
}

std::optional<std::vector<SubrecordGroupSpan>> subrecord_group_spans(
    const Plugin& plugin,
    const FormKey& record_key,
    const schema::SubrecordGroupDescriptor& group,
    const SubrecordGroupSpan* within) {
  const auto* record = find_record(plugin, record_key);
  if (record == nullptr) return std::nullopt;
  require_subrecord_group_record(*record, group);
  return group_spans(*record, group, within);
}

std::vector<SubrecordGroupSpan> subrecord_group_spans(const Record& record,
                                                      const schema::SubrecordGroupDescriptor& group,
                                                      const SubrecordGroupSpan* within) {
  require_subrecord_group_record(record, group);
  return group_spans(record, group, within);
}

SubrecordGroupMutationStatus insert_record_subrecord_group(Plugin& plugin,
                                                           const FormKey& record_key,
                                                           const schema::SubrecordGroupDescriptor& group,
                                                           std::size_t index,
                                                           const SubrecordGroupSpan* within) {
  assign_record_form_keys(plugin);
  auto* target = find_mutable_record(plugin, record_key);
  if (target == nullptr) return SubrecordGroupMutationStatus::record_missing;
  require_subrecord_group_record(*target, group);
  if (!group.run.members.empty() && group.run.count_member == Signature{}) {
    throw schema::SubrecordGroupError(std::string(group.path) + " spans uncounted sub-runs");
  }
  const bool block = group.block.closer != Signature{};
  std::optional<BlockLayout> layout;
  std::vector<SubrecordGroupSpan> spans;
  std::optional<std::size_t> counter;
  if (block) {
    layout = block_layout(*target, group);
    if (layout) {
      spans = layout->spans;
      counter = layout->counter;
    }
  } else {
    spans = group_spans(*target, group, within);
    counter = group_counter(*target, group, spans.size());
  }
  const bool counted = group.count_subrecord != Signature{};
  if (counted && !counter && (!spans.empty() || group.requires_existing_count)) return SubrecordGroupMutationStatus::subrecord_missing;
  if (index > spans.size()) return SubrecordGroupMutationStatus::index_out_of_range;
  if (layout && index < spans.size()) require_unlinked(*target, group, *layout, index, false);
  // Constraint: a child with no successors has no insertion boundary except an existing scoped element, so an append elsewhere would land past a parent closer such as ALED or at the record end.
  if (!group.parent.empty() && group.successors.empty() && index == spans.size() &&
      (within == nullptr || spans.empty())) {
    throw schema::SubrecordGroupError(std::string(group.path) + " has no insertion boundary here");
  }

  const auto behind_after = [&] {
    if (!spans.empty()) return spans.back().end;
    for (std::size_t i = 0; i < target->subrecords.size(); ++i) {
      if (target->subrecords[i].signature == group.after) return i + 1;
    }
    return first_successor_index(*target, group.successors);
  };
  const std::size_t position = block                  ? (!layout                ? block_insert_position(*target, group)
                                                         : index < spans.size() ? spans[index].first
                                                                                : layout->closer)
                               : index < spans.size()  ? spans[index].first
                               : within != nullptr    ? (spans.empty() ? within->end : spans.back().end)
                               : group.after != Signature{} ? behind_after()
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
    created.push_back(default_subrecord(member, target->header.form_version));
  }
  if (block && !layout) {
    Subrecord closer;
    closer.signature = group.block.closer;
    created.push_back(std::move(closer));
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

SubrecordGroupMutationStatus set_record_subrecord_group_member(
    Plugin& plugin,
    const FormKey& record_key,
    const schema::SubrecordGroupDescriptor& group,
    std::size_t index,
    Signature member_signature,
    bool present,
    const SubrecordGroupSpan* within) {
  assign_record_form_keys(plugin);
  auto* target = find_mutable_record(plugin, record_key);
  if (target == nullptr) return SubrecordGroupMutationStatus::record_missing;
  require_subrecord_group_record(*target, group);
  if (!group.run.members.empty()) {
    throw schema::SubrecordGroupError(std::string(group.path) +
                                      " has nested runs and no member-level authoring");
  }

  std::optional<std::size_t> member_index;
  for (std::size_t m = 0; m < group.members.size(); ++m) {
    if (group.members[m].subrecord == member_signature) member_index = m;
  }
  if (!member_index.has_value()) {
    throw schema::SubrecordGroupError(std::string(group.path) + " has no " +
                                      member_signature.to_string() + " member");
  }
  const auto& descriptor = group.members[*member_index];
  if (descriptor.required) {
    throw schema::SubrecordGroupError(std::string(group.path) + " member " +
                                      member_signature.to_string() + " is required");
  }

  const auto spans = group_spans(*target, group, within);
  if (index >= spans.size()) return SubrecordGroupMutationStatus::index_out_of_range;
  const auto span = spans[index];
  std::optional<std::size_t> existing;
  std::size_t occurrences = 0;
  for (std::size_t position = span.first; position < span.end; ++position) {
    if (target->subrecords[position].signature != member_signature) continue;
    existing = position;
    ++occurrences;
  }

  if (!present) {
    if (!existing.has_value()) return SubrecordGroupMutationStatus::unchanged;
    // Constraint: a clear names one member, so a member that repeats inside the element refuses instead of dropping an arbitrary copy.
    if (occurrences > 1) {
      throw schema::SubrecordGroupError(std::string(group.path) + " member " +
                                        member_signature.to_string() + " appears " +
                                        std::to_string(occurrences) +
                                        " times and cannot be cleared as one");
    }
    target->subrecords.erase(target->subrecords.begin() +
                             static_cast<std::ptrdiff_t>(*existing));
    mark_record_modified(plugin, *target);
    return SubrecordGroupMutationStatus::updated;
  }
  if (existing.has_value()) return SubrecordGroupMutationStatus::unchanged;

  if (descriptor.localized && plugin.header.tes4_flags.is_localized()) {
    throw schema::SubrecordGroupError(std::string(group.path) + " member " +
                                      member_signature.to_string() +
                                      " cannot be inserted into a localized plugin");
  }
  // Invariant: a member sits at its declaration position, so it goes before the first present member declared after it.
  std::size_t insertion = span.end;
  for (std::size_t position = span.first; position < span.end; ++position) {
    std::optional<std::size_t> at;
    for (std::size_t m = 0; m < group.members.size(); ++m) {
      if (group.members[m].subrecord == target->subrecords[position].signature) at = m;
    }
    if (at.has_value() && *at > *member_index) {
      insertion = position;
      break;
    }
  }

  target->subrecords.insert(target->subrecords.begin() + static_cast<std::ptrdiff_t>(insertion),
                            default_subrecord(descriptor, target->header.form_version));
  mark_record_modified(plugin, *target);
  return SubrecordGroupMutationStatus::updated;
}

SubrecordGroupMutationStatus remove_record_subrecord_group(Plugin& plugin,
                                                           const FormKey& record_key,
                                                           const schema::SubrecordGroupDescriptor& group,
                                                           std::size_t index,
                                                           const SubrecordGroupSpan* within) {
  assign_record_form_keys(plugin);
  auto* target = find_mutable_record(plugin, record_key);
  if (target == nullptr) return SubrecordGroupMutationStatus::record_missing;
  require_subrecord_group_record(*target, group);
  if (!group.run.members.empty() && group.run.count_member == Signature{}) {
    throw schema::SubrecordGroupError(std::string(group.path) + " spans uncounted sub-runs");
  }
  if (group.block.closer != Signature{}) {
    const auto layout = block_layout(*target, group);
    if (!layout || index >= layout->spans.size()) return SubrecordGroupMutationStatus::index_out_of_range;
    require_unlinked(*target, group, *layout, index, true);
    auto& subrecords = target->subrecords;
    if (layout->spans.size() == 1) {
      subrecords.erase(subrecords.begin() + static_cast<std::ptrdiff_t>(layout->counter),
                       subrecords.begin() + static_cast<std::ptrdiff_t>(layout->closer) + 1);
    } else {
      write_counter(subrecords[layout->counter].data, static_cast<std::uint32_t>(layout->spans.size() - 1));
      const auto& gone = layout->spans[index];
      subrecords.erase(subrecords.begin() + static_cast<std::ptrdiff_t>(gone.first),
                       subrecords.begin() + static_cast<std::ptrdiff_t>(gone.end));
    }
    mark_record_modified(plugin, *target);
    return SubrecordGroupMutationStatus::updated;
  }
  const auto spans = group_spans(*target, group, within);
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

static SubrecordGroupMutationStatus element_hex_unguarded(Plugin& plugin,
                                                          const FormKey& record_key,
                                                          const schema::SubrecordGroupDescriptor& group,
                                                          std::size_t index,
                                                          std::string_view hex,
                                                          const SubrecordGroupSpan* within,
                                                          bool adopt_members) {
  assign_record_form_keys(plugin);
  auto* target = find_mutable_record(plugin, record_key);
  if (target == nullptr) return SubrecordGroupMutationStatus::record_missing;
  require_subrecord_group_record(*target, group);
  const auto wanted = bytes_of_hex<schema::SubrecordGroupError>(hex, group.path);

  auto spans = group_spans(*target, group, within);
  if (index >= spans.size()) return SubrecordGroupMutationStatus::index_out_of_range;
  std::optional<SubrecordGroupSpan> grown;
  if (within != nullptr) grown = *within;
  bool adopted_member = false;
  if (adopt_members && group.run.members.empty()) {
    const auto span = spans[index];
    std::size_t present_total = 0;
    bool has_variable = false;
    std::vector<const schema::SubrecordGroupMemberDescriptor*> absent;
    for (const auto& member : group.members) {
      bool present = false;
      for (std::size_t position = span.first; position < span.end; ++position) {
        if (target->subrecords[position].signature == member.subrecord) {
          present = true;
          if (member.payload_size == 0) has_variable = true;
          present_total += member.payload_size;
        }
      }
      if (!present && !member.required && member.payload_size != 0) absent.push_back(&member);
    }
    if (!has_variable && wanted.size() > present_total && absent.size() < 16) {
      std::optional<std::size_t> chosen;
      std::size_t fits = 0;
      for (std::size_t mask = 1; mask < (std::size_t{1} << absent.size()); ++mask) {
        std::size_t total = present_total;
        for (std::size_t bit = 0; bit < absent.size(); ++bit) {
          if ((mask >> bit) & 1u) total += absent[bit]->payload_size;
        }
        if (total == wanted.size()) {
          chosen = mask;
          ++fits;
        }
      }
      if (fits > 1) {
        throw schema::SubrecordGroupError(std::string(group.path) + " element text fits several optional member sets");
      }
      if (chosen) {
        for (std::size_t bit = 0; bit < absent.size(); ++bit) {
          if (((*chosen >> bit) & 1u) == 0) continue;
          const auto status = set_record_subrecord_group_member(plugin, record_key, group, index, absent[bit]->subrecord,
                                                                true, grown ? &*grown : nullptr);
          if (status != SubrecordGroupMutationStatus::updated) return status;
          adopted_member = true;
          if (grown) ++grown->end;
        }
        spans = group_spans(*target, group, grown ? &*grown : nullptr);
        if (index >= spans.size()) return SubrecordGroupMutationStatus::index_out_of_range;
      }
    }
  }
  within = grown ? &*grown : nullptr;
  {
    const auto span = spans[index];
    const auto fixed_size = [&](Signature signature) -> std::size_t {
      for (const auto& member : group.members) {
        if (member.subrecord == signature) return member.payload_size;
      }
      for (const auto& member : group.run.members) {
        if (member.subrecord == signature) return member.payload_size;
      }
      return 0;
    };
    std::vector<std::size_t> sizes;
    std::size_t fixed_total = 0;
    std::optional<std::size_t> variable;
    for (std::size_t position = span.first; position < span.end; ++position) {
      const auto size = fixed_size(target->subrecords[position].signature);
      if (size == 0) {
        if (variable) {
          throw schema::SubrecordGroupError(std::string(group.path) +
                                            " element has several variable-length subrecords, so its text cannot be split");
        }
        variable = sizes.size();
      }
      sizes.push_back(size);
      fixed_total += size;
    }
    if (variable ? wanted.size() < fixed_total : wanted.size() != fixed_total) {
      throw schema::SubrecordGroupError(std::string(group.path) + " element text has " + std::to_string(wanted.size()) +
                                        " bytes but the element's subrecords take " + std::to_string(fixed_total) +
                                        (variable ? " or more" : ""));
    }
    if (variable) sizes[*variable] = wanted.size() - fixed_total;

    std::vector<std::vector<std::byte>> previous;
    std::size_t cursor = 0;
    bool changed = false;
    for (std::size_t slot = 0; slot < sizes.size(); ++slot) {
      auto& data = target->subrecords[span.first + slot].data;
      previous.push_back(data);
      const auto first = wanted.begin() + static_cast<std::ptrdiff_t>(cursor);
      const std::vector<std::byte> piece(first, first + static_cast<std::ptrdiff_t>(sizes[slot]));
      cursor += sizes[slot];
      changed = changed || piece != data;
      data = piece;
    }
    const auto restore = [&] {
      for (std::size_t slot = 0; slot < previous.size(); ++slot) {
        target->subrecords[span.first + slot].data = previous[slot];
      }
    };
    try {
      const auto after = group_spans(*target, group, within);
      const bool same = after.size() == spans.size() &&
                        std::equal(after.begin(), after.end(), spans.begin(), [](const auto& a, const auto& b) {
                          return a.first == b.first && a.end == b.end && a.runs.size() == b.runs.size();
                        });
      if (!same) throw schema::SubrecordGroupError(std::string(group.path) + " element text changes the group layout");
    } catch (...) {
      restore();
      throw;
    }
    if (changed) mark_record_modified(plugin, *target);
    return changed || adopted_member ? SubrecordGroupMutationStatus::updated : SubrecordGroupMutationStatus::unchanged;
  }
}

SubrecordGroupMutationStatus set_record_subrecord_group_element_hex(Plugin& plugin,
                                                                    const FormKey& record_key,
                                                                    const schema::SubrecordGroupDescriptor& group,
                                                                    std::size_t index,
                                                                    std::string_view hex,
                                                                    const SubrecordGroupSpan* within,
                                                                    bool adopt_members,
                                                                    bool self_restoring) {
  if (!adopt_members || !self_restoring) {
    return element_hex_unguarded(plugin, record_key, group, index, hex, within, adopt_members);
  }
  assign_record_form_keys(plugin);
  const auto* original = find_record(plugin, record_key);
  if (original == nullptr) return SubrecordGroupMutationStatus::record_missing;
  const Record snapshot = *original;
  const auto plugin_range = plugin.original_range;
  const auto restore = [&] {
    *find_mutable_record(plugin, record_key) = snapshot;
    plugin.original_range = plugin_range;
  };
  try {
    const auto status = element_hex_unguarded(plugin, record_key, group, index, hex, within, true);
    if (status != SubrecordGroupMutationStatus::updated && status != SubrecordGroupMutationStatus::unchanged) restore();
    return status;
  } catch (...) {
    restore();
    throw;
  }
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
  if (group.run.members.empty()) {
    throw schema::SubrecordGroupError(std::string(group.path) + " has no sub-run");
  }
  const auto spans = group_spans(*target, group);
  if (run >= spans.size()) return SubrecordGroupMutationStatus::index_out_of_range;
  const auto& span = spans[run];
  if (index > span.runs.size()) return SubrecordGroupMutationStatus::index_out_of_range;
  std::optional<std::size_t> counter;
  if (group.run.count_member != Signature{}) {
    counter = group_run_counter(*target, span, group.run.count_member);
    if (*counter == target->subrecords.size()) {
      throw schema::SubrecordGroupError(std::string(group.path) + " has no " +
                                        group.run.count_member.to_string() + " run count");
    }
  }

  const std::size_t position = index < span.runs.size() ? span.runs[index].first
                               // Invariant: an uncounted span keeps its group members after its runs, so an emptied one fills from the front.
                               : span.runs.empty()      ? (counter ? *counter + 1 : span.first)
                                                        : span.runs.back().end;
  std::vector<Subrecord> created;
  for (const auto& member : group.run.members) {
    if (member.localized && plugin.header.tes4_flags.is_localized()) {
      throw schema::SubrecordGroupError(std::string(group.path) + " cannot be inserted into a localized plugin");
    }
    if (!member.required && member.default_payload.empty()) continue;
    created.push_back(default_subrecord(member, target->header.form_version));
  }
  target->subrecords.insert(target->subrecords.begin() + static_cast<std::ptrdiff_t>(position),
                            std::make_move_iterator(created.begin()), std::make_move_iterator(created.end()));
  if (counter) write_counter(target->subrecords[*counter].data, static_cast<std::uint32_t>(span.runs.size() + 1));
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
  if (group.run.members.empty()) {
    throw schema::SubrecordGroupError(std::string(group.path) + " has no sub-run");
  }
  const auto spans = group_spans(*target, group);
  if (run >= spans.size()) return SubrecordGroupMutationStatus::index_out_of_range;
  const auto& span = spans[run];
  if (index >= span.runs.size()) return SubrecordGroupMutationStatus::index_out_of_range;
  std::optional<std::size_t> counter;
  if (group.run.count_member != Signature{}) {
    counter = group_run_counter(*target, span, group.run.count_member);
    if (*counter == target->subrecords.size()) {
      throw schema::SubrecordGroupError(std::string(group.path) + " has no " +
                                        group.run.count_member.to_string() + " run count");
    }
  }

  if (counter) write_counter(target->subrecords[*counter].data, static_cast<std::uint32_t>(span.runs.size() - 1));
  const auto first = target->subrecords.begin() + static_cast<std::ptrdiff_t>(span.runs[index].first);
  target->subrecords.erase(first, first + static_cast<std::ptrdiff_t>(span.runs[index].end - span.runs[index].first));
  mark_record_modified(plugin, *target);
  return SubrecordGroupMutationStatus::updated;
}

}
