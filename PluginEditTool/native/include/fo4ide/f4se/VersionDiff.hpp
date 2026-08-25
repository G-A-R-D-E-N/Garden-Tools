#pragma once

#include <string>
#include <vector>

#include <fo4ide/f4se/BindingModel.hpp>

namespace fo4ide::f4se {

// Port of SignatureChange.
struct SignatureChange {
    std::string cls;
    std::string function;
    std::string older;
    std::string newer;

    std::string toString() const {
        return cls + "." + function + ": " + older + " -> " + newer;
    }
};

// Port of F4SEVersionDelta.
struct F4SEVersionDelta {
    std::vector<NativeBinding> added;
    std::vector<NativeBinding> removed;
    std::vector<SignatureChange> changed;

    bool identical() const {
        return added.empty() && removed.empty() && changed.empty();
    }
};

// Port of F4SEVersionDiff: names the added / removed / signature-changed
// bindings between two recovered surfaces.
class VersionDiff {
public:
    static F4SEVersionDelta compare(const std::vector<NativeBinding>& older,
                                    const std::vector<NativeBinding>& newer);
};

}  // namespace fo4ide::f4se
