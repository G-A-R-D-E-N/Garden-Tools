#include "fo4/papyrus/registration.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace fo4::papyrus {
namespace {

std::string fold(const std::string& text) {
    std::string out(text);
    for (char& ch : out) {
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    }
    return out;
}

bool finite(double value) { return std::isfinite(value); }

}  // namespace

ScriptRegistration ScriptRegistration::forCustomEvent(std::string eventName) {
    if (eventName.empty()) throw std::invalid_argument("a custom event registration needs a name");
    ScriptRegistration registration;
    registration.kind = RegistrationKind::CustomEvent;
    registration.eventName = std::move(eventName);
    return registration;
}

ScriptRegistration ScriptRegistration::forTimer(double intervalSeconds, bool repeats, ClockSource clock) {
    if (!finite(intervalSeconds) || intervalSeconds <= 0.0) {
        throw std::invalid_argument("a timer needs a positive interval");
    }
    ScriptRegistration registration;
    registration.kind = RegistrationKind::Timer;
    registration.intervalSeconds = intervalSeconds;
    registration.repeats = repeats;
    registration.clock = clock;
    return registration;
}

const RegistrationEntry* RegistrationRegistry::registerRegistration(const OwnerId& owner,
                                                                    const ScriptRegistration& registration) {
    if (registration.kind == RegistrationKind::Timer &&
        (!finite(registration.intervalSeconds) || registration.intervalSeconds <= 0.0)) {
        throw std::invalid_argument("a timer needs a finite positive interval");
    }
    RegistrationEntry entry;
    entry.handle = nextHandle_++;
    entry.owner = owner;
    entry.registration = registration;
    entries_.push_back(std::move(entry));
    return &entries_.back();
}

bool RegistrationRegistry::unregister(std::int64_t handle) {
    const auto found = std::find_if(entries_.begin(), entries_.end(),
                                    [handle](const RegistrationEntry& entry) { return entry.handle == handle; });
    if (found == entries_.end()) return false;
    entries_.erase(found);
    return true;
}

std::size_t RegistrationRegistry::unregisterAll(const OwnerId& owner) {
    const auto before = entries_.size();
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                  [&owner](const RegistrationEntry& entry) { return entry.owner == owner; }),
                   entries_.end());
    return before - entries_.size();
}

std::vector<const RegistrationEntry*> RegistrationRegistry::matchCustomEvent(
    const std::string& eventName) const {
    std::vector<const RegistrationEntry*> matches;
    const std::string wanted = fold(eventName);
    for (const RegistrationEntry& entry : entries_) {
        if (entry.registration.kind != RegistrationKind::CustomEvent) continue;
        if (fold(entry.registration.eventName) == wanted) matches.push_back(&entry);
    }
    return matches;
}

std::int32_t RegistrationRegistry::advance(const std::string& owner, double deltaSeconds,
                                           ClockSource clock) {
    if (owner != kSessionOwner) {
        refusedForeignAdvance_ += 1;
        throw std::invalid_argument("only " + std::string(kSessionOwner) + " advances the script runtime");
    }
    if (!finite(deltaSeconds) || deltaSeconds < 0.0) {
        throw std::invalid_argument("a clock cannot run backwards");
    }

    std::vector<std::int64_t> timerHandles;
    for (const RegistrationEntry& entry : entries_) {
        if (entry.registration.kind == RegistrationKind::Timer) timerHandles.push_back(entry.handle);
    }

    std::int64_t fired = 0;
    for (const std::int64_t handle : timerHandles) {
        auto found = std::find_if(entries_.begin(), entries_.end(),
                                  [handle](const RegistrationEntry& entry) { return entry.handle == handle; });
        if (found == entries_.end()) continue;
        if (found->registration.clock != clock) continue;
        if (clock == ClockSource::GameTime && !gameTimeClockAvailable_) {
            unboundGameTime_ += 1;
            continue;
        }
        const double elapsed = found->elapsedSeconds + deltaSeconds;
        if (!finite(elapsed)) throw std::overflow_error("timer elapsed time is not representable");
        const double interval = found->registration.intervalSeconds;
        double periods = elapsed / interval;
        if (!found->registration.repeats) periods = std::min(1.0, periods);
        const double bump = 4.0 * (std::nextafter(periods, std::numeric_limits<double>::infinity()) - periods);
        const double count = std::floor(periods + bump);
        if (count > static_cast<double>(std::numeric_limits<std::int32_t>::max())) {
            throw std::overflow_error("timer fired more times than a batch can hold");
        }
        const std::int32_t fires = static_cast<std::int32_t>(count);
        if (fires < 0) continue;
        found->elapsedSeconds = std::max(0.0, elapsed - static_cast<double>(fires) * interval);
        found->fires += fires;
        fired += fires;
        fired_ += fires;
        if (!found->registration.repeats && fires > 0) unregister(handle);
    }
    if (fired > static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max())) {
        return std::numeric_limits<std::int32_t>::max();
    }
    return static_cast<std::int32_t>(fired);
}

}  // namespace fo4::papyrus
