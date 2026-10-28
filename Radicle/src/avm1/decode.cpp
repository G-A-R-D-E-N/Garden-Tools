#include <fo4/avm1/avm1.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace fo4::avm1 {

const char* status_name(Avm1Status status) noexcept {
  switch (status) {
    case Avm1Status::Ok:
      return "ok";
    case Avm1Status::Truncated:
      return "truncated";
    case Avm1Status::LengthPastEnd:
      return "length past end";
    case Avm1Status::FieldsMismatch:
      return "fields do not fill the record length";
    case Avm1Status::EmptyPush:
      return "empty push";
    case Avm1Status::BadPushValue:
      return "bad push value";
    case Avm1Status::MissingEnd:
      return "missing end";
    case Avm1Status::BranchOutOfRange:
      return "branch out of range";
    case Avm1Status::BranchNotRecord:
      return "branch target is not a record start";
    case Avm1Status::TooDeep:
      return "nested bodies are too deep";
  }
  return "unknown";
}

namespace {

std::uint8_t byte_at(std::span<const std::byte> bytes, std::size_t pos) noexcept {
  return std::to_integer<std::uint8_t>(bytes[pos]);
}

constexpr std::size_t kMaxBodyDepth = 64;

class Decoder {
public:
  explicit Decoder(std::span<const std::byte> bytes) noexcept : bytes_(bytes) {}

  void run();
  Avm1Stream take() { return std::move(out_); }

private:
  bool block(std::size_t end, bool top);
  bool record(std::size_t end);
  bool payload(Avm1Action& action);
  bool body(std::size_t size, std::size_t end);
  bool function_header(Avm1Function& fn, bool version2);
  bool try_header(Avm1Try& try_block);
  bool push_values(std::size_t offset, Avm1Push& push);
  bool resolve_branches();
  bool read8(std::uint8_t& out);
  bool read16(std::uint16_t& out);
  bool read32(std::uint32_t& out);
  bool read_string(std::string& out);
  bool fail(Avm1Status status, std::size_t offset, const std::string& detail);

  std::span<const std::byte> bytes_;
  std::size_t pos_{0};
  std::size_t limit_{0};
  std::size_t depth_{0};
  Avm1Stream out_;
};

void Decoder::run() {
  if (block(bytes_.size(), true)) {
    const auto rest = bytes_.subspan(pos_);
    out_.trailing.assign(rest.begin(), rest.end());
    resolve_branches();
  }
  if (!out_.ok()) {
    out_.actions.clear();
  }
}

bool Decoder::block(std::size_t end, bool top) {
  while (pos_ < end) {
    const bool is_end = byte_at(bytes_, pos_) == 0;
    if (!record(end)) {
      return false;
    }
    if (top && is_end) {
      return true;
    }
  }
  if (top) {
    return fail(Avm1Status::MissingEnd, end, "the stream ends before an End record");
  }
  return true;
}

bool Decoder::record(std::size_t end) {
  const std::size_t start = pos_;
  Avm1Action action;
  action.offset = start;
  action.code = byte_at(bytes_, pos_);
  pos_ += 1;
  if (action.code < 0x80) {
    action.length = 1;
    action.unknown = action_mnemonic(action.code) == nullptr;
  } else {
    limit_ = end;
    std::uint16_t length = 0;
    if (!read16(length)) {
      return false;
    }
    if (length > end - pos_) {
      return fail(Avm1Status::LengthPastEnd, start,
                  "the record declares " + std::to_string(length) + " bytes but its block has " +
                      std::to_string(end - pos_) + " left");
    }
    limit_ = pos_ + length;
    action.length = 3 + static_cast<std::size_t>(length);
    if (!payload(action)) {
      return false;
    }
    if (pos_ != limit_) {
      return fail(Avm1Status::FieldsMismatch, pos_,
                  "the fields of the record at offset " + std::to_string(start) +
                      " end before its declared length");
    }
  }
  std::size_t bodies[3] = {};
  std::size_t count = 0;
  if (const auto* fn = std::get_if<Avm1Function>(&action.payload)) {
    bodies[count++] = fn->body_size;
  } else if (const auto* with = std::get_if<Avm1With>(&action.payload)) {
    bodies[count++] = with->body_size;
  } else if (const auto* try_block = std::get_if<Avm1Try>(&action.payload)) {
    bodies[count++] = try_block->try_size;
    if ((try_block->flags & 1) != 0) {
      bodies[count++] = try_block->catch_size;
    }
    if ((try_block->flags & 2) != 0) {
      bodies[count++] = try_block->finally_size;
    }
  }
  const std::size_t owner = out_.actions.size();
  out_.actions.push_back(std::move(action));
  for (std::size_t index = 0; index < count; ++index) {
    const std::size_t first = out_.actions.size();
    if (!body(bodies[index], end)) {
      return false;
    }
    out_.actions[owner].body_records[index] = out_.actions.size() - first;
  }
  return true;
}

bool Decoder::body(std::size_t size, std::size_t end) {
  if (size > end - pos_) {
    return fail(Avm1Status::LengthPastEnd, pos_,
                "a body of " + std::to_string(size) + " bytes runs past the end of its block");
  }
  if (depth_ == kMaxBodyDepth) {
    return fail(Avm1Status::TooDeep, pos_,
                "more than " + std::to_string(kMaxBodyDepth) + " levels of body");
  }
  depth_ += 1;
  const bool parsed = block(pos_ + size, false);
  depth_ -= 1;
  return parsed;
}

bool Decoder::payload(Avm1Action& action) {
  switch (action.code) {
    case 0x81: {
      Avm1Frame value;
      if (!read16(value.frame)) {
        return false;
      }
      action.payload = value;
      return true;
    }
    case 0x83: {
      Avm1GetUrl value;
      if (!read_string(value.url) || !read_string(value.target)) {
        return false;
      }
      action.payload = std::move(value);
      return true;
    }
    case 0x87:
    case 0x89:
    case 0x8D:
    case 0x9A: {
      Avm1Byte value;
      if (!read8(value.value)) {
        return false;
      }
      action.payload = value;
      return true;
    }
    case 0x88: {
      Avm1ConstantPool value;
      std::uint16_t count = 0;
      if (!read16(count)) {
        return false;
      }
      if (count > limit_ - pos_) {
        return fail(Avm1Status::Truncated, pos_,
                    "the string count is larger than the bytes left in its record");
      }
      value.strings.resize(count);
      for (std::string& text : value.strings) {
        if (!read_string(text)) {
          return false;
        }
      }
      action.payload = std::move(value);
      return true;
    }
    case 0x8A: {
      Avm1WaitForFrame value;
      if (!read16(value.frame) || !read8(value.skip)) {
        return false;
      }
      action.payload = value;
      return true;
    }
    case 0x8B:
    case 0x8C: {
      Avm1Text value;
      if (!read_string(value.text)) {
        return false;
      }
      action.payload = std::move(value);
      return true;
    }
    case 0x8E:
    case 0x9B: {
      Avm1Function value;
      if (!function_header(value, action.code == 0x8E)) {
        return false;
      }
      action.payload = std::move(value);
      return true;
    }
    case 0x8F: {
      Avm1Try value;
      if (!try_header(value)) {
        return false;
      }
      action.payload = std::move(value);
      return true;
    }
    case 0x94: {
      Avm1With value;
      if (!read16(value.body_size)) {
        return false;
      }
      action.payload = value;
      return true;
    }
    case 0x96: {
      Avm1Push value;
      if (!push_values(action.offset, value)) {
        return false;
      }
      action.payload = std::move(value);
      return true;
    }
    case 0x99:
    case 0x9D: {
      std::uint16_t raw = 0;
      if (!read16(raw)) {
        return false;
      }
      Avm1Branch value;
      value.offset = static_cast<std::int16_t>(raw);
      action.payload = value;
      return true;
    }
    case 0x9E:
      return true;
    case 0x9F: {
      Avm1GotoFrame2 value;
      if (!read8(value.flags)) {
        return false;
      }
      if ((value.flags & 2) != 0) {
        std::uint16_t bias = 0;
        if (!read16(bias)) {
          return false;
        }
        value.scene_bias = bias;
      }
      action.payload = value;
      return true;
    }
    default: {
      Avm1Raw value;
      const auto rest = bytes_.subspan(pos_, limit_ - pos_);
      value.bytes.assign(rest.begin(), rest.end());
      pos_ = limit_;
      action.payload = std::move(value);
      action.unknown = true;
      return true;
    }
  }
}

bool Decoder::function_header(Avm1Function& fn, bool version2) {
  std::uint16_t count = 0;
  fn.version2 = version2;
  if (!read_string(fn.name) || !read16(count)) {
    return false;
  }
  if (version2 && (!read8(fn.register_count) || !read16(fn.flags))) {
    return false;
  }
  if (count > limit_ - pos_) {
    return fail(Avm1Status::Truncated, pos_,
                "the parameter count is larger than the bytes left in its record");
  }
  fn.parameters.resize(count);
  for (Avm1Parameter& parameter : fn.parameters) {
    if (version2 && !read8(parameter.register_index)) {
      return false;
    }
    if (!read_string(parameter.name)) {
      return false;
    }
  }
  return read16(fn.body_size);
}

bool Decoder::try_header(Avm1Try& try_block) {
  if (!read8(try_block.flags) || !read16(try_block.try_size) || !read16(try_block.catch_size) ||
      !read16(try_block.finally_size)) {
    return false;
  }
  if ((try_block.flags & 4) != 0) {
    return read8(try_block.catch_register);
  }
  return read_string(try_block.catch_name);
}

bool Decoder::push_values(std::size_t offset, Avm1Push& push) {
  if (pos_ == limit_) {
    return fail(Avm1Status::EmptyPush, offset, "the Push record has no value");
  }
  while (pos_ < limit_) {
    const std::size_t type_offset = pos_;
    std::uint8_t type = 0;
    if (!read8(type)) {
      return false;
    }
    if (type > static_cast<std::uint8_t>(Avm1ValueType::Constant16)) {
      return fail(Avm1Status::BadPushValue, type_offset,
                  "the Push value type " + std::to_string(type) + " is not defined");
    }
    Avm1Value value;
    value.type = static_cast<Avm1ValueType>(type);
    std::uint8_t small = 0;
    std::uint32_t high = 0;
    std::uint32_t low = 0;
    switch (value.type) {
      case Avm1ValueType::String:
        if (!read_string(value.text)) {
          return false;
        }
        break;
      case Avm1ValueType::Float:
        if (!read32(value.float_bits)) {
          return false;
        }
        break;
      case Avm1ValueType::Null:
      case Avm1ValueType::Undefined:
        break;
      case Avm1ValueType::Register:
        if (!read8(small)) {
          return false;
        }
        value.index = small;
        break;
      case Avm1ValueType::Boolean:
        if (!read8(small)) {
          return false;
        }
        if (small > 1) {
          return fail(Avm1Status::BadPushValue, type_offset,
                      "the Push Boolean value " + std::to_string(small) + " is not 0 or 1");
        }
        value.boolean = small == 1;
        break;
      case Avm1ValueType::Double:
        if (!read32(high) || !read32(low)) {
          return false;
        }
        value.double_bits = (static_cast<std::uint64_t>(high) << 32) | low;
        break;
      case Avm1ValueType::Integer:
        if (!read32(low)) {
          return false;
        }
        value.integer = static_cast<std::int32_t>(low);
        break;
      case Avm1ValueType::Constant8:
        if (!read8(small)) {
          return false;
        }
        value.index = small;
        break;
      case Avm1ValueType::Constant16:
        if (!read16(value.index)) {
          return false;
        }
        break;
    }
    push.values.push_back(std::move(value));
  }
  return true;
}

bool Decoder::resolve_branches() {
  const auto size = static_cast<std::ptrdiff_t>(bytes_.size());
  std::vector<bool> boundary(bytes_.size() + 1, false);
  for (const Avm1Action& action : out_.actions) {
    boundary[action.offset] = true;
  }
  boundary[bytes_.size()] = true;
  for (Avm1Action& action : out_.actions) {
    auto* branch = std::get_if<Avm1Branch>(&action.payload);
    if (branch == nullptr) {
      continue;
    }
    const auto target =
        static_cast<std::ptrdiff_t>(action.offset + action.length) + branch->offset;
    if (target < 0 || target > size) {
      return fail(Avm1Status::BranchOutOfRange, action.offset,
                  "the branch targets " + std::to_string(target) + ", outside the " +
                      std::to_string(size) + "-byte stream");
    }
    if (!boundary[static_cast<std::size_t>(target)]) {
      return fail(Avm1Status::BranchNotRecord, action.offset,
                  "the branch targets " + std::to_string(target) +
                      ", which is not the start of a record");
    }
    branch->target = static_cast<std::size_t>(target);
  }
  return true;
}

bool Decoder::read8(std::uint8_t& out) {
  if (limit_ - pos_ < 1) {
    return fail(Avm1Status::Truncated, pos_, "a field runs past the end of its record");
  }
  out = byte_at(bytes_, pos_);
  pos_ += 1;
  return true;
}

bool Decoder::read16(std::uint16_t& out) {
  if (limit_ - pos_ < 2) {
    return fail(Avm1Status::Truncated, pos_, "a field runs past the end of its record");
  }
  out = static_cast<std::uint16_t>(byte_at(bytes_, pos_) | (byte_at(bytes_, pos_ + 1) << 8));
  pos_ += 2;
  return true;
}

bool Decoder::read32(std::uint32_t& out) {
  if (limit_ - pos_ < 4) {
    return fail(Avm1Status::Truncated, pos_, "a field runs past the end of its record");
  }
  out = 0;
  for (std::size_t index = 0; index < 4; ++index) {
    out |= static_cast<std::uint32_t>(byte_at(bytes_, pos_ + index)) << (8 * index);
  }
  pos_ += 4;
  return true;
}

bool Decoder::read_string(std::string& out) {
  std::size_t end = pos_;
  while (end < limit_ && byte_at(bytes_, end) != 0) {
    ++end;
  }
  if (end == limit_) {
    return fail(Avm1Status::Truncated, pos_, "a string is not NUL-terminated inside its record");
  }
  out.assign(reinterpret_cast<const char*>(bytes_.data() + pos_), end - pos_);
  pos_ = end + 1;
  return true;
}

bool Decoder::fail(Avm1Status status, std::size_t offset, const std::string& detail) {
  out_.status = status;
  out_.offset = offset;
  out_.message = "offset " + std::to_string(offset) + ": " + detail;
  return false;
}

}  // namespace

Avm1Stream decode_actions(std::span<const std::byte> bytes) {
  Decoder decoder(bytes);
  decoder.run();
  return decoder.take();
}

}  // namespace fo4::avm1
