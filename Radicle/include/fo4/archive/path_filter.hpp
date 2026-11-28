#pragma once

#include "fo4/text/bounded_regex.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace fo4::archive {

class PathFilter {
public:
    PathFilter(std::string filter, std::string_view mode,
               std::chrono::milliseconds regex_timeout = fo4::text::BoundedRegex::default_timeout);

    bool matches(std::string_view path) const;

private:
    std::string simple_;
    std::optional<fo4::text::BoundedRegex> pattern_;
};

}  // namespace fo4::archive
