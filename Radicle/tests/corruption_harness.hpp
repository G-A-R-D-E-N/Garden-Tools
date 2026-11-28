#pragma once

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <string>
#include <typeinfo>
#include <vector>

namespace fo4::test {

struct CorruptionStats {
  std::size_t cases{0};
  std::size_t rejected{0};
};

template <class Error, class Byte, class Read>
CorruptionStats expect_closed_under_corruption(const std::vector<Byte>& valid, std::size_t header_size,
                                               Read&& read) {
  static_assert(sizeof(Byte) == 1);
  CorruptionStats stats;
  std::size_t failures = 0;
  const auto attempt = [&](const std::vector<Byte>& input, const std::string& label, bool must_throw) {
    if (failures >= 3) return;
    ++stats.cases;
    try {
      read(input);
      if (must_throw) {
        ++failures;
        ADD_FAILURE() << label << ": parsed a " << input.size() << "-byte prefix shorter than the header";
      }
    } catch (const Error&) {
      ++stats.rejected;
    } catch (const std::exception& error) {
      ++failures;
      ADD_FAILURE() << label << ": escaped the error contract with " << typeid(error).name() << ": "
                    << error.what();
    } catch (...) {
      ++failures;
      ADD_FAILURE() << label << ": escaped the error contract with a non-std exception";
    }
  };

  read(valid);

  for (std::size_t length = 0; length < valid.size(); ++length) {
    const std::vector<Byte> prefix(valid.begin(), valid.begin() + static_cast<std::ptrdiff_t>(length));
    attempt(prefix, "truncate to " + std::to_string(length), length < header_size);
  }

  const auto hex = [](std::uint32_t value) {
    char text[16];
    std::snprintf(text, sizeof text, "0x%X", value);
    return std::string(text);
  };
  const auto get = [&](std::size_t at, std::size_t width) {
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < width; ++i) {
      value |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(valid[at + i])) << (8 * i);
    }
    return value;
  };
  const auto patched = [&](std::size_t at, std::size_t width, std::uint32_t value, const char* kind) {
    auto mutated = valid;
    for (std::size_t i = 0; i < width; ++i) mutated[at + i] = static_cast<Byte>((value >> (8 * i)) & 0xFFu);
    attempt(mutated, std::string(kind) + " at offset " + std::to_string(at) + " = " + hex(value), false);
  };

  for (std::size_t at = 0; at < valid.size(); ++at) {
    for (const std::uint32_t value : {0x00u, 0x80u, 0xFFu}) patched(at, 1, value, "u8");
    if (at + 2 <= valid.size()) {
      const auto old = get(at, 2);
      for (const std::uint32_t value : {0xFFFFu, (old + 1) & 0xFFFFu, (old - 1) & 0xFFFFu}) {
        patched(at, 2, value, "u16");
      }
    }
    if (at + 4 <= valid.size()) {
      const auto old = get(at, 4);
      for (const std::uint32_t value : {0xFFFFFFFFu, 0x80000000u, 0x01000000u, old + 1, old - 1}) {
        patched(at, 4, value, "u32");
      }
    }
  }
  return stats;
}

}  // namespace fo4::test
