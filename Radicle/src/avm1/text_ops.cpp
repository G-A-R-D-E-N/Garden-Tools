#include "text_ops.hpp"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <limits>
#include <string>
#include <string_view>

namespace fo4::avm1::text_ops {

namespace {

constexpr const char* kWhitespace = " \t\r\n";
constexpr double kUint32Range = 4294967296.0;

struct Decoded {
  std::uint32_t value;
  std::size_t length;
};

std::size_t utf8_length(std::uint8_t lead) {
  if (lead < 0x80) {
    return 1;
  }
  if (lead >= 0xC2 && lead <= 0xDF) {
    return 2;
  }
  if (lead >= 0xE0 && lead <= 0xEF) {
    return 3;
  }
  if (lead >= 0xF0 && lead <= 0xF4) {
    return 4;
  }
  return 0;
}

Decoded decode_code_point(std::string_view text) {
  const auto lead = static_cast<std::uint8_t>(text.front());
  const std::size_t length = utf8_length(lead);
  if (length == 0 || text.size() < length) {
    return {lead, 1};
  }
  std::uint32_t value = length == 1 ? lead : lead & (0xFFu >> (length + 1));
  for (std::size_t i = 1; i < length; ++i) {
    const auto continuation = static_cast<std::uint8_t>(text[i]);
    if ((continuation & 0xC0u) != 0x80u) {
      return {lead, 1};
    }
    value = (value << 6) | (continuation & 0x3Fu);
  }
  constexpr std::uint32_t kShortest[] = {0, 0, 0x80, 0x800, 0x10000};
  if (value < kShortest[length] || value > 0x10FFFFu) {
    return {lead, 1};
  }
  return {value, length};
}

std::size_t code_point_count(std::string_view text) {
  std::size_t count = 0;
  for (std::size_t at = 0; at < text.size(); ++count) {
    at += decode_code_point(text.substr(at)).length;
  }
  return count;
}

std::size_t byte_offset(std::string_view text, std::size_t code_points) {
  std::size_t at = 0;
  for (std::size_t seen = 0; seen < code_points && at < text.size(); ++seen) {
    at += decode_code_point(text.substr(at)).length;
  }
  return at;
}

std::string number_text(double value) {
  if (std::isnan(value)) {
    return "NaN";
  }
  if (std::isinf(value)) {
    return value < 0.0 ? "-Infinity" : "Infinity";
  }
  if (value == 0.0) {
    return "0";
  }
  const std::string scientific = std::format("{:.14e}", std::fabs(value));
  const std::size_t e_at = scientific.find('e');
  std::string digits = scientific.substr(0, 1) + scientific.substr(2, e_at - 2);
  while (digits.size() > 1 && digits.back() == '0') {
    digits.pop_back();
  }
  const int point = std::stoi(scientific.substr(e_at + 1)) + 1;
  const int count = static_cast<int>(digits.size());
  std::string out = value < 0.0 ? "-" : "";
  if (count <= point && point <= 21) {
    out += digits + std::string(static_cast<std::size_t>(point - count), '0');
  } else if (0 < point && point <= 21) {
    out += digits.substr(0, static_cast<std::size_t>(point)) + "." +
           digits.substr(static_cast<std::size_t>(point));
  } else if (-6 < point && point <= 0) {
    out += "0." + std::string(static_cast<std::size_t>(-point), '0') + digits;
  } else {
    out += digits.substr(0, 1);
    if (count > 1) {
      out += "." + digits.substr(1);
    }
    const int exponent = point - 1;
    out += exponent < 0 ? "e-" : "e+";
    out += std::to_string(std::abs(exponent));
  }
  return out;
}

double parse_number(const std::string& text) {
  const std::size_t first = text.find_first_not_of(kWhitespace);
  if (first == std::string::npos) {
    return 0.0;
  }
  const std::string trimmed = text.substr(first, text.find_last_not_of(kWhitespace) - first + 1);
  char* end = nullptr;
  const double value = std::strtod(trimmed.c_str(), &end);
  if (end == trimmed.c_str() || *end != '\0') {
    return std::numeric_limits<double>::quiet_NaN();
  }
  return value;
}

}  // namespace

std::uint32_t first_code_point(std::string_view text) {
  return text.empty() ? 0 : decode_code_point(text).value;
}

// Invariant: callers mask the unit to 16 bits, so three bytes is the longest form.
std::string encode_code_unit(std::uint32_t unit) {
  if (unit < 0x80) {
    return std::string(1, static_cast<char>(unit));
  }
  if (unit < 0x800) {
    return {static_cast<char>(0xC0u | (unit >> 6)), static_cast<char>(0x80u | (unit & 0x3Fu))};
  }
  return {static_cast<char>(0xE0u | (unit >> 12)),
          static_cast<char>(0x80u | ((unit >> 6) & 0x3Fu)),
          static_cast<char>(0x80u | (unit & 0x3Fu))};
}

std::string to_text(const Avm1Data& data, std::uint8_t swf_version) {
  switch (data.kind) {
    case Avm1DataKind::Undefined:
      return swf_version >= 1 && swf_version <= 6 ? "" : "undefined";
    case Avm1DataKind::Null:
      return "null";
    case Avm1DataKind::Boolean:
      return data.boolean ? "true" : "false";
    case Avm1DataKind::Number:
      return number_text(data.number);
    case Avm1DataKind::String:
      return data.text;
    case Avm1DataKind::Object:
      return "[object Object]";
    case Avm1DataKind::Function:
      return "[type Function]";
  }
  return "undefined";
}

// Invariant: swf_version 0 keeps the unspecified rules, where null is 0 and undefined is NaN.
double to_number(const Avm1Data& data, std::uint8_t swf_version) {
  switch (data.kind) {
    case Avm1DataKind::Undefined:
      if (swf_version == 0 || swf_version >= 7) {
        return std::numeric_limits<double>::quiet_NaN();
      }
      return 0.0;
    case Avm1DataKind::Null:
      if (swf_version >= 7) {
        return std::numeric_limits<double>::quiet_NaN();
      }
      return 0.0;
    case Avm1DataKind::Boolean:
      return data.boolean ? 1.0 : 0.0;
    case Avm1DataKind::Number:
      return data.number;
    case Avm1DataKind::String:
      return parse_number(data.text);
    case Avm1DataKind::Object:
    case Avm1DataKind::Function:
      return std::numeric_limits<double>::quiet_NaN();
  }
  return std::numeric_limits<double>::quiet_NaN();
}

std::uint32_t to_uint32(const Avm1Data& data, std::uint8_t swf_version) {
  const double number = to_number(data, swf_version);
  if (!std::isfinite(number)) {
    return 0;
  }
  const double modulo = std::fmod(std::trunc(number), kUint32Range);
  return static_cast<std::uint32_t>(modulo < 0.0 ? modulo + kUint32Range : modulo);
}

std::size_t unit_count(std::string_view text, std::uint8_t swf_version) {
  return swf_version >= kCodePointVersion ? code_point_count(text) : text.size();
}

std::size_t unit_offset(std::string_view text, std::size_t units, std::uint8_t swf_version) {
  return swf_version >= kCodePointVersion ? byte_offset(text, units) : units;
}

}  // namespace fo4::avm1::text_ops
