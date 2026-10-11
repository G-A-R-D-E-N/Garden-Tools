#include "fo4/schema/ctda.hpp"

#include "fo4/signature.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace fo4 {
namespace {

constexpr Signature sig_citc = Signature::from_string("CITC");
constexpr Signature sig_cis1 = Signature::from_string("CIS1");
constexpr Signature sig_cis2 = Signature::from_string("CIS2");

// Offsets into the CTDA payload (after the 4-byte subrecord header). These
// match Radicle's existing CTDA remap table in form_links.cpp and Mutagen's
// Condition layout (see Condition.xml + the overlay field offsets).
constexpr std::size_t k_ctda_flags = 0;
constexpr std::size_t k_ctda_padding = 1;
constexpr std::size_t k_ctda_compare = 4;
constexpr std::size_t k_ctda_function = 8;
constexpr std::size_t k_ctda_unknown2 = 10;
constexpr std::size_t k_ctda_param1 = 12;
constexpr std::size_t k_ctda_param2 = 16;
constexpr std::size_t k_ctda_run_on = 20;
constexpr std::size_t k_ctda_reference = 24;
constexpr std::size_t k_ctda_unknown3 = 28;
constexpr std::size_t k_ctda_size = 32;

std::uint16_t read_u16(std::span<const std::byte> data, std::size_t offset) {
  std::uint16_t value{0};
  std::memcpy(&value, data.data() + offset, sizeof(value));
  return value;
}

std::uint32_t read_u32(std::span<const std::byte> data, std::size_t offset) {
  std::uint32_t value{0};
  std::memcpy(&value, data.data() + offset, sizeof(value));
  return value;
}

std::int32_t read_i32(std::span<const std::byte> data, std::size_t offset) {
  std::int32_t value{0};
  std::memcpy(&value, data.data() + offset, sizeof(value));
  return value;
}

void write_u16(std::span<std::byte> data, std::size_t offset, std::uint16_t value) {
  std::memcpy(data.data() + offset, &value, sizeof(value));
}

void write_u32(std::span<std::byte> data, std::size_t offset, std::uint32_t value) {
  std::memcpy(data.data() + offset, &value, sizeof(value));
}

void write_i32(std::span<std::byte> data, std::size_t offset, std::int32_t value) {
  std::memcpy(data.data() + offset, &value, sizeof(value));
}

std::string zstring_of(std::span<const std::byte> data) {
  std::string out;
  out.reserve(data.size());
  for (const auto byte : data) {
    const auto ch = static_cast<char>(std::to_integer<std::uint8_t>(byte));
    if (ch == '\0') break;
    out.push_back(ch);
  }
  return out;
}

std::vector<std::byte> zstring_bytes(std::string_view text) {
  std::vector<std::byte> out;
  out.reserve(text.size() + 1);
  for (const char ch : text) {
    out.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  }
  out.push_back(std::byte{0});
  return out;
}

}  // namespace

CtdaCondition parse_ctda_condition(std::span<const std::byte> payload) {
  if (payload.size() != k_ctda_size) {
    throw std::out_of_range("CTDA payload must be 32 bytes");
  }
  CtdaCondition condition;
  condition.flags_byte = std::to_integer<std::uint8_t>(payload[k_ctda_flags]);
  std::memcpy(condition.padding.data(), payload.data() + k_ctda_padding, condition.padding.size());
  condition.comparison_raw = read_u32(payload, k_ctda_compare);
  condition.function = read_u16(payload, k_ctda_function);
  condition.unknown2 = read_u16(payload, k_ctda_unknown2);
  condition.param1 = read_i32(payload, k_ctda_param1);
  condition.param2 = read_i32(payload, k_ctda_param2);
  condition.run_on = read_u32(payload, k_ctda_run_on);
  condition.reference = read_u32(payload, k_ctda_reference);
  condition.unknown3 = read_i32(payload, k_ctda_unknown3);
  return condition;
}

std::vector<std::byte> serialize_ctda_condition(const CtdaCondition& condition) {
  std::vector<std::byte> out(k_ctda_size);
  out[k_ctda_flags] = std::byte{condition.flags_byte};
  std::memcpy(out.data() + k_ctda_padding, condition.padding.data(), condition.padding.size());
  write_u32(out, k_ctda_compare, condition.comparison_raw);
  write_u16(out, k_ctda_function, condition.function);
  write_u16(out, k_ctda_unknown2, condition.unknown2);
  write_i32(out, k_ctda_param1, condition.param1);
  write_i32(out, k_ctda_param2, condition.param2);
  write_u32(out, k_ctda_run_on, condition.run_on);
  write_u32(out, k_ctda_reference, condition.reference);
  write_i32(out, k_ctda_unknown3, condition.unknown3);
  return out;
}

std::vector<Subrecord> serialize_ctda_run(std::span<const CtdaCondition> conditions) {
  std::vector<Subrecord> replacement;
  replacement.reserve(conditions.size() * 2 + 1);
  for (const auto& condition : conditions) {
    Subrecord ctda;
    ctda.signature = sig_ctda;
    ctda.data = serialize_ctda_condition(condition);
    replacement.push_back(std::move(ctda));
    if (condition.parameter1_string.has_value()) {
      Subrecord cis1;
      cis1.signature = sig_cis1;
      cis1.data = zstring_bytes(*condition.parameter1_string);
      replacement.push_back(std::move(cis1));
    }
    if (condition.parameter2_string.has_value()) {
      Subrecord cis2;
      cis2.signature = sig_cis2;
      cis2.data = zstring_bytes(*condition.parameter2_string);
      replacement.push_back(std::move(cis2));
    }
  }
  return replacement;
}

std::vector<CtdaCondition> read_ctda_conditions_in(const Record& record,
                                                   std::size_t begin,
                                                   std::size_t end) {
  std::vector<CtdaCondition> conditions;
  bool run_open = true;
  end = std::min(end, record.subrecords.size());
  for (std::size_t i = begin; i < end; ++i) {
    const auto& subrecord = record.subrecords[i];
    if (subrecord.signature == sig_ctda) {
      if (!run_open) throw CtdaMultipleListsError("condition scope has more than one list");
      conditions.push_back(parse_ctda_condition(subrecord.data));
    } else if (run_open && !conditions.empty() &&
               (subrecord.signature == sig_cis1 || subrecord.signature == sig_cis2)) {
      auto& last = conditions.back();
      if (subrecord.signature == sig_cis1) {
        last.parameter1_string = zstring_of(subrecord.data);
      } else {
        last.parameter2_string = zstring_of(subrecord.data);
      }
    } else if (!conditions.empty()) {
      run_open = false;
    }
  }
  return conditions;
}

std::vector<CtdaCondition> read_ctda_conditions(const Record& record) {
  return read_ctda_conditions_in(record, 0, record.subrecords.size());
}

CtdaScopeWrite write_ctda_conditions_in(Record& record,
                                        std::size_t begin,
                                        std::size_t end,
                                        std::span<const CtdaCondition> conditions,
                                        std::optional<std::size_t> insert_anchor) {
  end = std::min(end, record.subrecords.size());
  std::size_t first = end;
  std::size_t last = end;
  std::size_t citc = end;
  bool run_open = true;
  for (std::size_t i = begin; i < end; ++i) {
    const auto& sub = record.subrecords[i];
    if (sub.signature == sig_ctda) {
      if (first == end) {
        first = i;
        if (i > begin && record.subrecords[i - 1].signature == sig_citc &&
            record.subrecords[i - 1].data.size() == 4) {
          citc = i - 1;
        }
      } else if (!run_open) {
        return CtdaScopeWrite::multiple_lists;
      }
      last = i;
    } else if (first != end && run_open &&
               (sub.signature == sig_cis1 || sub.signature == sig_cis2)) {
      last = i;
    } else if (first != end) {
      run_open = false;
    }
  }

  auto replacement = serialize_ctda_run(conditions);

  if (first == end) {
    if (conditions.empty()) return CtdaScopeWrite::no_op;
    if (!insert_anchor.has_value()) return CtdaScopeWrite::no_list;
    const std::size_t anchor = std::min(*insert_anchor, record.subrecords.size());
    record.subrecords.insert(
        record.subrecords.begin() + static_cast<std::ptrdiff_t>(anchor),
        std::make_move_iterator(replacement.begin()),
        std::make_move_iterator(replacement.end()));
    return CtdaScopeWrite::inserted;
  }

  const bool has_citc = citc < first;
  record.subrecords.erase(
      record.subrecords.begin() + static_cast<std::ptrdiff_t>(first),
      record.subrecords.begin() + static_cast<std::ptrdiff_t>(last + 1));

  record.subrecords.insert(
      record.subrecords.begin() + static_cast<std::ptrdiff_t>(first),
      std::make_move_iterator(replacement.begin()),
      std::make_move_iterator(replacement.end()));

  if (has_citc) {
    std::uint32_t count = static_cast<std::uint32_t>(conditions.size());
    std::memcpy(record.subrecords[first - 1].data.data(), &count, sizeof(count));
  }
  return CtdaScopeWrite::replaced;
}

bool write_ctda_conditions(Record& record, std::span<const CtdaCondition> conditions) {
  return write_ctda_conditions_in(record, 0, record.subrecords.size(), conditions,
                                  std::nullopt) == CtdaScopeWrite::replaced;
}

}  // namespace fo4
