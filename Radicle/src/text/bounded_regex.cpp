#include "fo4/text/bounded_regex.hpp"

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>

namespace fo4::text {
namespace {

struct Deadline {
    std::chrono::steady_clock::time_point end;
    std::uint32_t ticks = 0;
};

class TimedIterator {
public:
    using iterator_category = std::bidirectional_iterator_tag;
    using value_type = char;
    using difference_type = std::ptrdiff_t;
    using pointer = const char*;
    using reference = const char&;

    TimedIterator() = default;
    TimedIterator(const char* position, Deadline* deadline) : position_(position), deadline_(deadline) {}

    reference operator*() const {
        tick();
        return *position_;
    }
    TimedIterator& operator++() {
        tick();
        ++position_;
        return *this;
    }
    TimedIterator operator++(int) {
        auto previous = *this;
        ++*this;
        return previous;
    }
    TimedIterator& operator--() {
        tick();
        --position_;
        return *this;
    }
    TimedIterator operator--(int) {
        auto previous = *this;
        --*this;
        return previous;
    }
    friend bool operator==(const TimedIterator& left, const TimedIterator& right) {
        return left.position_ == right.position_;
    }

private:
    static constexpr std::uint32_t check_mask = 0x3ff;

    void tick() const {
        if ((++deadline_->ticks & check_mask) == 0 && std::chrono::steady_clock::now() > deadline_->end) {
            throw RegexTimeout();
        }
    }

    const char* position_ = nullptr;
    Deadline* deadline_ = nullptr;
};

}  // namespace

RegexTimeout::RegexTimeout()
    : std::runtime_error("The regex filter took too long to evaluate and was stopped. "
                         "Use a simpler expression or substring/wildcard mode.") {}

BoundedRegex::BoundedRegex(std::string_view pattern, bool case_insensitive,
                           std::chrono::milliseconds timeout)
    : pattern_(std::make_shared<const std::regex>(
          std::string(pattern),
          case_insensitive ? std::regex_constants::ECMAScript | std::regex_constants::icase
                           : std::regex_constants::ECMAScript)),
      timeout_(timeout) {}

bool BoundedRegex::search(std::string_view text) const {
    Deadline deadline{std::chrono::steady_clock::now() + timeout_};
    const TimedIterator first(text.data(), &deadline);
    const TimedIterator last(text.data() + text.size(), &deadline);
    std::match_results<TimedIterator> match;
    try {
        return std::regex_search(first, last, match, *pattern_);
    } catch (const std::regex_error& error) {
        if (error.code() == std::regex_constants::error_stack ||
            error.code() == std::regex_constants::error_complexity) {
            throw RegexTimeout();
        }
        throw;
    }
}

}  // namespace fo4::text
