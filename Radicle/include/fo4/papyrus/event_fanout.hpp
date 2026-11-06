#pragma once

#include "fo4/papyrus/owner.hpp"
#include "fo4/papyrus/status.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace fo4::papyrus {

enum class FanOutResult : std::uint8_t {
    Delivered = 0,
    Refused = 1,
    SkippedParked = 2,
    NoHandler = 3,
    UnknownOwner = 4,
};

// Invariant: Delivered means the event was queued for the scheduler; a later runStep reports how the handler ran.
struct FanOutOutcome {
    OwnerId owner;
    std::uint64_t instance{0};
    std::string script;
    FanOutResult result{FanOutResult::UnknownOwner};
    Status status{Status::Ok};
    std::string reason;
};

struct FanOutReport {
    std::string eventName;
    std::string handler;
    std::vector<FanOutOutcome> outcomes;
    std::int32_t delivered{0};
    std::int32_t refused{0};
    std::int32_t skippedParked{0};
    std::int32_t noHandler{0};
    std::int32_t unknownOwner{0};

    std::string toReport() const;
    std::string digest() const;
};

// Invariant: a dotted event name Type.Event is handled by ::remote_Type_Event; any other name is its own handler.
std::string eventHandlerName(const std::string& eventName);

const char* fanOutResultName(FanOutResult result);

}  // namespace fo4::papyrus
