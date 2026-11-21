#pragma once

#include "fo4/papyrus/runtime.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::papyrus {

enum class InstanceStateRefusal : std::uint8_t {
    None = 0,
    Malformed = 1,
    NotFound = 2,
    TypeMismatch = 3,
    Busy = 4,
    Ambiguous = 5,
};

struct InstanceStateResult {
    InstanceStateRefusal refusal{InstanceStateRefusal::None};
    std::string reason;

    bool ok() const { return refusal == InstanceStateRefusal::None; }
};

struct InstanceStateCapture {
    std::string blob;
    std::string digest;
    std::vector<std::uint64_t> engineHandles;
};

struct InstanceIdentity {
    OwnerId owner;
    std::string script;
};

// Invariant: skipUnknown counts unknown instances, states, variables and unresolvable references instead of refusing; it never relaxes parsing or type checks.
// Invariant: a set only restores those identities and leaves every other instance untouched and uncounted; an empty set restores nothing.
struct InstanceStateOptions {
    bool skipUnknown{false};
    std::optional<std::vector<InstanceIdentity>> only;
    std::map<std::uint64_t, std::uint64_t> engineRemap;
};

struct InstanceStateReport {
    std::int32_t instances{0};
    std::int32_t variables{0};
    std::int32_t skippedInstances{0};
    std::int32_t skippedVariables{0};
    std::int32_t unboundReferences{0};

    std::string toReport() const;
};

// Invariant: instances are identified by owner and script, never by runtime id; the blob and digest do not depend on attach order.
InstanceStateResult captureInstanceState(const Runtime& runtime, InstanceStateCapture& out);

struct InstanceStatePlan {
    std::vector<InstanceRestore> entries;
};

// Invariant: stage changes nothing; commit cannot fail, so every other fallible step goes between them.
InstanceStateResult stageInstanceState(Runtime& runtime, std::string_view blob, const InstanceStateOptions& options,
                                       InstanceStatePlan& staged, InstanceStateReport& report);
void commitInstanceState(Runtime& runtime, InstanceStatePlan& staged) noexcept;

// Invariant: all or nothing; any refusal leaves the runtime untouched.
InstanceStateResult restoreInstanceState(Runtime& runtime, std::string_view blob, const InstanceStateOptions& options,
                                         InstanceStateReport& report);

}  // namespace fo4::papyrus
