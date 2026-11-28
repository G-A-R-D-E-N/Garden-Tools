#include "fo4/papyrus/owner.hpp"

#include <stdexcept>
#include <utility>

namespace fo4::papyrus {
namespace {

const char* prefix(OwnerKind kind) {
    switch (kind) {
        case OwnerKind::Form:
            return "form";
        case OwnerKind::PlacedReference:
            return "placed";
        case OwnerKind::DynamicReference:
            return "dynamic";
        case OwnerKind::QuestAlias:
            return "alias";
        case OwnerKind::RuntimeInstance:
            return "instance";
    }
    return "form";
}

OwnerId make(OwnerKind kind, std::string baseFormKey, std::string discriminator) {
    if (baseFormKey.empty()) throw std::invalid_argument("a Papyrus owner needs a base form key");
    OwnerId owner;
    owner.kind = kind;
    owner.baseFormKey = std::move(baseFormKey);
    owner.discriminator = std::move(discriminator);
    return owner;
}

}  // namespace

std::string OwnerId::key() const {
    return std::string(prefix(kind)) + ":" + baseFormKey + "#" + discriminator;
}

bool OwnerId::operator==(const OwnerId& other) const {
    return kind == other.kind && baseFormKey == other.baseFormKey &&
           discriminator == other.discriminator;
}

bool OwnerId::operator<(const OwnerId& other) const {
    if (kind != other.kind) return kind < other.kind;
    if (baseFormKey != other.baseFormKey) return baseFormKey < other.baseFormKey;
    return discriminator < other.discriminator;
}

OwnerId OwnerId::forForm(std::string baseFormKey) {
    return make(OwnerKind::Form, std::move(baseFormKey), std::string());
}

OwnerId OwnerId::forPlacedReference(std::string baseFormKey, std::string reference) {
    if (reference.empty()) throw std::invalid_argument("a Papyrus owner needs a placed reference");
    return make(OwnerKind::PlacedReference, std::move(baseFormKey), std::move(reference));
}

OwnerId OwnerId::forDynamicReference(std::string baseFormKey, std::string token) {
    if (token.empty()) throw std::invalid_argument("a Papyrus owner needs a dynamic reference token");
    return make(OwnerKind::DynamicReference, std::move(baseFormKey), std::move(token));
}

OwnerId OwnerId::forQuestAlias(std::string baseFormKey, std::int32_t aliasId) {
    return make(OwnerKind::QuestAlias, std::move(baseFormKey), std::to_string(aliasId));
}

OwnerId OwnerId::forRuntimeInstance(std::string baseFormKey, std::int64_t instance) {
    return make(OwnerKind::RuntimeInstance, std::move(baseFormKey), std::to_string(instance));
}

OwnerId OwnerRegistry::allocateRuntimeInstance(const std::string& baseFormKey) {
    std::int64_t& next = next_[baseFormKey];
    next += 1;
    OwnerId owner = OwnerId::forRuntimeInstance(baseFormKey, next);
    allocated_.push_back(owner);
    return owner;
}

std::size_t OwnerRegistry::countFor(const std::string& baseFormKey) const {
    std::size_t count = 0;
    for (const OwnerId& owner : allocated_) {
        if (owner.baseFormKey == baseFormKey) ++count;
    }
    return count;
}

}  // namespace fo4::papyrus
