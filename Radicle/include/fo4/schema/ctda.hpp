#pragma once

#include "fo4/formid.hpp"
#include "fo4/plugin/container.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace fo4 {

class CtdaMultipleListsError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

class CtdaNotLosslessError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

inline constexpr std::uint16_t k_ctda_event_function_index = 576;

// Condition.Flag bits (Mutagen): OR=0x01, ParametersUseAliases=0x02,
// UseGlobal=0x04, UsePackData=0x08, SwapSubjectAndTarget=0x10.
inline constexpr std::uint8_t k_ctda_flag_or = 0x01;
inline constexpr std::uint8_t k_ctda_flag_aliases = 0x02;
inline constexpr std::uint8_t k_ctda_flag_use_global = 0x04;
inline constexpr std::uint8_t k_ctda_flag_pack_data = 0x08;
inline constexpr std::uint8_t k_ctda_flag_swap = 0x10;

// A parsed CTDA condition. Offsets are relative to the 32-byte subrecord
// payload (after the 4-byte header): flags/operator byte @0, padding @1-3,
// comparison value @4, function @8 (u16), unknown2/member @10,
// param1/parameter3 @12, param2 @16, run-on @20, reference @24, unknown3 @28.
struct CtdaCondition {
  // Raw flags byte: low 5 bits are Condition.Flag, high 3 bits the compare
  // operator (EqualTo=0, NotEqualTo=1, GreaterThan=2, GreaterThanOrEqualTo=3,
  // LessThan=4, LessThanOrEqualTo=5).
  std::uint8_t flags_byte{0};

  std::array<std::byte, 3> padding{};

  // Comparison value: float bits for float conditions, a raw FormID for
  // UseGlobal conditions. Interpretation is the caller's (the JSON contract
  // distinguishes via the use-global flag).
  std::uint32_t comparison_raw{0};

  std::uint16_t function{0};
  std::uint16_t unknown2{0};
  std::int32_t param1{0};
  std::int32_t param2{0};
  std::uint32_t run_on{0};
  std::uint32_t reference{0};
  std::int32_t unknown3{-1};

  // Strings from the CIS1/CIS2 subrecords that follow the CTDA in the record.
  std::optional<std::string> parameter1_string;
  std::optional<std::string> parameter2_string;

  // Low 5 bits of the flags byte.
  [[nodiscard]] std::uint8_t flags() const noexcept {
    return flags_byte & 0x1Fu;
  }
  // High 3 bits of the flags byte.
  [[nodiscard]] std::uint8_t compare_operator() const noexcept {
    return static_cast<std::uint8_t>((flags_byte >> 5) & 0x7u);
  }
  [[nodiscard]] bool use_global() const noexcept {
    return (flags_byte & k_ctda_flag_use_global) != 0;
  }
  [[nodiscard]] bool is_event_data() const noexcept {
    return function == k_ctda_event_function_index;
  }

  // Float interpretation of the comparison value.
  [[nodiscard]] float comparison_float() const noexcept {
    float value;
    static_assert(sizeof(value) == sizeof(comparison_raw));
    std::memcpy(&value, &comparison_raw, sizeof(value));
    return value;
  }
};

// Parses one CTDA subrecord payload into a condition. Throws
// std::out_of_range when the payload is shorter than the layout requires.
CtdaCondition parse_ctda_condition(std::span<const std::byte> payload);

// Serializes a condition back to a CTDA subrecord payload.
std::vector<std::byte> serialize_ctda_condition(const CtdaCondition& condition);

std::vector<Subrecord> serialize_ctda_run(std::span<const CtdaCondition> conditions);

std::vector<CtdaCondition> read_ctda_conditions(const Record& record);

std::vector<CtdaCondition> read_ctda_conditions_in(const Record& record,
                                                   std::size_t begin,
                                                   std::size_t end);

enum class CtdaScopeWrite {
  replaced,
  inserted,
  no_op,
  no_list,
  multiple_lists,
};

CtdaScopeWrite write_ctda_conditions_in(Record& record,
                                        std::size_t begin,
                                        std::size_t end,
                                        std::span<const CtdaCondition> conditions,
                                        std::optional<std::size_t> insert_anchor);

bool write_ctda_conditions(Record& record, std::span<const CtdaCondition> conditions);

}  // namespace fo4
