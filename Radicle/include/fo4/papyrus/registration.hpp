#pragma once

#include "fo4/papyrus/owner.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace fo4::papyrus {

enum class RegistrationKind : std::uint8_t {
    CustomEvent = 0,
    Timer = 1,
    RemoteEvent = 2,
};

enum class ClockSource : std::uint8_t {
    RealTime = 0,
    GameTime = 1,
};

// Invariant: a registration keeps its whole payload, so it reads back exactly as it was made.
struct ScriptRegistration {
    RegistrationKind kind{RegistrationKind::CustomEvent};
    std::string eventName;
    double intervalSeconds{0.0};
    bool repeats{false};
    ClockSource clock{ClockSource::RealTime};
    std::optional<OwnerId> source;

    static ScriptRegistration forRemoteEvent(OwnerId source, std::string eventName);
    static ScriptRegistration forCustomEvent(std::string eventName);
    static ScriptRegistration forTimer(double intervalSeconds, bool repeats,
                                       ClockSource clock = ClockSource::RealTime);
};

struct RegistrationEntry {
    std::int64_t handle{0};
    OwnerId owner;
    ScriptRegistration registration;
    double elapsedSeconds{0.0};
    std::int64_t fires{0};
};

// Invariant: only the session owner advances the clock; an unavailable game clock counts timers as unbound.
class RegistrationRegistry {
public:
    static constexpr const char* kSessionOwner = "GameSession";

    explicit RegistrationRegistry(bool gameTimeClockAvailable = false)
        : gameTimeClockAvailable_(gameTimeClockAvailable) {}

    const RegistrationEntry* registerRegistration(const OwnerId& owner, const ScriptRegistration& registration);
    bool unregister(std::int64_t handle);
    std::size_t unregisterAll(const OwnerId& owner);
    std::vector<const RegistrationEntry*> matchCustomEvent(const std::string& eventName) const;
    std::vector<const RegistrationEntry*> matchRemoteEvent(const OwnerId& source, const std::string& eventName) const;
    std::int32_t advance(const std::string& owner, double deltaSeconds, ClockSource clock);
    std::size_t count() const { return entries_.size(); }
    std::int64_t fired() const { return fired_; }
    std::int32_t refusedForeignAdvance() const { return refusedForeignAdvance_; }
    std::int32_t unboundGameTime() const { return unboundGameTime_; }
    bool gameTimeClockAvailable() const { return gameTimeClockAvailable_; }
    const std::vector<RegistrationEntry>& entries() const { return entries_; }

private:
    bool gameTimeClockAvailable_{false};
    std::vector<RegistrationEntry> entries_;
    std::int64_t nextHandle_{1};
    std::int64_t fired_{0};
    std::int32_t refusedForeignAdvance_{0};
    std::int32_t unboundGameTime_{0};
};

}  // namespace fo4::papyrus
