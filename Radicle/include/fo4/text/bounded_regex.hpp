#pragma once

#include <chrono>
#include <memory>
#include <regex>
#include <stdexcept>
#include <string_view>

namespace fo4::text {

class RegexTimeout : public std::runtime_error {
public:
    RegexTimeout();
};

class BoundedRegex {
public:
    static constexpr std::chrono::milliseconds default_timeout{100};

    explicit BoundedRegex(std::string_view pattern, bool case_insensitive = true,
                          std::chrono::milliseconds timeout = default_timeout);

    bool search(std::string_view text) const;

private:
#ifdef __linux__
    struct Impl;
    std::shared_ptr<Impl> pattern_;
#else
    std::shared_ptr<const std::regex> pattern_;
#endif
    std::chrono::milliseconds timeout_;
};

}  // namespace fo4::text
