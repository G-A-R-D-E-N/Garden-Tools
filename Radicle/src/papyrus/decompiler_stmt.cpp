#include "decompiler_internal.hpp"

#include <algorithm>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace fo4::papyrus::decompiler_detail {

std::string reconstructStatements(BodyState& s) {
    const auto& fn = *s.fn;
    const auto& instrs = fn.instructions;

    for (bool folding = true; folding;) {
        folding = false;
        for (std::size_t i = 0; i < instrs.size(); ++i) {
            const auto& first = s.jumps[i];
            if (!first || first->condVar.empty() || first->target <= static_cast<int>(i)) continue;
            if (first->target >= static_cast<int>(instrs.size())) continue;
            auto& second = s.jumps[static_cast<std::size_t>(first->target)];
            if (!second || !iequals(second->condVar, first->condVar)) continue;

            bool clear = true;
            for (int k = static_cast<int>(i) + 1; k < first->target && clear; ++k) {
                clear = !s.stmt[static_cast<std::size_t>(k)] && !s.jumps[static_cast<std::size_t>(k)];
            }
            if (!clear) continue;

            second->cond = first->kind == JKind::JmpT
                               ? "(" + first->cond + " || " + second->cond + ")"
                               : "(" + first->cond + " && " + second->cond + ")";
            s.jumps[i] = std::nullopt;
            folding = true;
        }
    }

    for (const auto& [idx, expression] : s.tempExpression) {
        if (std::find(s.tempWasRead.begin(), s.tempWasRead.end(), idx) != s.tempWasRead.end()) {
            continue;
        }
        const auto& mnemonic = instrs[static_cast<std::size_t>(idx)].mnemonic;
        if (mnemonic != "callmethod" && mnemonic != "callstatic" && mnemonic != "callparent") {
            continue;
        }
        s.stmt[static_cast<std::size_t>(idx)] = expression;
    }

    std::unordered_map<int, std::string> declaredAt;
    std::vector<int> zeroedByDeclaration;
    std::vector<PexTypedName> hoisted;

    const auto isZeroFor = [](const std::string& type, const PexValue& value) {
        if (iequals(type, "bool")) return value.type == PexValueType::Bool && !value.bool_;
        if (iequals(type, "int")) return value.type == PexValueType::Integer && value.int_ == 0;
        if (iequals(type, "float")) return value.type == PexValueType::Float && value.float_ == 0.0F;
        if (iequals(type, "string")) return value.type == PexValueType::String && value.str.empty();
        return value.isNone();
    };

    const auto runsUnconditionally = [&](int idx) {
        for (std::size_t k = 0; k < instrs.size(); ++k) {
            const auto& j = s.jumps[k];
            if (!j) continue;
            if (static_cast<int>(k) < idx && j->target > idx) return false;
            if (static_cast<int>(k) > idx && j->target <= idx) return false;
        }
        return true;
    };

    std::function<void(int, int, std::string_view)> structure;
    structure = [&](int start, int end, std::string_view ind) {
        int i = start;
        while (i < end) {
            const auto& j = s.jumps[static_cast<std::size_t>(i)];
            if (!j) {
                if (s.stmt[static_cast<std::size_t>(i)] &&
                    std::find(zeroedByDeclaration.begin(), zeroedByDeclaration.end(), i) ==
                        zeroedByDeclaration.end()) {
                    const auto declared = declaredAt.find(i);
                    if (declared != declaredAt.end()) {
                        s.out << ind << declared->second << " " << *s.stmt[static_cast<std::size_t>(i)]
                              << "\n";
                    } else {
                        s.out << ind << *s.stmt[static_cast<std::size_t>(i)] << "\n";
                    }
                }
                ++i;
                continue;
            }
            if (j->kind == JKind::JmpF && j->target > i && j->target <= end) {
                const int target = j->target;
                const int lastIdx = target - 1;
                const auto& lastJ = (lastIdx > i) ? s.jumps[static_cast<std::size_t>(lastIdx)]
                                                  : std::nullopt;
                if (lastJ && lastJ->kind == JKind::Jmp && lastJ->target <= i) {
                    s.out << ind << "While (" << j->cond << ")\n";
                    structure(i + 1, lastIdx, std::string(ind) + "\t");
                    s.out << ind << "EndWhile\n";
                    i = target;
                    continue;
                }
                if (lastJ && lastJ->kind == JKind::Jmp && lastJ->target > lastIdx) {
                    const int elseEnd = std::min(lastJ->target, end);
                    if (elseEnd <= target) {
                        s.out << ind << "If (" << j->cond << ")\n";
                        structure(i + 1, lastIdx, std::string(ind) + "\t");
                        s.out << ind << "EndIf\n";
                        i = target;
                        continue;
                    }
                    s.out << ind << "If (" << j->cond << ")\n";
                    structure(i + 1, lastIdx, std::string(ind) + "\t");
                    s.out << ind << "Else\n";
                    structure(target, elseEnd, std::string(ind) + "\t");
                    s.out << ind << "EndIf\n";
                    i = elseEnd;
                    continue;
                }
                s.out << ind << "If (" << j->cond << ")\n";
                structure(i + 1, target, std::string(ind) + "\t");
                s.out << ind << "EndIf\n";
                i = target;
                continue;
            }
            if (j->kind == JKind::JmpT && j->target > i && j->target <= end) {
                s.out << ind << "If (!(" << j->cond << "))\n";
                structure(i + 1, j->target, std::string(ind) + "\t");
                s.out << ind << "EndIf\n";
                i = j->target;
                continue;
            }
            ++i;
        }
    };

    for (const auto& l : fn.locals) {
        if (iequals(l.name, "::NoneVar")) continue;
        if (iequals(l.type, "None")) continue;
        if (s.inlineTemps && BodyState::isTemp(l.name)) continue;

        const auto write = s.firstWrite.find(l.name);
        if (write != s.firstWrite.end() && write->second >= 0) {
            const auto at = write->second;
            if (instrs[static_cast<std::size_t>(at)].mnemonic == "assign" &&
                isZeroFor(l.type, instrs[static_cast<std::size_t>(at)].args[1])) {
                zeroedByDeclaration.push_back(at);
                hoisted.push_back(l);
                continue;
            }
            if (s.stmt[static_cast<std::size_t>(at)] &&
                startsWith(*s.stmt[static_cast<std::size_t>(at)], BodyState::san(l.name) + " = ") &&
                runsUnconditionally(at)) {
                declaredAt[at] = l.type;
                continue;
            }
        }

        hoisted.push_back(l);
    }

    std::ostringstream body;
    for (const auto& l : hoisted) body << s.indent << l.type << " " << BodyState::san(l.name) << "\n";
    structure(0, static_cast<int>(instrs.size()), s.indent);
    body << s.out.str();
    return body.str();
}

}  // namespace fo4::papyrus::decompiler_detail
