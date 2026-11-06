#pragma once

#include <cctype>
#include <cstddef>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <fo4/pex/pex.hpp>

namespace fo4::papyrus::decompiler_detail {

using fo4::pex::PexFunction;
using fo4::pex::PexInstruction;
using fo4::pex::PexObject;
using fo4::pex::PexTypedName;
using fo4::pex::PexValue;
using fo4::pex::PexValueType;

inline bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto x = static_cast<unsigned char>(a[i]);
        const auto y = static_cast<unsigned char>(b[i]);
        if (std::tolower(x) != std::tolower(y)) return false;
    }
    return true;
}

inline bool startsWith(std::string_view text, std::string_view prefix) {
    return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

inline bool endsWith(std::string_view text, std::string_view suffix) {
    return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
}

inline std::string join(const std::vector<std::string>& parts, std::string_view sep) {
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) out += sep;
        out += parts[i];
    }
    return out;
}

enum class JKind { JmpF, JmpT, Jmp };

struct JInfo {
    JKind kind{JKind::Jmp};
    int target{0};
    std::string cond;
    std::string condVar;
};

struct BodyState {
    const PexObject* obj{nullptr};
    const PexFunction* fn{nullptr};
    std::string indent;
    bool inlineTemps{true};

    std::unordered_map<std::string, std::string> types;
    std::unordered_map<std::string, std::string> backing;
    std::unordered_map<std::string, std::string> temps;
    std::vector<std::optional<std::string>> stmt;
    std::vector<std::optional<JInfo>> jumps;
    std::unordered_map<std::string, int> firstWrite;
    std::unordered_map<std::string, int> tempDefinition;
    std::unordered_map<int, std::string> tempExpression;
    std::vector<int> tempWasRead;
    std::string leftTemp;

    std::ostringstream out;

    std::string typeOf(const std::string& name) const {
        const auto it = types.find(name);
        return it == types.end() ? std::string() : it->second;
    }

    static bool isTemp(std::string_view n) { return startsWith(n, "::temp"); }

    std::string resolveName(const std::string& n) const {
        const auto it = backing.find(n);
        if (it != backing.end()) return it->second;
        constexpr std::string_view kSuffix = "_var";
        if (startsWith(n, "::") && n.size() > 2 + kSuffix.size() && endsWith(n, kSuffix) && !types.contains(n)) {
            return n.substr(2, n.size() - 2 - kSuffix.size());
        }
        return n;
    }

    static std::string san(std::string_view n) {
        if (startsWith(n, "::")) return "_" + std::string(n.substr(2));
        return std::string(n);
    }

    std::string op(const PexValue& v) {
        if (v.type != PexValueType::Identifier) return v.toString();
        const std::string& n = v.str;
        if (iequals(n, "::NoneVar")) return "None";
        if (inlineTemps && isTemp(n)) {
            const auto found = temps.find(n);
            if (found != temps.end()) {
                const auto def = tempDefinition.find(n);
                if (def != tempDefinition.end()) tempWasRead.push_back(def->second);
                return found->second;
            }
            if (leftTemp.empty()) leftTemp = n;
        }
        return san(resolveName(n));
    }

    std::string a(const PexInstruction& ins, std::size_t i) { return op(ins.args[i]); }

    std::string dest(const PexInstruction& ins, std::size_t i) const { return ins.args[i].str; }

    void put(int idx, const std::string& destination, const std::string& expr) {
        if (!firstWrite.contains(destination)) firstWrite[destination] = idx;
        if (inlineTemps && isTemp(destination)) {
            temps[destination] = expr;
            tempDefinition[destination] = idx;
            tempExpression[idx] = expr;
        } else if (iequals(destination, "::NoneVar")) {
            stmt[idx] = expr;
        } else {
            stmt[idx] = san(resolveName(destination)) + " = " + expr;
        }
    }
};

std::string reconstructStatements(BodyState& s);
void translateInstructions(BodyState& s);

}  // namespace fo4::papyrus::decompiler_detail
