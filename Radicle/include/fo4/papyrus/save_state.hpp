#pragma once

#include "fo4/papyrus/value.hpp"
#include "fo4/pex/pex.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::papyrus {

struct SavedThread {
    std::string ownerKey;
    std::string script;
    std::string fingerprint;
    std::string function;
    std::int32_t pc{0};
    std::vector<std::string> locals;
    std::optional<std::int64_t> waitingToken;
    std::optional<std::string> waitingNative;
};

struct SavedRegistration {
    std::string ownerKey;
    std::string kind;
    std::string eventName;
    double interval{0.0};
    bool repeats{false};
    std::string clock;
};

struct SavedTimer {
    std::string ownerKey;
    std::string eventName;
    double elapsed{0.0};
    std::int64_t fires{0};
};

struct SavedObjective {
    std::string ownerKey;
    std::int32_t index{0};
    std::int32_t state{0};
};

// Invariant: version 2 is the only serialized form; capture sorts every list so the text and digest are stable.
struct SaveState {
    static constexpr std::int32_t kVersion = 2;

    std::int32_t version{kVersion};
    std::vector<SavedThread> threads;
    std::vector<SavedRegistration> registrations;
    std::vector<SavedTimer> timers;
    std::vector<SavedObjective> objectives;

    std::string serialize() const;
    std::string digest() const;
    static std::optional<SaveState> deserialize(std::string_view json, std::string& error);
};

SaveState captureSaveState(std::vector<SavedThread> threads, std::vector<SavedRegistration> registrations,
                           std::vector<SavedTimer> timers, std::vector<SavedObjective> objectives);

enum class ThreadRestoreState : std::uint8_t {
    Resumed = 0,
    RejectedChangedScript = 1,
    RejectedUnknownScript = 2,
    RejectedLatentWait = 3,
};

struct ThreadRestore {
    std::string ownerKey;
    std::string script;
    ThreadRestoreState state{ThreadRestoreState::RejectedUnknownScript};
    std::string reason;
};

struct RestoreReport {
    std::vector<ThreadRestore> threads;
    std::int32_t resumed{0};
    std::int32_t rejected{0};
    std::vector<SavedRegistration> registrations;
    std::vector<SavedTimer> timers;
    std::vector<SavedObjective> objectives;

    std::string toReport() const;
    std::string digest() const;
};

// Invariant: a thread resumes only on a matching fingerprint; a latent wait or absent script is rejected.
RestoreReport restoreSaveState(const SaveState& state,
                               const std::map<std::string, std::string>& currentFingerprints);

// Invariant: the fingerprint covers every instruction, state, function, property, variable and struct.
std::string scriptFingerprint(const pex::PexObject& object);

}  // namespace fo4::papyrus
