#pragma once

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include <fo4ide/papyrus/Pex.hpp>

namespace fo4ide::papyrus::test {

// Port of the C# PexComparer used by the DecompilerSweep and graph round-trip
// oracles. Returns a human-readable first difference between two PexObjects,
// or an empty string when they match.
inline bool cmpIequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

inline bool cmpStartsWith(std::string_view text, std::string_view prefix) {
    return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

inline std::string cmpLower(std::string_view text) {
    std::string out(text);
    for (auto& ch : out) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return out;
}

inline std::string normaliseName(const std::string& name) {
    if (cmpStartsWith(name, "::temp")) return "::temp";
    if (cmpStartsWith(name, "::mangled_")) return "::mangled";
    return cmpLower(name);
}

inline std::string operand(const PexValue& value) {
    switch (value.type) {
        case PexValueType::Identifier:
            return normaliseName(value.str);
        case PexValueType::String:
            return "\"" + value.str + "\"";
        case PexValueType::Integer:
            return std::to_string(value.int_);
        case PexValueType::Float: {
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%.6f", static_cast<double>(value.float_));
            return buffer;
        }
        case PexValueType::Bool:
            return value.bool_ ? "true" : "false";
        default:
            return "None";
    }
}

inline std::string normalise(const PexInstruction& instruction) {
    std::string out = instruction.mnemonic;
    for (const auto& arg : instruction.args) out += " " + operand(arg);
    return out;
}

inline std::string functionDifference(const PexFunction& fn, const PexFunction& ourFn) {
    if (fn.isNative != ourFn.isNative) return "native flag";
    if (fn.isGlobal != ourFn.isGlobal) return "global flag";
    if (!cmpIequals(fn.returnType, ourFn.returnType)) {
        return "returns " + fn.returnType + " vs " + ourFn.returnType;
    }
    if (fn.params.size() != ourFn.params.size()) return "parameter count";

    const std::size_t shared = std::min(fn.instructions.size(), ourFn.instructions.size());
    for (std::size_t i = 0; i < shared; ++i) {
        const std::string a = normalise(fn.instructions[i]);
        const std::string b = normalise(ourFn.instructions[i]);
        if (a != b) {
            return "[" + std::to_string(i) + "] original: " + a + " | ours: " + b;
        }
    }
    if (fn.instructions.size() != ourFn.instructions.size()) {
        return std::to_string(fn.instructions.size()) + " instructions vs " +
               std::to_string(ourFn.instructions.size());
    }
    return std::string();
}

inline std::string firstDifference(const PexObject& theirs, const PexObject& ours) {
    if (!cmpIequals(theirs.parentClassName, ours.parentClassName)) {
        return "parent '" + theirs.parentClassName + "' vs '" + ours.parentClassName + "'";
    }

    for (const auto& state : theirs.states) {
        const auto* ourState = [&]() -> const PexState* {
            for (const auto& s : ours.states) {
                if (cmpIequals(s.name, state.name)) return &s;
            }
            return nullptr;
        }();
        if (!ourState) return "missing state '" + state.name + "'";

        for (const auto& fn : state.functions) {
            const PexFunction* ourFn = nullptr;
            for (const auto& f : ourState->functions) {
                if (cmpIequals(f.name, fn.name)) {
                    ourFn = &f;
                    break;
                }
            }
            if (!ourFn) return "missing function " + fn.name;

            const auto difference = functionDifference(fn, *ourFn);
            if (!difference.empty()) return fn.name + ": " + difference;
        }

        for (const auto& fn : ourState->functions) {
            bool found = false;
            for (const auto& f : state.functions) {
                if (cmpIequals(f.name, fn.name)) {
                    found = true;
                    break;
                }
            }
            if (!found) return "extra function " + fn.name;
        }
    }

    for (const auto& property : theirs.properties) {
        const PexProperty* ourProperty = nullptr;
        for (const auto& p : ours.properties) {
            if (cmpIequals(p.name, property.name)) {
                ourProperty = &p;
                break;
            }
        }
        if (!ourProperty) return "missing property " + property.name;
        if (property.flags != ourProperty->flags) {
            return "property " + property.name + " flags";
        }
        if (property.userFlags != ourProperty->userFlags) {
            return "property " + property.name + " user flags";
        }
    }

    return std::string();
}

}  // namespace fo4ide::papyrus::test
