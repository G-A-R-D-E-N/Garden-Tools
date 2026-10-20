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
  const auto count = std::min(when.count, when.equals.size());
  const bool found = std::find(when.equals.begin(), when.equals.begin() + static_cast<std::ptrdiff_t>(count),
                               *value) != when.equals.begin() + static_cast<std::ptrdiff_t>(count);
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
  const bool several_in_one_scope =
      std::any_of(scopes.begin(), scopes.end(), [](const auto& scope) { return scope.size() > 1; });
  if (!address.repeated && several_in_one_scope) {
    out.duplicate = true;
    return out;
  }
  candidates.clear();
  for (const auto& scope : scopes) candidates.insert(candidates.end(), scope.begin(), scope.end());

  const auto& when = address.when;
  const bool guarded = when.count != 0;
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
    bool sized = record.header.form_version >= shape.min_form_version &&
                 (shape.max_form_version == 0 || record.header.form_version < shape.max_form_version) &&
                 data.size() >= shape.minimum_payload_size &&
                 (shape.payload_size == 0 || data.size() == shape.payload_size) &&
                 behavior_field_payload_valid(record, record.subrecords[index]);
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
        } else {
          slot.valid = true;
        }
      }
      out.slots.push_back(slot);
    }
  }
  return out;
}

}
