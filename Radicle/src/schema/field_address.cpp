#include "fo4/schema/field_address.hpp"
#include "fo4/schema/scalar.hpp"
#include "fo4/schema/form_link_mixed.hpp"
#include "fo4/schema/behavior_layout.hpp"

#include <algorithm>
#include <optional>
#include <span>

namespace fo4::schema {
namespace {

std::optional<std::int64_t> read_discriminator(std::span<const std::byte> payload,
                                               std::size_t base,
                                               const FieldDiscriminator& when) {
  const auto width = discriminator_width(when.storage);
  if (base > payload.size() || when.offset > payload.size() - base) return std::nullopt;
  const auto at = base + when.offset;
  if (payload.size() - at < width) return std::nullopt;
  std::uint32_t raw = 0;
  for (std::size_t i = 0; i < width; ++i) {
    raw |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(payload[at + i])) << (8 * i);
  }
  if (when.storage == DiscriminatorStorage::i32) {
    return static_cast<std::int64_t>(static_cast<std::int32_t>(raw));
  }
  return static_cast<std::int64_t>(raw);
}

bool listed(const FieldDiscriminator& when, std::optional<std::int64_t> value) {
  if (!value.has_value()) return when.negated && when.match_absent;
  bool found = false;
  if (when.bits != 0) {
    found = (static_cast<std::uint64_t>(*value) & when.bits) != 0;
  } else {
    const auto count = std::min(when.count, when.equals.size());
    found = std::find(when.equals.begin(), when.equals.begin() + static_cast<std::ptrdiff_t>(count),
                      *value) != when.equals.begin() + static_cast<std::ptrdiff_t>(count);
    for (std::size_t index = 0; !found && index < when.extra_count; ++index) {
      found = when.extra[index] == *value;
    }
  }
  return found != when.negated;
}

bool record_discriminator_matches(const Record& record,
                                  const FieldDiscriminator& when,
                                  std::size_t ordinal) {
  std::size_t seen = 0;
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature != when.subrecord) continue;
    if (seen++ != ordinal) continue;
    return listed(when, read_discriminator(subrecord.data, 0, when));
  }
  return when.match_absent;
}

std::size_t count_occurrences(const Record& record, Signature signature) {
  std::size_t count = 0;
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature == signature) ++count;
  }
  return count;
}

std::size_t ordinal_of(const Record& record, Signature signature, std::size_t index) {
  std::size_t ordinal = 0;
  for (std::size_t i = 0; i < index && i < record.subrecords.size(); ++i) {
    if (record.subrecords[i].signature == signature) ++ordinal;
  }
  return ordinal;
}

}

bool record_discriminator_matches(const Record& record, const FieldDiscriminator& when) {
  if (count_occurrences(record, when.subrecord) > 1) return false;
  return record_discriminator_matches(record, when, 0);
}

std::optional<SegmentSpan> run_span(std::span<const std::byte> payload, const RunAddress& run) {
  if (run.count == 0 || run.count > run.sections.size() || run.index >= run.count ||
      run.start > payload.size()) {
    return std::nullopt;
  }
  std::size_t cursor = run.start;
  std::optional<SegmentSpan> ours;
  for (std::size_t i = 0; i < run.count; ++i) {
    const auto& section = run.sections[i];
    if (section.stride == 0) return std::nullopt;
    std::size_t elements = 1;
    const bool inline_count = section.count_width != 0 && section.count_offset == k_run_inline_count;
    if (section.count_width != 0) {
      const std::size_t at = inline_count ? cursor : section.count_offset;
      if (at + section.count_width > payload.size()) return std::nullopt;
      elements = 0;
      for (std::size_t byte = 0; byte < section.count_width; ++byte) {
        elements |= static_cast<std::size_t>(std::to_integer<std::uint8_t>(payload[at + byte])) << (8 * byte);
      }
    }
    if (inline_count) cursor += section.count_width;
    if (elements > (payload.size() - cursor) / section.stride) return std::nullopt;
    if (i == run.index) ours = SegmentSpan{cursor, elements};
    cursor += elements * section.stride;
  }
  if (cursor != payload.size()) return std::nullopt;
  return ours;
}

std::size_t discriminator_width(DiscriminatorStorage storage) noexcept {
  switch (storage) {
    case DiscriminatorStorage::u8:
      return 1;
    case DiscriminatorStorage::u16:
      return 2;
    case DiscriminatorStorage::u32:
    case DiscriminatorStorage::i32:
      return 4;
  }
  return 0;
}

FieldSlots resolve_field_slots(const Record& record,
                               const FieldShape& shape,
                               const FieldAddress& address) {
  FieldSlots out;
  std::vector<std::size_t> candidates;
  for (std::size_t i = 0; i < record.subrecords.size(); ++i) {
    if (record.subrecords[i].signature == shape.subrecord) candidates.push_back(i);
  }
  std::vector<std::vector<std::size_t>> scopes;
  if (auto scoped = behavior_field_candidate_scopes(record, shape.subrecord, address.repeated)) {
    scopes = std::move(*scoped);
  } else {
    scopes.push_back(std::move(candidates));
  }
  if (address.has_subrecord_occurrence) {
    // Constraint: an ordinal selects one copy of the subrecord, so RACE's male and female MODT
    // are separate payloads rather than one slot run.
    for (auto& scope : scopes) {
      scope.erase(std::remove_if(scope.begin(), scope.end(),
                                 [&](std::size_t index) {
                                   return ordinal_of(record, shape.subrecord, index) !=
                                          address.subrecord_occurrence;
                                 }),
                  scope.end());
    }
  }
  const bool several_in_one_scope =
      std::any_of(scopes.begin(), scopes.end(), [](const auto& scope) { return scope.size() > 1; });
  if (!address.repeated && several_in_one_scope) {
    out.duplicate = true;
    return out;
  }
  candidates.clear();
  for (const auto& scope : scopes) candidates.insert(candidates.end(), scope.begin(), scope.end());

  const auto& when = address.when;
  const bool guarded = discriminator_guarded(when);
  const bool external = guarded && when.subrecord.packed() != 0;
  std::size_t source_count = 0;
  std::size_t own_count = 0;
  if (external) {
    source_count = count_occurrences(record, when.subrecord);
    if (source_count > 1) own_count = count_occurrences(record, shape.subrecord);
  }
  const bool per_ordinal = source_count > 1;
  const bool ordinal_aligned = !per_ordinal || own_count == source_count;
  const bool record_guard = !external || per_ordinal || record_discriminator_matches(record, when, 0);

  const auto guard_passes = [&](std::size_t index, std::span<const std::byte> data, std::size_t base) {
    if (!guarded) return true;
    if (!external) return listed(when, read_discriminator(data, base, when));
    if (!per_ordinal) return record_guard;
    if (!ordinal_aligned) return false;
    return record_discriminator_matches(record, when, ordinal_of(record, shape.subrecord, index));
  };

  for (const auto index : candidates) {
    const auto& data = record.subrecords[index].data;
    const bool version_ok = record.header.form_version >= shape.min_form_version &&
                            (shape.max_form_version == 0 ||
                             record.header.form_version < shape.max_form_version);
    const bool behavior_ok = behavior_field_payload_valid(record, record.subrecords[index]);
    const bool size_ok = data.size() >= shape.minimum_payload_size &&
                         (shape.payload_size == 0 || data.size() == shape.payload_size);
    bool sized = version_ok && size_ok && behavior_ok;
    if (record.header.signature == Signature::from_string("NAVI") &&
        shape.subrecord == Signature::from_string("NVMI")) {
      sized = sized && navmesh_mixed_formid_offsets(record, record.subrecords[index]).has_value();
      const auto version = std::find_if(record.subrecords.begin(), record.subrecords.end(), [](const auto& sub) {
        return sub.signature == Signature::from_string("NVER");
      });
      sized = sized && version != record.subrecords.end() && version->data.size() == 4;
    }

    if (shape.whole_payload) {
      FieldSlot slot{index, 0, false, false};
      const bool passes = guard_passes(index, data, 0);
      if (shape.width != 0 && data.size() != shape.width && passes) {
        out.malformed = true;
      } else {
        slot.valid = sized && passes;
      }
      out.slots.push_back(slot);
      continue;
    }

    std::size_t first = 0;
    std::size_t count = 1;
    std::size_t step = 0;
    if (address.segment.count != 0) {
      const auto span = header_segment(data, record.header.signature, shape.subrecord, address);
      if (!span) continue;
      first = span->base;
      step = address.stride;
      count = span->count;
    } else if (address.run.count != 0) {
      const auto span = run_span(data, address.run);
      if (!span) {
        out.malformed = out.malformed || sized;
        continue;
      }
      first = span->base;
      step = address.stride;
      count = span->count;
    } else if (address.stride != 0) {
      if (data.size() < address.array_offset ||
          (data.size() - address.array_offset) % address.stride != 0) {
        out.misaligned = out.misaligned || sized;
        continue;
      }
      first = address.array_offset;
      step = address.stride;
      count = (data.size() - address.array_offset) / address.stride;
    }

    for (std::size_t element = 0; element < count; ++element) {
      const auto start = first + element * step;
      const auto offset = start + shape.offset;
      FieldSlot slot{index, offset, false, false};
      if (sized && guard_passes(index, data, start)) {
        if (offset > data.size() || data.size() - offset < shape.width) {
          slot.misfit = true;
          out.wrong_size = true;
        } else {
          slot.valid = true;
        }
      } else if (version_ok && behavior_ok && shape.payload_size != 0 &&
                 data.size() >= shape.minimum_payload_size && data.size() != shape.payload_size &&
                 guard_passes(index, data, start)) {
        // Constraint: a payload shorter than a row's minimum is a record without that optional
        // trailing field, not a size fault; only an exact size contract can be broken.
        out.wrong_size = true;
      }
      out.slots.push_back(slot);
    }
  }
  return out;
}

}
