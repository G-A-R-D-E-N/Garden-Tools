#include <fo4/avm1/avm1.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace fo4::avm1 {

namespace {

using Bytes = std::vector<std::byte>;

constexpr std::size_t kFieldMax = 0xFFFFu;

struct Encoding {
  Bytes out;
  std::map<std::size_t, std::size_t> starts;
  std::vector<std::pair<std::size_t, std::size_t>> branches;
  std::size_t source_end{0};
  Avm1Status status{Avm1Status::Ok};
};

void note(Encoding& enc, Avm1Status status) {
  if (enc.status == Avm1Status::Ok) {
    enc.status = status;
  }
}

void put8(Bytes& out, std::uint8_t value) {
  out.push_back(static_cast<std::byte>(value));
}

void put16(Bytes& out, std::uint16_t value) {
  put8(out, static_cast<std::uint8_t>(value & 0xFFu));
  put8(out, static_cast<std::uint8_t>(value >> 8));
}

void put32(Bytes& out, std::uint32_t value) {
  put16(out, static_cast<std::uint16_t>(value & 0xFFFFu));
  put16(out, static_cast<std::uint16_t>(value >> 16));
}

void patch16(Bytes& out, std::size_t at, std::uint16_t value) {
  out[at] = static_cast<std::byte>(value & 0xFFu);
  out[at + 1] = static_cast<std::byte>(value >> 8);
}

void put_string(Bytes& out, const std::string& text) {
  for (const char ch : text) {
    put8(out, static_cast<std::uint8_t>(ch));
  }
  put8(out, 0);
}

void write_value(Bytes& out, const Avm1Value& value) {
  put8(out, static_cast<std::uint8_t>(value.type));
  switch (value.type) {
    case Avm1ValueType::String:
      put_string(out, value.text);
      return;
    case Avm1ValueType::Float:
      put32(out, value.float_bits);
      return;
    case Avm1ValueType::Null:
    case Avm1ValueType::Undefined:
      return;
    case Avm1ValueType::Register:
    case Avm1ValueType::Constant8:
      put8(out, static_cast<std::uint8_t>(value.index));
      return;
    case Avm1ValueType::Boolean:
      put8(out, value.boolean ? 1 : 0);
      return;
    case Avm1ValueType::Double:
      put32(out, static_cast<std::uint32_t>(value.double_bits >> 32));
      put32(out, static_cast<std::uint32_t>(value.double_bits & 0xFFFFFFFFu));
      return;
    case Avm1ValueType::Integer:
      put32(out, static_cast<std::uint32_t>(value.integer));
      return;
    case Avm1ValueType::Constant16:
      put16(out, value.index);
      return;
  }
}

void write_payload(Bytes&, std::monostate) {}

void write_payload(Bytes& out, const Avm1Raw& value) {
  out.insert(out.end(), value.bytes.begin(), value.bytes.end());
}

void write_payload(Bytes& out, const Avm1Byte& value) {
  put8(out, value.value);
}

void write_payload(Bytes& out, const Avm1Frame& value) {
  put16(out, value.frame);
}

void write_payload(Bytes& out, const Avm1WaitForFrame& value) {
  put16(out, value.frame);
  put8(out, value.skip);
}

void write_payload(Bytes& out, const Avm1Text& value) {
  put_string(out, value.text);
}

void write_payload(Bytes& out, const Avm1GetUrl& value) {
  put_string(out, value.url);
  put_string(out, value.target);
}

void write_payload(Bytes& out, const Avm1GotoFrame2& value) {
  put8(out, value.flags);
  if (value.scene_bias) {
    put16(out, *value.scene_bias);
  }
}

void write_payload(Bytes& out, const Avm1ConstantPool& value) {
  put16(out, static_cast<std::uint16_t>(value.strings.size()));
  for (const std::string& text : value.strings) {
    put_string(out, text);
  }
}

void write_payload(Bytes& out, const Avm1Push& value) {
  for (const Avm1Value& item : value.values) {
    write_value(out, item);
  }
}

void write_payload(Bytes& out, const Avm1Branch& value) {
  put16(out, static_cast<std::uint16_t>(value.offset));
}

void write_payload(Bytes& out, const Avm1Function& value) {
  put_string(out, value.name);
  put16(out, static_cast<std::uint16_t>(value.parameters.size()));
  if (value.version2) {
    put8(out, value.register_count);
    put16(out, value.flags);
  }
  for (const Avm1Parameter& parameter : value.parameters) {
    if (value.version2) {
      put8(out, parameter.register_index);
    }
    put_string(out, parameter.name);
  }
  put16(out, value.body_size);
}

void write_payload(Bytes& out, const Avm1With& value) {
  put16(out, value.body_size);
}

void write_payload(Bytes& out, const Avm1Try& value) {
  put8(out, value.flags);
  put16(out, value.try_size);
  put16(out, value.catch_size);
  put16(out, value.finally_size);
  if ((value.flags & 4) != 0) {
    put8(out, value.catch_register);
  } else {
    put_string(out, value.catch_name);
  }
}

std::size_t write_range(Encoding& enc, const std::vector<Avm1Action>& actions, std::size_t index,
                        std::size_t end);

std::size_t write_body(Encoding& enc, const std::vector<Avm1Action>& actions, std::size_t first,
                       std::size_t end, std::size_t size_at, std::size_t records) {
  if (records > end - first) {
    note(enc, Avm1Status::BodyRecordsPastEnd);
  }
  const std::size_t body_end = std::min(end, first + records);
  const std::size_t body_start = enc.out.size();
  write_range(enc, actions, first, body_end);
  const std::size_t size = enc.out.size() - body_start;
  if (size > kFieldMax) {
    note(enc, Avm1Status::SizeTooLarge);
  }
  patch16(enc.out, size_at, static_cast<std::uint16_t>(size));
  return body_end;
}

std::size_t write_record(Encoding& enc, const std::vector<Avm1Action>& actions, std::size_t index,
                         std::size_t end) {
  const Avm1Action& action = actions[index];
  const std::size_t start = enc.out.size();
  if (action.offset != kNewRecordOffset) {
    enc.starts.emplace(action.offset, start);
    enc.source_end = std::max(enc.source_end, action.offset + action.length);
  }
  put8(enc.out, action.code);
  if (action.code < 0x80) {
    return index + 1;
  }
  put16(enc.out, 0);
  const std::size_t payload = enc.out.size();
  std::visit([&enc](const auto& value) { write_payload(enc.out, value); }, action.payload);
  const std::size_t length = enc.out.size() - payload;
  if (length > kFieldMax) {
    note(enc, Avm1Status::SizeTooLarge);
  }
  patch16(enc.out, start + 1, static_cast<std::uint16_t>(length));
  if (const auto* branch = std::get_if<Avm1Branch>(&action.payload)) {
    enc.branches.emplace_back(payload, branch->target);
  }
  const std::size_t next = index + 1;
  if (std::holds_alternative<Avm1Function>(action.payload) ||
      std::holds_alternative<Avm1With>(action.payload)) {
    return write_body(enc, actions, next, end, enc.out.size() - 2, action.body_records[0]);
  }
  const auto* try_block = std::get_if<Avm1Try>(&action.payload);
  if (try_block == nullptr) {
    return next;
  }
  std::size_t body = 0;
  std::size_t cursor =
      write_body(enc, actions, next, end, payload + 1, action.body_records[body++]);
  if ((try_block->flags & 1) != 0) {
    cursor = write_body(enc, actions, cursor, end, payload + 3, action.body_records[body++]);
  }
  if ((try_block->flags & 2) != 0) {
    cursor = write_body(enc, actions, cursor, end, payload + 5, action.body_records[body++]);
  }
  return cursor;
}

std::size_t write_range(Encoding& enc, const std::vector<Avm1Action>& actions, std::size_t index,
                        std::size_t end) {
  while (index < end) {
    index = write_record(enc, actions, index, end);
  }
  return index;
}

Encoding layout(const Avm1Stream& stream) {
  Encoding enc;
  write_range(enc, stream.actions, 0, stream.actions.size());
  enc.out.insert(enc.out.end(), stream.trailing.begin(), stream.trailing.end());
  const std::size_t stream_end = enc.out.size();
  const std::size_t original_end = stream.original_end != kNewRecordOffset
                                       ? stream.original_end
                                       : enc.source_end + stream.trailing.size();
  for (const auto& [at, target] : enc.branches) {
    const auto found = enc.starts.lower_bound(target);
    const std::size_t dest = found == enc.starts.end() ? stream_end : found->second;
    const auto delta = static_cast<std::ptrdiff_t>(dest) - static_cast<std::ptrdiff_t>(at + 2);
    const auto narrow = static_cast<std::int16_t>(delta);
    if (narrow != delta) {
      note(enc, Avm1Status::BranchDeltaOutOfRange);
    }
    if (enc.starts.count(target) == 0 && target != original_end) {
      note(enc, Avm1Status::BranchUnresolved);
    }
    patch16(enc.out, at, static_cast<std::uint16_t>(narrow));
  }
  return enc;
}

}  // namespace

std::vector<std::byte> encode_actions(const Avm1Stream& stream) {
  return layout(stream).out;
}

std::vector<std::byte> encode_actions_checked(const Avm1Stream& stream, Avm1Status& status) {
  Encoding enc = layout(stream);
  status = enc.status;
  if (status != Avm1Status::Ok) {
    return {};
  }
  return std::move(enc.out);
}

Avm1Status validate_actions(const Avm1Stream& stream) {
  return layout(stream).status;
}

}  // namespace fo4::avm1
