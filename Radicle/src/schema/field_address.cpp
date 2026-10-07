#include "fo4/schema/field_address.hpp"
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
  if (!value.has_value()) return false;
  const auto count = std::min(when.count, when.equals.size());
  return std::find(when.equals.begin(), when.equals.begin() + static_cast<std::ptrdiff_t>(count),
                   *value) != when.equals.begin() + static_cast<std::ptrdiff_t>(count);
}

bool record_discriminator_matches(const Record& record, const FieldDiscriminator& when) {
  const Subrecord* found = nullptr;
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature != when.subrecord) continue;
    if (found != nullptr) return false;
    found = &subrecord;
  }
  if (found == nullptr) return false;
  return listed(when, read_discriminator(found->data, 0, when));
}

}  // namespace

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
  if (auto scoped = behavior_field_candidates(record, shape.subrecord)) candidates = std::move(*scoped);
  if (!address.repeated && candidates.size() > 1) {
    out.duplicate = true;
    return out;
  }

  const auto& when = address.when;
  const bool guarded = when.count != 0;
  const bool external = guarded && when.subrecord.packed() != 0;
  const bool external_ok = !external || record_discriminator_matches(record, when);

  for (const auto index : candidates) {
    const auto& data = record.subrecords[index].data;
    const bool sized = (shape.payload_size == 0 || data.size() == shape.payload_size) &&
                       behavior_field_payload_valid(record, record.subrecords[index]);

    if (shape.whole_payload) {
      FieldSlot slot{index, 0, false, false};
      if (shape.width != 0 && data.size() != shape.width) {
        out.malformed = true;
      } else {
        slot.valid = sized && external_ok &&
                     (!guarded || external || listed(when, read_discriminator(data, 0, when)));
      }
      out.slots.push_back(slot);
      continue;
    }

    std::size_t first = 0;
    std::size_t count = 1;
    std::size_t step = 0;
    if (address.stride != 0) {
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
      if (sized && external_ok && (!guarded || external || listed(when, read_discriminator(data, start, when)))) {
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

}  // namespace fo4::schema
