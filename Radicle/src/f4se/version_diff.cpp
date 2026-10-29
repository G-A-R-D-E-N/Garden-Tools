#include <fo4/f4se/version_diff.hpp>

#include <algorithm>
#include <cctype>
#include <map>

namespace fo4::f4se {

namespace {

using Key = std::pair<std::string, std::string>;

std::string toLowerCopy(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const auto ch : text) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return out;
}

Key keyOf(const NativeBinding& binding) {
    return {toLowerCopy(binding.papyrusClass), toLowerCopy(binding.functionName)};
}

std::string signature(const NativeBinding& binding) {
    std::string text = binding.returnType.toString();
    text += binding.isGlobal() ? " global " : " ";
    text += "(";
    for (std::size_t i = 0; i < binding.parameters.size(); i++) {
        if (i != 0) text += ", ";
        text += binding.parameters[i].type.toString();
    }
    text += ")";
    return text;
}

bool classBefore(const NativeBinding& a, const NativeBinding& b) {
    const int byClass = CiLess::icompare(a.papyrusClass, b.papyrusClass);
    if (byClass != 0) return byClass < 0;
    return CiLess::icompare(a.functionName, b.functionName) < 0;
}

}

F4SEVersionDelta VersionDiff::compare(const std::vector<NativeBinding>& older,
                                      const std::vector<NativeBinding>& newer) {
    std::map<Key, const NativeBinding*> before;
    std::map<Key, const NativeBinding*> after;
    for (const auto& binding : older) before[keyOf(binding)] = &binding;
    for (const auto& binding : newer) after[keyOf(binding)] = &binding;

    std::vector<NativeBinding> added;
    std::vector<NativeBinding> removed;
    for (const auto& [key, binding] : after) {
        if (before.find(key) == before.end()) added.push_back(*binding);
    }
    for (const auto& [key, binding] : before) {
        if (after.find(key) == after.end()) removed.push_back(*binding);
    }

    std::vector<SignatureChange> changed;
    for (const auto& [key, first] : before) {
        const auto found = after.find(key);
        if (found == after.end()) continue;
        const auto a = signature(*first);
        const auto b = signature(*found->second);
        if (a != b) {
            changed.push_back(SignatureChange{first->papyrusClass, first->functionName, a, b});
        }
    }

    std::sort(added.begin(), added.end(), classBefore);
    std::sort(removed.begin(), removed.end(), classBefore);
    std::sort(changed.begin(), changed.end(), [](const SignatureChange& a,
                                                 const SignatureChange& b) {
        const int byClass = CiLess::icompare(a.cls, b.cls);
        if (byClass != 0) return byClass < 0;
        return CiLess::icompare(a.function, b.function) < 0;
    });

    F4SEVersionDelta delta;
    delta.added = std::move(added);
    delta.removed = std::move(removed);
    delta.changed = std::move(changed);
    return delta;
}

}
