#include <fo4/f4se/cross_check.hpp>

#include <algorithm>
#include <cctype>
#include <map>
#include <set>

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

Key keyOf(std::string_view a, std::string_view b) {
    return {toLowerCopy(a), toLowerCopy(b)};
}

bool sameType(const PapyrusTypeText& a, const PapyrusTypeText& b) {
    if (a.isArray != b.isArray) return false;
    if (CiEqual{}(a.name, b.name)) return true;

    const bool aQualified = a.name.find(':') != std::string::npos;
    const bool bQualified = b.name.find(':') != std::string::npos;
    if (aQualified == bQualified) return false;

    const auto unqualified = [](const std::string& name) {
        const auto colon = name.rfind(':');
        return colon == std::string::npos ? name : name.substr(colon + 1);
    };
    return CiEqual{}(unqualified(a.name), unqualified(b.name));
}

}

CrossCheckResult CrossCheck::compare(const std::vector<NativeBinding>& recovered,
                                     const std::vector<OracleNative>& declarations) {
    std::map<Key, const OracleNative*> byKey;
    for (const auto& native : declarations) {
        byKey[keyOf(native.script, native.name)] = &native;
    }

    std::vector<NativeBinding> cppOnly;
    std::vector<CrossCheckMismatch> mismatches;
    std::set<Key> seen;
    int matched = 0;

    for (const auto& binding : recovered) {
        const auto key = keyOf(binding.papyrusClass, binding.functionName);
        const auto found = byKey.find(key);
        if (found == byKey.end()) {
            cppOnly.push_back(binding);
            continue;
        }

        const auto& native = *found->second;
        seen.insert(key);
        matched++;

        if (binding.arity() != native.arity()) {
            mismatches.push_back(CrossCheckMismatch{
                binding.papyrusClass, binding.functionName, "arity",
                std::to_string(binding.arity()), std::to_string(native.arity())});
            continue;
        }

        if (binding.isGlobal() != native.isGlobal) {
            mismatches.push_back(CrossCheckMismatch{
                binding.papyrusClass, binding.functionName, "global",
                binding.isGlobal() ? "True" : "False",
                native.isGlobal ? "True" : "False"});
        }

        if (!sameType(binding.returnType, native.returnType)) {
            mismatches.push_back(CrossCheckMismatch{
                binding.papyrusClass, binding.functionName, "return",
                binding.returnType.toString(), native.returnType.toString()});
        }

        for (int i = 0; i < binding.arity(); i++) {
            const auto& a = binding.parameters[static_cast<std::size_t>(i)].type;
            const auto& b = native.parameterTypes[static_cast<std::size_t>(i)];
            if (sameType(a, b)) continue;
            mismatches.push_back(CrossCheckMismatch{
                binding.papyrusClass, binding.functionName, "parameter " + std::to_string(i),
                a.toString(), b.toString()});
        }
    }

    std::vector<OracleNative> pscOnly;
    for (const auto& native : declarations) {
        if (seen.find(keyOf(native.script, native.name)) == seen.end()) {
            pscOnly.push_back(native);
        }
    }

    CrossCheckResult result;
    result.recovered = static_cast<int>(recovered.size());
    result.declared = static_cast<int>(declarations.size());
    result.matched = matched;
    result.cppOnly = std::move(cppOnly);
    result.pscOnly = std::move(pscOnly);
    result.mismatches = std::move(mismatches);
    result.latentOnlyInCpp = static_cast<int>(std::count_if(
        recovered.begin(), recovered.end(), [](const NativeBinding& n) { return n.isLatent; }));
    result.noWaitOnlyInCpp = static_cast<int>(std::count_if(
        recovered.begin(), recovered.end(), [](const NativeBinding& n) { return n.noWait; }));
    return result;
}

}
