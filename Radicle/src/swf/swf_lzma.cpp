#include "swf_internal.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace fo4::swf::detail {

namespace {

constexpr std::uint32_t kTopValue = 1u << 24;
constexpr unsigned kBitModelTotalBits = 11;
constexpr unsigned kMoveBits = 5;
constexpr std::uint16_t kProbInit = 1024;
constexpr unsigned kNumStates = 12;
constexpr unsigned kPosBitsMax = 4;
constexpr unsigned kPosStates = 1u << kPosBitsMax;
constexpr unsigned kLenToPosStates = 4;
constexpr unsigned kEndPosModelIndex = 14;
constexpr unsigned kNumFullDistances = 128;
constexpr unsigned kAlignBits = 4;
constexpr unsigned kMatchMinLen = 2;
constexpr std::size_t kMatchMaxLen = 273;
constexpr std::uint32_t kEndMarker = 0xFFFFFFFFu;
constexpr std::size_t kMaxOverrun = 5;
constexpr unsigned kLiteralCoderSize = 0x300;
constexpr unsigned kMaxProperties = 9 * 5 * 5;
constexpr std::uint32_t kEncoderDictionary = 1u << 24;
constexpr unsigned kEncodeLc = 3;
constexpr unsigned kEncodePb = 2;
constexpr unsigned kHashBits = 16;
constexpr std::size_t kNoPosition = std::numeric_limits<std::size_t>::max();

std::vector<std::uint16_t> make_probs(std::size_t count) {
  return std::vector<std::uint16_t>(count, kProbInit);
}

struct LengthModel {
  std::uint16_t choice{kProbInit};
  std::uint16_t choice2{kProbInit};
  std::vector<std::uint16_t> low = make_probs(kPosStates << 3);
  std::vector<std::uint16_t> mid = make_probs(kPosStates << 3);
  std::vector<std::uint16_t> high = make_probs(256);
};

struct Model {
  explicit Model(unsigned literal_bits) : literal(make_probs(std::size_t{kLiteralCoderSize} << literal_bits)) {}

  std::vector<std::uint16_t> literal;
  std::vector<std::uint16_t> is_match = make_probs(kNumStates << kPosBitsMax);
  std::vector<std::uint16_t> is_rep = make_probs(kNumStates);
  std::vector<std::uint16_t> is_rep_g0 = make_probs(kNumStates);
  std::vector<std::uint16_t> is_rep_g1 = make_probs(kNumStates);
  std::vector<std::uint16_t> is_rep_g2 = make_probs(kNumStates);
  std::vector<std::uint16_t> is_rep0_long = make_probs(kNumStates << kPosBitsMax);
  std::vector<std::uint16_t> pos_slot = make_probs(kLenToPosStates << 6);
  std::vector<std::uint16_t> pos_special = make_probs(1 + kNumFullDistances - kEndPosModelIndex);
  std::vector<std::uint16_t> align = make_probs(1u << kAlignBits);
  LengthModel match_len;
  LengthModel rep_len;
};

class RangeDecoder {
public:
  explicit RangeDecoder(std::span<const std::byte> input) noexcept : input_(input) {}

  bool start() noexcept {
    if (input_.empty() || input_[0] != std::byte{0}) {
      return false;
    }
    position_ = 1;
    for (unsigned index = 0; index < 4; ++index) {
      code_ = (code_ << 8) | next_byte();
    }
    return true;
  }

  bool exhausted() const noexcept { return overrun_ > kMaxOverrun; }

  unsigned bit(std::uint16_t& probability) noexcept {
    const std::uint32_t bound = (range_ >> kBitModelTotalBits) * probability;
    unsigned value = 0;
    if (code_ < bound) {
      range_ = bound;
      probability = static_cast<std::uint16_t>(
          probability + (((1u << kBitModelTotalBits) - probability) >> kMoveBits));
    } else {
      range_ -= bound;
      code_ -= bound;
      probability = static_cast<std::uint16_t>(probability - (probability >> kMoveBits));
      value = 1;
    }
    normalize();
    return value;
  }

  std::uint32_t direct_bits(unsigned count) noexcept {
    std::uint32_t value = 0;
    for (unsigned index = 0; index < count; ++index) {
      range_ >>= 1;
      std::uint32_t current = 0;
      if (code_ >= range_) {
        code_ -= range_;
        current = 1;
      }
      value = (value << 1) | current;
      normalize();
    }
    return value;
  }

private:
  void normalize() noexcept {
    if (range_ < kTopValue) {
      range_ <<= 8;
      code_ = (code_ << 8) | next_byte();
    }
  }

  std::uint32_t next_byte() noexcept {
    if (position_ < input_.size()) {
      return std::to_integer<std::uint32_t>(input_[position_++]);
    }
    ++overrun_;
    return 0;
  }

  std::span<const std::byte> input_;
  std::size_t position_{0};
  std::size_t overrun_{0};
  std::uint32_t range_{0xFFFFFFFFu};
  std::uint32_t code_{0};
};

unsigned bit_tree_decode(RangeDecoder& rc, std::uint16_t* probs, unsigned bits) noexcept {
  unsigned index = 1;
  for (unsigned step = 0; step < bits; ++step) {
    const unsigned value = rc.bit(probs[index]);
    index = (index << 1) | value;
  }
  return index - (1u << bits);
}

unsigned reverse_bit_tree_decode(RangeDecoder& rc, std::uint16_t* probs, unsigned bits) noexcept {
  unsigned index = 1;
  unsigned symbol = 0;
  for (unsigned step = 0; step < bits; ++step) {
    const unsigned value = rc.bit(probs[index]);
    index = (index << 1) | value;
    symbol |= value << step;
  }
  return symbol;
}

unsigned decode_length(RangeDecoder& rc, LengthModel& model, unsigned pos_state) noexcept {
  if (rc.bit(model.choice) == 0) {
    return bit_tree_decode(rc, &model.low[pos_state << 3], 3);
  }
  if (rc.bit(model.choice2) == 0) {
    return 8 + bit_tree_decode(rc, &model.mid[pos_state << 3], 3);
  }
  return 16 + bit_tree_decode(rc, model.high.data(), 8);
}

std::uint32_t decode_distance(RangeDecoder& rc, Model& model, unsigned length) noexcept {
  const unsigned length_state = std::min(length, kLenToPosStates - 1);
  const unsigned slot = bit_tree_decode(rc, &model.pos_slot[length_state << 6], 6);
  if (slot < 4) {
    return slot;
  }
  const unsigned direct = (slot >> 1) - 1;
  std::uint32_t distance = (2u | (slot & 1u)) << direct;
  if (slot < kEndPosModelIndex) {
    distance += reverse_bit_tree_decode(rc, &model.pos_special[distance - slot], direct);
  } else {
    distance += rc.direct_bits(direct - kAlignBits) << kAlignBits;
    distance += reverse_bit_tree_decode(rc, model.align.data(), kAlignBits);
  }
  return distance;
}

std::uint8_t decode_literal(RangeDecoder& rc, std::uint16_t* probs, bool matched,
                            std::uint32_t match_byte) noexcept {
  unsigned symbol = 1;
  if (matched) {
    do {
      const unsigned match_bit = (match_byte >> 7) & 1u;
      match_byte <<= 1;
      const unsigned value = rc.bit(probs[((1 + match_bit) << 8) + symbol]);
      symbol = (symbol << 1) | value;
      if (match_bit != value) {
        break;
      }
    } while (symbol < 0x100);
  }
  while (symbol < 0x100) {
    symbol = (symbol << 1) | rc.bit(probs[symbol]);
  }
  return static_cast<std::uint8_t>(symbol);
}

bool decode_lzma(std::span<const std::byte> stream, unsigned lc, unsigned lp, unsigned pb,
                 std::size_t out_size, std::vector<std::byte>& out) {
  RangeDecoder rc(stream);
  if (!rc.start()) {
    return false;
  }
  Model model(lc + lp);
  const unsigned pos_mask = (1u << pb) - 1u;
  const std::size_t literal_mask = (std::size_t{1} << lp) - 1u;
  unsigned state = 0;
  std::uint32_t rep0 = 0;
  std::uint32_t rep1 = 0;
  std::uint32_t rep2 = 0;
  std::uint32_t rep3 = 0;
  while (out.size() < out_size) {
    if (rc.exhausted()) {
      return false;
    }
    const std::size_t position = out.size();
    const unsigned pos_state = static_cast<unsigned>(position) & pos_mask;
    const unsigned state_index = (state << kPosBitsMax) + pos_state;
    if (rc.bit(model.is_match[state_index]) == 0) {
      const unsigned previous = position == 0 ? 0u : std::to_integer<unsigned>(out.back());
      const std::size_t context = ((position & literal_mask) << lc) + (previous >> (8 - lc));
      const bool matched = state >= 7;
      if (matched && rep0 >= position) {
        return false;
      }
      const auto match_byte = matched ? std::to_integer<std::uint32_t>(out[position - rep0 - 1]) : 0u;
      out.push_back(static_cast<std::byte>(
          decode_literal(rc, &model.literal[kLiteralCoderSize * context], matched, match_byte)));
      state = state < 4 ? 0 : (state < 10 ? state - 3 : state - 6);
      continue;
    }
    unsigned length = 0;
    if (rc.bit(model.is_rep[state]) == 0) {
      length = decode_length(rc, model.match_len, pos_state);
      state = state < 7 ? 7 : 10;
      const std::uint32_t distance = decode_distance(rc, model, length);
      if (distance == kEndMarker) {
        return false;
      }
      rep3 = rep2;
      rep2 = rep1;
      rep1 = rep0;
      rep0 = distance;
    } else {
      if (position == 0) {
        return false;
      }
      if (rc.bit(model.is_rep_g0[state]) == 0) {
        if (rc.bit(model.is_rep0_long[state_index]) == 0) {
          state = state < 7 ? 9 : 11;
          if (rep0 >= position) {
            return false;
          }
          const std::byte value = out[position - rep0 - 1];
          out.push_back(value);
          continue;
        }
      } else {
        std::uint32_t distance = rep1;
        if (rc.bit(model.is_rep_g1[state]) != 0) {
          if (rc.bit(model.is_rep_g2[state]) == 0) {
            distance = rep2;
          } else {
            distance = rep3;
            rep3 = rep2;
          }
          rep2 = rep1;
        }
        rep1 = rep0;
        rep0 = distance;
      }
      length = decode_length(rc, model.rep_len, pos_state);
      state = state < 7 ? 8 : 11;
    }
    if (rep0 >= position) {
      return false;
    }
    const std::size_t copy = std::size_t{length} + kMatchMinLen;
    if (copy > out_size - position) {
      return false;
    }
    for (std::size_t index = 0; index < copy; ++index) {
      const std::byte value = out[position - rep0 - 1 + index];
      out.push_back(value);
    }
  }
  return !rc.exhausted();
}

class RangeEncoder {
public:
  explicit RangeEncoder(std::vector<std::byte>& out) noexcept : out_(out) {}

  void bit(std::uint16_t& probability, unsigned value) {
    const std::uint32_t bound = (range_ >> kBitModelTotalBits) * probability;
    if (value == 0) {
      range_ = bound;
      probability = static_cast<std::uint16_t>(
          probability + (((1u << kBitModelTotalBits) - probability) >> kMoveBits));
    } else {
      low_ += bound;
      range_ -= bound;
      probability = static_cast<std::uint16_t>(probability - (probability >> kMoveBits));
    }
    normalize();
  }

  void direct_bits(std::uint32_t value, unsigned count) {
    for (unsigned index = count; index > 0; --index) {
      range_ >>= 1;
      if (((value >> (index - 1)) & 1u) != 0) {
        low_ += range_;
      }
      normalize();
    }
  }

  void flush() {
    for (unsigned index = 0; index < 5; ++index) {
      shift_low();
    }
  }

private:
  void normalize() {
    if (range_ < kTopValue) {
      range_ <<= 8;
      shift_low();
    }
  }

  void shift_low() {
    if (static_cast<std::uint32_t>(low_) < 0xFF000000u || (low_ >> 32) != 0) {
      const auto carry = static_cast<std::uint8_t>(low_ >> 32);
      auto temp = cache_;
      do {
        out_.push_back(static_cast<std::byte>(static_cast<std::uint8_t>(temp + carry)));
        temp = static_cast<std::uint8_t>(0xFF);
      } while (--cache_size_ != 0);
      cache_ = static_cast<std::uint8_t>(low_ >> 24);
    }
    ++cache_size_;
    low_ = (low_ & 0x00FFFFFFu) << 8;
  }

  std::vector<std::byte>& out_;
  std::uint64_t low_{0};
  std::uint32_t range_{0xFFFFFFFFu};
  std::uint8_t cache_{0};
  std::size_t cache_size_{1};
};

void bit_tree_encode(RangeEncoder& rc, std::uint16_t* probs, unsigned bits, unsigned symbol) {
  unsigned index = 1;
  for (unsigned step = bits; step > 0; --step) {
    const unsigned value = (symbol >> (step - 1)) & 1u;
    rc.bit(probs[index], value);
    index = (index << 1) | value;
  }
}

void reverse_bit_tree_encode(RangeEncoder& rc, std::uint16_t* probs, unsigned bits,
                             unsigned symbol) {
  unsigned index = 1;
  for (unsigned step = 0; step < bits; ++step) {
    const unsigned value = (symbol >> step) & 1u;
    rc.bit(probs[index], value);
    index = (index << 1) | value;
  }
}

void encode_length(RangeEncoder& rc, LengthModel& model, unsigned pos_state, unsigned length) {
  if (length < 8) {
    rc.bit(model.choice, 0);
    bit_tree_encode(rc, &model.low[pos_state << 3], 3, length);
    return;
  }
  rc.bit(model.choice, 1);
  if (length < 16) {
    rc.bit(model.choice2, 0);
    bit_tree_encode(rc, &model.mid[pos_state << 3], 3, length - 8);
    return;
  }
  rc.bit(model.choice2, 1);
  bit_tree_encode(rc, model.high.data(), 8, length - 16);
}

unsigned slot_of(std::uint32_t distance) noexcept {
  if (distance < 4) {
    return distance;
  }
  const unsigned top = 31u - static_cast<unsigned>(std::countl_zero(distance));
  return (top << 1) | ((distance >> (top - 1)) & 1u);
}

void encode_distance(RangeEncoder& rc, Model& model, unsigned length, std::uint32_t distance) {
  const unsigned length_state = std::min(length, kLenToPosStates - 1);
  const unsigned slot = slot_of(distance);
  bit_tree_encode(rc, &model.pos_slot[length_state << 6], 6, slot);
  if (slot < 4) {
    return;
  }
  const unsigned direct = (slot >> 1) - 1;
  const std::uint32_t base = (2u | (slot & 1u)) << direct;
  const std::uint32_t rest = distance - base;
  if (slot < kEndPosModelIndex) {
    reverse_bit_tree_encode(rc, &model.pos_special[base - slot], direct, rest);
  } else {
    rc.direct_bits(rest >> kAlignBits, direct - kAlignBits);
    reverse_bit_tree_encode(rc, model.align.data(), kAlignBits, rest & ((1u << kAlignBits) - 1u));
  }
}

void encode_literal(RangeEncoder& rc, std::uint16_t* probs, unsigned byte_value, bool matched,
                    unsigned match_byte) {
  unsigned symbol = 1;
  for (unsigned step = 8; step > 0; --step) {
    const unsigned value = (byte_value >> (step - 1)) & 1u;
    if (matched) {
      const unsigned match_bit = (match_byte >> (step - 1)) & 1u;
      rc.bit(probs[((1 + match_bit) << 8) + symbol], value);
      matched = match_bit == value;
    } else {
      rc.bit(probs[symbol], value);
    }
    symbol = (symbol << 1) | value;
  }
}

std::size_t common_length(std::span<const std::byte> body, std::size_t from, std::size_t at) noexcept {
  std::size_t length = 0;
  while (at + length < body.size() && length < kMatchMaxLen &&
         body[from + length] == body[at + length]) {
    ++length;
  }
  return length;
}

std::size_t hash3(std::span<const std::byte> body, std::size_t at) noexcept {
  const std::uint32_t key = (std::to_integer<std::uint32_t>(body[at]) << 16) |
                            (std::to_integer<std::uint32_t>(body[at + 1]) << 8) |
                            std::to_integer<std::uint32_t>(body[at + 2]);
  return static_cast<std::size_t>((key * 2654435761u) >> (32 - kHashBits));
}

}  // namespace

bool lzma_decode_swf(std::span<const std::byte> input, std::size_t out_size,
                     std::vector<std::byte>& out) {
  out.clear();
  if (input.size() < 5) {
    return false;
  }
  const unsigned properties = std::to_integer<unsigned>(input[0]);
  if (properties >= kMaxProperties) {
    return false;
  }
  const unsigned lc = properties % 9;
  const unsigned lp = (properties / 9) % 5;
  const unsigned pb = properties / 45;
  return decode_lzma(input.subspan(5), lc, lp, pb, out_size, out);
}

std::vector<std::byte> lzma_encode_swf(std::span<const std::byte> body) {
  std::vector<std::byte> out;
  out.push_back(static_cast<std::byte>((kEncodePb * 5) * 9 + kEncodeLc));
  for (unsigned shift = 0; shift < 32; shift += 8) {
    out.push_back(static_cast<std::byte>((kEncoderDictionary >> shift) & 0xFFu));
  }
  RangeEncoder rc(out);
  Model model(kEncodeLc);
  std::vector<std::size_t> heads(std::size_t{1} << kHashBits, kNoPosition);
  unsigned state = 0;
  std::uint32_t rep0 = 0;
  std::size_t position = 0;
  while (position < body.size()) {
    const unsigned pos_state = static_cast<unsigned>(position) & ((1u << kEncodePb) - 1u);
    const unsigned state_index = (state << kPosBitsMax) + pos_state;
    std::size_t rep_length = 0;
    if (position > rep0) {
      rep_length = common_length(body, position - rep0 - 1, position);
    }
    std::size_t match_length = 0;
    std::size_t match_distance = 0;
    if (position + 3 <= body.size()) {
      const std::size_t slot = hash3(body, position);
      const std::size_t candidate = heads[slot];
      heads[slot] = position;
      if (candidate != kNoPosition && position - candidate <= kEncoderDictionary) {
        match_length = common_length(body, candidate, position);
        match_distance = position - candidate - 1;
      }
    }
    if (rep_length >= kMatchMinLen && rep_length >= match_length) {
      rc.bit(model.is_match[state_index], 1);
      rc.bit(model.is_rep[state], 1);
      rc.bit(model.is_rep_g0[state], 0);
      rc.bit(model.is_rep0_long[state_index], 1);
      encode_length(rc, model.rep_len, pos_state, static_cast<unsigned>(rep_length - kMatchMinLen));
      state = state < 7 ? 8 : 11;
      position += rep_length;
    } else if (match_length > kMatchMinLen) {
      const auto length = static_cast<unsigned>(match_length - kMatchMinLen);
      rc.bit(model.is_match[state_index], 1);
      rc.bit(model.is_rep[state], 0);
      encode_length(rc, model.match_len, pos_state, length);
      encode_distance(rc, model, length, static_cast<std::uint32_t>(match_distance));
      state = state < 7 ? 7 : 10;
      rep0 = static_cast<std::uint32_t>(match_distance);
      position += match_length;
    } else {
      rc.bit(model.is_match[state_index], 0);
      const unsigned previous = position == 0 ? 0u : std::to_integer<unsigned>(body[position - 1]);
      const std::size_t context = previous >> (8 - kEncodeLc);
      const bool matched = state >= 7;
      const auto match_byte = matched ? std::to_integer<unsigned>(body[position - rep0 - 1]) : 0u;
      encode_literal(rc, &model.literal[kLiteralCoderSize * context],
                     std::to_integer<unsigned>(body[position]), matched, match_byte);
      state = state < 4 ? 0 : (state < 10 ? state - 3 : state - 6);
      ++position;
    }
  }
  rc.flush();
  return out;
}

}  // namespace fo4::swf::detail
