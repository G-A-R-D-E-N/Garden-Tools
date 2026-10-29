#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace fo4::papyrus {

enum class OwnerKind : std::uint8_t {
    Form = 0,
    PlacedReference = 1,
    DynamicReference = 2,
    QuestAlias = 3,
    RuntimeInstance = 4,
};

// Invariant: identity is kind plus base form key plus discriminator, so owners of one base form stay distinct.
struct OwnerId {
    OwnerKind kind{OwnerKind::Form};
    std::string baseFormKey;
    std::string discriminator;

    std::string key() const;
    bool operator==(const OwnerId& other) const;
    bool operator!=(const OwnerId& other) const { return !(*this == other); }
    bool operator<(const OwnerId& other) const;

    static OwnerId forForm(std::string baseFormKey);
    static OwnerId forPlacedReference(std::string baseFormKey, std::string reference);
    static OwnerId forDynamicReference(std::string baseFormKey, std::string token);
    static OwnerId forQuestAlias(std::string baseFormKey, std::int32_t aliasId);
    static OwnerId forRuntimeInstance(std::string baseFormKey, std::int64_t instance);
};

// Invariant: every runtime instance of one base form gets a distinct discriminator.
class OwnerRegistry {
public:
    OwnerId allocateRuntimeInstance(const std::string& baseFormKey);
    std::size_t countFor(const std::string& baseFormKey) const;
    const std::vector<OwnerId>& allocated() const { return allocated_; }

private:
    std::map<std::string, std::int64_t> next_;
    std::vector<OwnerId> allocated_;
};

}  // namespace fo4::papyrus
