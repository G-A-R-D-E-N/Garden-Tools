#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>

namespace fo4 {

class parse_error : public std::runtime_error {
public:
  parse_error(std::size_t offset, std::string message)
      : std::runtime_error(format(offset, message)),
        offset_(offset),
        message_(std::move(message)) {}

  std::size_t offset() const noexcept { return offset_; }
  const std::string& parse_message() const noexcept { return message_; }

private:
  static std::string format(std::size_t offset, const std::string& message) {
    return "plugin parse error at 0x" + to_hex(offset) + ": " + message;
  }

  static std::string to_hex(std::size_t value) {
    static constexpr char digits[] = "0123456789ABCDEF";
    if (value == 0) {
      return std::string{"0"};
    }
    std::string out;
    while (value != 0) {
      out.insert(out.begin(), digits[value & 0xF]);
      value >>= 4;
    }
    return out;
  }

  std::size_t offset_;
  std::string message_;
};

}  // namespace fo4
