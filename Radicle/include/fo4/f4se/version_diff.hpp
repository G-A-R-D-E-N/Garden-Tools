#pragma once

#include <string>
#include <vector>

#include <fo4/f4se/binding_model.hpp>

namespace fo4::f4se {

struct SignatureChange {
    std::string cls;
    std::string function;
    std::string older;
    std::string newer;

    std::string toString() const {
        return cls + "." + function + ": " + older + " -> " + newer;
    }
};

struct F4SEVersionDelta {
    std::vector<NativeBinding> added;
    std::vector<NativeBinding> removed;
    std::vector<SignatureChange> changed;

    bool identical() const {
        return added.empty() && removed.empty() && changed.empty();
    }
};

class VersionDiff {
public:
    static F4SEVersionDelta compare(const std::vector<NativeBinding>& older,
                                    const std::vector<NativeBinding>& newer);
};

}
