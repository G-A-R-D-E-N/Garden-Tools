#include "interpreter_internal.hpp"

#include <string>

namespace fo4::avm2::detail {

std::string escape_xml_markup(const std::string& text, bool attribute) {
  std::string escaped;
  escaped.reserve(text.size());
  for (const char character : text) {
    switch (character) {
      case '&':
        escaped += "&amp;";
        break;
      case '<':
        escaped += "&lt;";
        break;
      case '>':
        escaped += "&gt;";
        break;
      case '"':
        escaped += attribute ? "&quot;" : "\"";
        break;
      default:
        escaped.push_back(character);
        break;
    }
  }
  return escaped;
}

}  // namespace fo4::avm2::detail
