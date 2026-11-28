#include "fo4/archive/path_filter.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace fo4::archive {
namespace {

std::string lower(std::string_view value) {
    std::string out(value);
    std::ranges::transform(out, out.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return out;
}

}  // namespace

PathFilter::PathFilter(std::string filter, std::string_view mode, std::chrono::milliseconds regex_timeout) {
    if (filter.empty()) return;
    const auto normalized = lower(mode);
    if (normalized.empty() || normalized == "simple") {
        simple_ = lower(filter);
        return;
    }
    if (normalized == "wildcard") {
        std::string expression{"^"};
        for (const char ch : filter) {
            if (ch == '*') {
                expression += ".*";
            } else if (ch == '?') {
                expression += '.';
            } else {
                if (std::string_view{".^$|()[]{}+\\"}.find(ch) != std::string_view::npos) expression.push_back('\\');
                expression.push_back(ch);
            }
        }
        expression += '$';
        pattern_.emplace(expression, true, regex_timeout);
        return;
    }
    if (normalized == "regex") {
        pattern_.emplace(filter, true, regex_timeout);
        return;
    }
    throw std::invalid_argument("filterMode must be simple, wildcard, or regex");
}

bool PathFilter::matches(std::string_view path) const {
    if (!simple_.empty()) return lower(path).find(simple_) != std::string::npos;
    if (pattern_) return pattern_->search(path);
    return true;
}

}  // namespace fo4::archive
