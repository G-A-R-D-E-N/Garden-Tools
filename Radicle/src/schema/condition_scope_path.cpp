#include "fo4/schema/condition_scopes.hpp"

#include "fo4/signature.hpp"

#include <charconv>
#include <cstddef>
#include <optional>
#include <string_view>
#include <system_error>

namespace fo4::schema {
namespace {

char ascii_lower(char ch) {
  return (ch >= 'A' && ch <= 'Z') ? static_cast<char>(ch - 'A' + 'a') : ch;
}

bool iequals(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (ascii_lower(a[i]) != ascii_lower(b[i])) return false;
  }
  return true;
}

bool is_whitespace(char ch) {
  return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' ||
         ch == '\f' || ch == '\v';
}

std::string_view trim_whitespace(std::string_view text) noexcept {
  while (!text.empty() && is_whitespace(text.front())) text.remove_prefix(1);
  while (!text.empty() && is_whitespace(text.back())) text.remove_suffix(1);
  return text;
}

bool parse_index(std::string_view text, std::size_t& value) noexcept {
  text = trim_whitespace(text);
  if (text.empty()) return false;
  const auto* begin = text.data();
  const auto* end = begin + text.size();
  const auto [ptr, error] = std::from_chars(begin, end, value, 10);
  return error == std::errc{} && ptr == end;
}

std::optional<ScopedConditionPath> parse_scoped_path(
    Signature record, std::string_view path) noexcept {
  const auto separator = path.find('.');
  if (separator == std::string_view::npos) return std::nullopt;
  const auto second = path.find('.', separator + 1);
  if (second != std::string_view::npos) {
    if (path.find('.', second + 1) != std::string_view::npos ||
        !iequals(trim_whitespace(path.substr(second + 1)), "Conditions")) return std::nullopt;
    const auto outer = trim_whitespace(path.substr(0, separator));
    const auto inner = trim_whitespace(path.substr(separator + 1, second - separator - 1));
    const auto outer_open = outer.find('['), outer_close = outer.find(']');
    const auto inner_open = inner.find('['), inner_close = inner.find(']');
    if (outer_open == std::string_view::npos || outer_close != outer.size() - 1 || outer_close <= outer_open ||
        inner_open == std::string_view::npos || inner_close != inner.size() - 1 || inner_close <= inner_open)
      return std::nullopt;
    std::size_t outer_index = 0, inner_index = 0;
    if (!parse_index(outer.substr(outer_open + 1, outer_close - outer_open - 1), outer_index) ||
        !parse_index(inner.substr(inner_open + 1, inner_close - inner_open - 1), inner_index)) return std::nullopt;
    for (const auto& descriptor : k_generated_condition_scopes) {
      if (descriptor.kind == ConditionScopeKind::scene_start_scene_conditions && descriptor.record == record &&
          iequals(outer.substr(0, outer_open), descriptor.container) &&
          iequals(inner.substr(0, inner_open), descriptor.member))
        return ScopedConditionPath{descriptor, outer_index, inner_index};
    }
    return std::nullopt;
  }
  const auto container_path = trim_whitespace(path.substr(0, separator));
  const auto member_path = trim_whitespace(path.substr(separator + 1));
  const auto open = container_path.find('[');
  if (open == std::string_view::npos) return std::nullopt;
  const auto close = container_path.find(']', open + 1);
  if (close == std::string_view::npos || close + 1 != container_path.size()) {
    return std::nullopt;
  }

  for (const auto& descriptor : k_generated_condition_scopes) {
    if (descriptor.record != record || descriptor.kind == ConditionScopeKind::scene_start_scene_conditions) continue;
    if (!iequals(container_path.substr(0, open), descriptor.container)) continue;

    std::size_t container_index = 0;
    if (!parse_index(container_path.substr(open + 1, close - open - 1),
                     container_index)) {
      continue;
    }
    if (!iequals(member_path, descriptor.member)) continue;
    return ScopedConditionPath{descriptor, container_index, std::nullopt};
  }
  return std::nullopt;
}

}

std::optional<ScopedConditionPath> scoped_condition_path(
    Signature record, std::string_view path) noexcept {
  return parse_scoped_path(record, path);
}

}
