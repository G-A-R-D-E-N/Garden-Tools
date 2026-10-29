#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace fo4::avm1 {

enum class Avm1Status : std::uint8_t {
  Ok = 0,
  Truncated,
  LengthPastEnd,
  FieldsMismatch,
  EmptyPush,
  BadPushValue,
  MissingEnd,
  BranchOutOfRange,
  BranchNotRecord,
  TooDeep,
  BodyRecordsPastEnd,
  SizeTooLarge,
  BranchUnresolved,
  BranchDeltaOutOfRange,
};

const char* status_name(Avm1Status status) noexcept;

const char* action_mnemonic(std::uint8_t code) noexcept;

enum class Avm1ValueType : std::uint8_t {
  String = 0,
  Float = 1,
  Null = 2,
  Undefined = 3,
  Register = 4,
  Boolean = 5,
  Double = 6,
  Integer = 7,
  Constant8 = 8,
  Constant16 = 9,
};

struct Avm1Value {
  Avm1ValueType type{Avm1ValueType::Undefined};
  std::string text;
  std::uint32_t float_bits{0};
  std::uint64_t double_bits{0};
  std::int32_t integer{0};
  std::uint16_t index{0};
  bool boolean{false};
};

struct Avm1Raw {
  std::vector<std::byte> bytes;
};

struct Avm1Byte {
  std::uint8_t value{0};
};

struct Avm1Frame {
  std::uint16_t frame{0};
};

struct Avm1WaitForFrame {
  std::uint16_t frame{0};
  std::uint8_t skip{0};
};

struct Avm1Text {
  std::string text;
};

struct Avm1GetUrl {
  std::string url;
  std::string target;
};

struct Avm1GotoFrame2 {
  std::uint8_t flags{0};
  std::optional<std::uint16_t> scene_bias;
};

struct Avm1ConstantPool {
  std::vector<std::string> strings;
};

struct Avm1Push {
  std::vector<Avm1Value> values;
};

struct Avm1Branch {
  std::int16_t offset{0};
  std::size_t target{0};
};

struct Avm1Parameter {
  std::uint8_t register_index{0};
  std::string name;
};

struct Avm1Function {
  bool version2{false};
  std::string name;
  std::uint8_t register_count{0};
  std::uint16_t flags{0};
  std::vector<Avm1Parameter> parameters;
  std::uint16_t body_size{0};
};

struct Avm1With {
  std::uint16_t body_size{0};
};

struct Avm1Try {
  std::uint8_t flags{0};
  std::uint16_t try_size{0};
  std::uint16_t catch_size{0};
  std::uint16_t finally_size{0};
  std::string catch_name;
  std::uint8_t catch_register{0};
};

using Avm1Payload = std::variant<std::monostate, Avm1Raw, Avm1Byte, Avm1Frame, Avm1WaitForFrame,
                                 Avm1Text, Avm1GetUrl, Avm1GotoFrame2, Avm1ConstantPool,
                                 Avm1Push, Avm1Branch, Avm1Function, Avm1With, Avm1Try>;

inline constexpr std::size_t kNewRecordOffset = static_cast<std::size_t>(-1);

struct Avm1Action {
  std::size_t offset{kNewRecordOffset};
  std::size_t length{0};
  std::uint8_t code{0};
  bool unknown{false};
  Avm1Payload payload;
  std::array<std::size_t, 3> body_records{};
};

struct Avm1Stream {
  Avm1Status status{Avm1Status::Ok};
  std::size_t offset{0};
  std::string message;
  std::vector<Avm1Action> actions;
  std::vector<std::byte> trailing;
  std::size_t original_end{kNewRecordOffset};

  bool ok() const noexcept { return status == Avm1Status::Ok; }
};

Avm1Stream decode_actions(std::span<const std::byte> bytes);

/* Requires a stream from decode_actions, which keeps every record and the bytes after End. */
std::vector<std::byte> encode_actions(const Avm1Stream& stream);

std::vector<std::byte> encode_actions_checked(const Avm1Stream& stream, Avm1Status& status);

Avm1Status validate_actions(const Avm1Stream& stream);

bool insert_action(std::vector<Avm1Action>& actions, std::size_t index, Avm1Action action);

bool insert_action_into_body(std::vector<Avm1Action>& actions, std::size_t container_index,
                             std::size_t slot, std::size_t index, Avm1Action action);

bool erase_action(std::vector<Avm1Action>& actions, std::size_t index);

std::string disassemble_actions(const Avm1Stream& stream);

}  // namespace fo4::avm1
