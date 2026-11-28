#include <fo4/abc/abc.hpp>

#include <cstring>

namespace fo4::abc {

ByteReader::ByteReader(const std::uint8_t* data, std::size_t size) noexcept
    : data_(data),
      size_(data == nullptr ? 0 : size),
      pos_(0),
      status_(data == nullptr ? AbcStatus::Truncated : AbcStatus::Ok) {}

ByteReader::ByteReader(const std::vector<std::uint8_t>& bytes) noexcept
    : data_(bytes.empty() ? nullptr : bytes.data()),
      size_(bytes.size()),
      pos_(0),
      status_(AbcStatus::Ok) {}

void ByteReader::fail(AbcStatus status) noexcept {
  if (status_ == AbcStatus::Ok) {
    status_ = status;
  }
}

std::uint8_t ByteReader::u8() {
  if (remaining() < 1) {
    fail(AbcStatus::Truncated);
    return 0;
  }
  return data_[pos_++];
}

std::int8_t ByteReader::s8() { return static_cast<std::int8_t>(u8()); }

std::uint16_t ByteReader::u16() {
  if (remaining() < 2) {
    fail(AbcStatus::Truncated);
    return 0;
  }
  const std::uint16_t value = static_cast<std::uint16_t>(
      static_cast<std::uint16_t>(data_[pos_]) |
      static_cast<std::uint16_t>(static_cast<std::uint16_t>(data_[pos_ + 1]) << 8));
  pos_ += 2;
  return value;
}

std::uint32_t ByteReader::u32() {
  if (!ok()) {
    return 0;
  }
  std::uint32_t value = 0;
  for (std::uint32_t shift = 0; shift < 35; shift += 7) {
    if (remaining() < 1) {
      fail(AbcStatus::Truncated);
      return 0;
    }
    const std::uint8_t byte = data_[pos_++];
    value |= static_cast<std::uint32_t>(byte & 0x7Fu) << shift;
    if ((byte & 0x80u) == 0u) {
      return value;
    }
  }
  fail(AbcStatus::VarintTooLong);
  return 0;
}

std::int32_t ByteReader::s24() {
  if (remaining() < 3) {
    fail(AbcStatus::Truncated);
    return 0;
  }
  const std::uint32_t raw = static_cast<std::uint32_t>(data_[pos_]) |
                            static_cast<std::uint32_t>(data_[pos_ + 1]) << 8 |
                            static_cast<std::uint32_t>(data_[pos_ + 2]) << 16;
  pos_ += 3;
  return (raw & 0x800000u) != 0u ? static_cast<std::int32_t>(raw) - 0x1000000
                                 : static_cast<std::int32_t>(raw);
}

std::uint32_t ByteReader::u30() { return u32() & 0x3FFFFFFFu; }

std::int32_t ByteReader::s32() { return static_cast<std::int32_t>(u32()); }

double ByteReader::d64() {
  if (remaining() < 8) {
    fail(AbcStatus::Truncated);
    return 0.0;
  }
  std::uint64_t bits = 0;
  for (std::size_t i = 0; i < 8; ++i) {
    bits |= static_cast<std::uint64_t>(data_[pos_ + i]) << (8 * i);
  }
  pos_ += 8;
  double value = 0.0;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

std::string ByteReader::utf8() {
  const std::uint32_t length = u30();
  if (!ok()) {
    return std::string{};
  }
  if (remaining() < length) {
    fail(AbcStatus::Truncated);
    return std::string{};
  }
  const std::string out(reinterpret_cast<const char*>(data_ + pos_), length);
  pos_ += length;
  return out;
}

std::vector<std::uint8_t> ByteReader::block(std::size_t count) {
  if (remaining() < count) {
    fail(AbcStatus::Truncated);
    return std::vector<std::uint8_t>{};
  }
  const std::vector<std::uint8_t> out(data_ + pos_, data_ + pos_ + count);
  pos_ += count;
  return out;
}

void ByteReader::skip(std::size_t count) {
  if (remaining() < count) {
    fail(AbcStatus::Truncated);
    return;
  }
  pos_ += count;
}

}
