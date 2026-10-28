#include <fo4/papyrus/decompiler.hpp>
#include <fo4/papyrus/script_names.hpp>

#include <algorithm>
#include <cctype>
#include <functional>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace fo4::papyrus {

using fo4::pex::PexFile;
using fo4::pex::PexFunction;
using fo4::pex::PexInstruction;
using fo4::pex::PexObject;
using fo4::pex::PexTypedName;
using fo4::pex::PexValue;
using fo4::pex::PexValueType;

namespace {

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto x = static_cast<unsigned char>(a[i]);
        const auto y = static_cast<unsigned char>(b[i]);
        if (std::tolower(x) != std::tolower(y)) return false;
    }
    return true;
}

bool startsWith(std::string_view text, std::string_view prefix) {
    return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

std::string cap(std::string s) {
    if (s.empty()) return s;
    s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
    return s;
}

std::string join(const std::vector<std::string>& parts, std::string_view sep) {
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) out += sep;
        out += parts[i];
    }
    return out;
}

}  // namespace

std::string Decompiler::toSource(const PexFile& pex) {
    std::ostringstream sb;

    std::unordered_map<int, std::string> flagNames;
    for (const auto& uf : pex.userFlags) flagNames[uf.index] = uf.name;

    const auto flags = [&](std::uint32_t mask, const std::vector<std::string>& omit) {
        std::vector<std::string> names;
        for (int b = 0; b < 32; ++b) {
            if ((mask & (1u << b)) == 0) continue;
            const auto found = flagNames.find(b);
            if (found == flagNames.end()) continue;
            bool omitted = false;
            for (const auto& o : omit) {
                if (iequals(found->second, o)) {
                    omitted = true;
                    break;
                }
            }
            if (!omitted) names.push_back(cap(found->second));
        }
        if (names.empty()) return std::string();
        return " " + join(names, " ");
    };

    for (const auto& obj : pex.objects) {
        if (!safeScriptObjectName(obj.name)) {
            throw std::runtime_error(
                "PEX object name contains an unsafe filesystem component: " + obj.name);
        }
        sb << "ScriptName " << obj.name;
        if (!obj.parentClassName.empty()) sb << " extends " << obj.parentClassName;
        if (obj.isConst) sb << " Const";
        sb << flags(obj.userFlags, {});
        sb << "\n";
        if (!obj.docString.empty()) sb << "{ " << obj.docString << " }\n";
        sb << "\n";

        for (const auto& st : obj.structs) {
            sb << "Struct " << st.name << "\n";
            for (const auto& m : st.members) {
                sb << "\t" << m.type << " " << m.name;
                if (m.defaultValue && !m.defaultValue->isNone()) sb << " = " << m.defaultValue->toString();
                if (m.isConst) sb << " Const";
                sb << flags(m.userFlags, {});
                sb << "\n";
                if (!m.docString.empty()) sb << "\t{ " << m.docString << " }\n";
            }
            sb << "EndStruct\n";
            sb << "\n";
        }

        for (const auto& p : obj.properties) {
            if (p.isAutoVar()) {
                std::optional<PexValue> backingDefault;
                bool backingConst = false;
                for (const auto& v : obj.variables) {
                    if (iequals(v.name, p.autoVarName)) {
                        backingDefault = v.defaultValue;
                        backingConst = v.isConst;
                        break;
                    }
                }
                sb << p.type << " Property " << p.name;
                if (backingDefault && !backingDefault->isNone()) sb << " = " << backingDefault->toString();
                sb << (p.canWrite() ? " Auto" : " AutoReadOnly");
                if (backingConst) sb << " Const";
                sb << flags(p.userFlags, {});
                sb << "\n";
                if (!p.docString.empty()) sb << "{ " << p.docString << " }\n";
            } else {
                sb << p.type << " Property " << p.name << flags(p.userFlags, {}) << "\n";
                if (!p.docString.empty()) sb << "{ " << p.docString << " }\n";
                if (p.readHandler) {
                    emitFunction(sb, obj, *p.readHandler, flagNames, "\t",
                                 p.type + " Function Get");
                }
                if (p.writeHandler) {
                    emitFunction(sb, obj, *p.writeHandler, flagNames, "\t", "Function Set");
                }
                sb << "EndProperty\n";
            }
            sb << "\n";
        }

        bool anyVar = false;
        for (const auto& v : obj.variables) {
            if (startsWith(v.name, "::")) continue;
            sb << v.type << " " << v.name;
            if (v.defaultValue && !v.defaultValue->isNone()) sb << " = " << v.defaultValue->toString();
            if (v.isConst) sb << " Const";
            sb << flags(v.userFlags, {});
            sb << "\n";
            anyVar = true;
        }
        if (anyVar) sb << "\n";

        for (const auto& state : obj.states) {
            const bool isDefault = state.name.empty();
            if (!isDefault) {
                const bool isAuto = iequals(state.name, obj.autoStateName);
                sb << (isAuto ? "Auto " : "") << "State " << state.name << "\n";
                sb << "\n";
            }
            for (const auto& fn : state.functions) {
                emitFunction(sb, obj, fn, flagNames, isDefault ? "" : "\t");
                sb << "\n";
            }
            if (!isDefault) sb << "EndState\n" << "\n";
        }
    }
    std::string result = sb.str();
    while (!result.empty() && (result.back() == '\n' || result.back() == ' ')) result.pop_back();
    result += "\n";
    return result;
}

void Decompiler::emitFunction(std::ostringstream& sb, const PexObject& obj,
                              const PexFunction& fn,
                              const std::unordered_map<int, std::string>& flagNames,
                              std::string_view indent,
                              std::optional<std::string> nameOverride) {
    const auto flagsStr = [&](std::uint32_t mask) {
        std::vector<std::string> names;
        for (int b = 0; b < 32; ++b) {
            if ((mask & (1u << b)) == 0) continue;
            const auto found = flagNames.find(b);
            if (found == flagNames.end()) continue;
            names.push_back(cap(found->second));
        }
        if (names.empty()) return std::string();
        return " " + join(names, " ");
    };

    const bool isRemote = !nameOverride && startsWith(fn.name, "::remote_");
    const bool isEvent =
        !nameOverride && iequals(fn.returnType, "None") &&
        (isRemote || startsWith(fn.name, "On"));

    std::vector<std::string> paramStrs;
    for (const auto& p : fn.params) paramStrs.push_back(p.type + " " + p.name);
    const auto ps = join(paramStrs, ", ");

    std::string header;
    if (nameOverride) {
        header = *nameOverride + "(" + ps + ")";
    } else if (isRemote) {
        auto rest = fn.name.substr(std::string("::remote_").size());
        std::string type = fn.params.empty() ? "" : fn.params[0].type;
        std::string ev = rest;
        if (!type.empty() && startsWith(rest, type + "_")) {
            ev = rest.substr(type.size() + 1);
        } else {
            const auto idx = rest.find("_On");
            if (idx != std::string::npos && idx > 0) {
                type = rest.substr(0, idx);
                ev = rest.substr(idx + 1);
            } else {
                ev = rest;
            }
        }
        header = type.empty() ? "Event " + ev + "(" + ps + ")"
                              : "Event " + type + "." + ev + "(" + ps + ")";
    } else if (isEvent) {
        header = "Event " + fn.name + "(" + ps + ")";
    } else {
        const std::string ret = iequals(fn.returnType, "None") ? "" : fn.returnType + " ";
        header = ret + "Function " + fn.name + "(" + ps + ")";
    }
    if (fn.isGlobal) header += " Global";
    if (fn.isNative) header += " Native";
    header += flagsStr(fn.userFlags);
    sb << indent << header << "\n";
    if (!fn.docString.empty()) sb << indent << "{ " << fn.docString << " }\n";

    if (fn.isNative) return;

    std::string body;
    try {
        body = decompileBody(obj, fn, std::string(indent) + "\t", true);
        if (body.find("::") != std::string::npos) {
            body = decompileBody(obj, fn, std::string(indent) + "\t", false);
        }
    } catch (const std::exception& ex) {
        body = std::string(indent) + "\t; [decompile failed: " + ex.what() +
               "; assembly follows]\n" + asmBody(fn, std::string(indent) + "\t");
    }
    sb << body;

    const std::string end = (!nameOverride && isEvent) ? "EndEvent" : "EndFunction";
    sb << indent << end << "\n";
}

namespace {

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

    std::ostringstream out;

    std::string typeOf(const std::string& name) const {
        const auto it = types.find(name);
        return it == types.end() ? std::string() : it->second;
    }

    static bool isTemp(std::string_view n) { return startsWith(n, "::temp"); }

    std::string resolveName(const std::string& n) const {
        const auto it = backing.find(n);
        return it == backing.end() ? n : it->second;
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

}  // namespace

std::string Decompiler::decompileBody(const PexObject& obj, const PexFunction& fn,
                                      std::string indent, bool inlineTemps) {
    BodyState s;
    s.obj = &obj;
    s.fn = &fn;
    s.indent = std::move(indent);
    s.inlineTemps = inlineTemps;

    for (const auto& p : fn.params) s.types[p.name] = p.type;
    for (const auto& l : fn.locals) s.types[l.name] = l.type;
    for (const auto& v : obj.variables) s.types[v.name] = v.type;
    for (const auto& pr : obj.properties) s.types[pr.name] = pr.type;

    for (const auto& pr : obj.properties) {
        if (pr.isAutoVar() && !pr.autoVarName.empty()) s.backing[pr.autoVarName] = pr.name;
    }

    const auto& instrs = fn.instructions;
    s.stmt.resize(instrs.size());
    s.jumps.resize(instrs.size());

    const auto selfDot = [](const std::string& self) {
        return iequals(self, "self") ? std::string() : self + ".";
    };
    const auto elemType = [](const std::string& arrayType) {
        if (arrayType.size() >= 2 && arrayType.substr(arrayType.size() - 2) == "[]") {
            return arrayType.substr(0, arrayType.size() - 2);
        }
        return arrayType;
    };
    const auto args = [&](const PexInstruction& ins) {
        std::vector<std::string> varargs;
        for (std::size_t a = static_cast<std::size_t>(ins.fixedArgCount); a < ins.args.size(); ++a) {
            varargs.push_back(s.op(ins.args[a]));
        }
        return join(varargs, ", ");
    };

    for (std::size_t i = 0; i < instrs.size(); ++i) {
        const auto& ins = instrs[i];
        const std::string mnemonic = ins.mnemonic;

        if (mnemonic == "nop") {
            continue;
        } else if (mnemonic == "iadd" || mnemonic == "fadd") {
            s.put(static_cast<int>(i), s.dest(ins, 0), "(" + s.a(ins, 1) + " + " + s.a(ins, 2) + ")");
        } else if (mnemonic == "isub" || mnemonic == "fsub") {
            s.put(static_cast<int>(i), s.dest(ins, 0), "(" + s.a(ins, 1) + " - " + s.a(ins, 2) + ")");
        } else if (mnemonic == "imul" || mnemonic == "fmul") {
            s.put(static_cast<int>(i), s.dest(ins, 0), "(" + s.a(ins, 1) + " * " + s.a(ins, 2) + ")");
        } else if (mnemonic == "idiv" || mnemonic == "fdiv") {
            s.put(static_cast<int>(i), s.dest(ins, 0), "(" + s.a(ins, 1) + " / " + s.a(ins, 2) + ")");
        } else if (mnemonic == "imod") {
            s.put(static_cast<int>(i), s.dest(ins, 0), "(" + s.a(ins, 1) + " % " + s.a(ins, 2) + ")");
        } else if (mnemonic == "ineg" || mnemonic == "fneg") {
            s.put(static_cast<int>(i), s.dest(ins, 0), "(-" + s.a(ins, 1) + ")");
        } else if (mnemonic == "not") {
            s.put(static_cast<int>(i), s.dest(ins, 0), "(!" + s.a(ins, 1) + ")");
        } else if (mnemonic == "assign") {
            s.put(static_cast<int>(i), s.dest(ins, 0), s.a(ins, 1));
        } else if (mnemonic == "cast") {
            std::string src = s.a(ins, 1);
            if (src == "None") {
                s.put(static_cast<int>(i), s.dest(ins, 0), "None");
                continue;
            }
            const std::string dt = s.typeOf(s.dest(ins, 0));
            if (iequals(dt, "ScriptObject")) {
                s.put(static_cast<int>(i), s.dest(ins, 0), src);
                continue;
            }
            if (ins.args[1].type == PexValueType::Identifier &&
                iequals(s.typeOf(ins.args[1].str), dt)) {
                s.put(static_cast<int>(i), s.dest(ins, 0), src);
                continue;
            }
            s.put(static_cast<int>(i), s.dest(ins, 0),
                  dt.empty() ? src : "(" + src + " as " + dt + ")");
        } else if (mnemonic == "cmp_eq") {
            s.put(static_cast<int>(i), s.dest(ins, 0), "(" + s.a(ins, 1) + " == " + s.a(ins, 2) + ")");
        } else if (mnemonic == "cmp_lt") {
            s.put(static_cast<int>(i), s.dest(ins, 0), "(" + s.a(ins, 1) + " < " + s.a(ins, 2) + ")");
        } else if (mnemonic == "cmp_lte") {
            s.put(static_cast<int>(i), s.dest(ins, 0), "(" + s.a(ins, 1) + " <= " + s.a(ins, 2) + ")");
        } else if (mnemonic == "cmp_gt") {
            s.put(static_cast<int>(i), s.dest(ins, 0), "(" + s.a(ins, 1) + " > " + s.a(ins, 2) + ")");
        } else if (mnemonic == "cmp_gte") {
            s.put(static_cast<int>(i), s.dest(ins, 0), "(" + s.a(ins, 1) + " >= " + s.a(ins, 2) + ")");
        } else if (mnemonic == "strcat") {
            s.put(static_cast<int>(i), s.dest(ins, 0), "(" + s.a(ins, 1) + " + " + s.a(ins, 2) + ")");
        } else if (mnemonic == "is") {
            s.put(static_cast<int>(i), s.dest(ins, 0),
                  "(" + s.a(ins, 1) + " is " + ins.args[2].str + ")");
        } else if (mnemonic == "propget") {
            s.put(static_cast<int>(i), s.dest(ins, 2),
                  selfDot(s.a(ins, 1)) + ins.args[0].str);
        } else if (mnemonic == "propset") {
            s.stmt[i] = selfDot(s.a(ins, 1)) + ins.args[0].str + " = " + s.a(ins, 2);
        } else if (mnemonic == "array_create") {
            s.put(static_cast<int>(i), s.dest(ins, 0),
                  "new " + elemType(s.typeOf(s.dest(ins, 0))) + "[" + s.a(ins, 1) + "]");
        } else if (mnemonic == "array_length") {
            s.put(static_cast<int>(i), s.dest(ins, 0), s.a(ins, 1) + ".Length");
        } else if (mnemonic == "array_getelement") {
            s.put(static_cast<int>(i), s.dest(ins, 0), s.a(ins, 1) + "[" + s.a(ins, 2) + "]");
        } else if (mnemonic == "array_setelement") {
            s.stmt[i] = s.a(ins, 0) + "[" + s.a(ins, 1) + "] = " + s.a(ins, 2);
        } else if (mnemonic == "array_findelement") {
            s.put(static_cast<int>(i), s.dest(ins, 1),
                  s.a(ins, 0) + ".Find(" + s.a(ins, 2) + ", " + s.a(ins, 3) + ")");
        } else if (mnemonic == "array_rfindelement") {
            s.put(static_cast<int>(i), s.dest(ins, 1),
                  s.a(ins, 0) + ".RFind(" + s.a(ins, 2) + ", " + s.a(ins, 3) + ")");
        } else if (mnemonic == "array_findstruct") {
            s.put(static_cast<int>(i), s.dest(ins, 1),
                  s.a(ins, 0) + ".FindStruct(\"" + ins.args[2].str + "\", " + s.a(ins, 3) + ", " +
                      s.a(ins, 4) + ")");
        } else if (mnemonic == "array_rfindstruct") {
            s.put(static_cast<int>(i), s.dest(ins, 1),
                  s.a(ins, 0) + ".RFindStruct(\"" + ins.args[2].str + "\", " + s.a(ins, 3) + ", " +
                      s.a(ins, 4) + ")");
        } else if (mnemonic == "array_add") {
            s.stmt[i] = s.a(ins, 0) + ".Add(" + s.a(ins, 1) + ", " + s.a(ins, 2) + ")";
        } else if (mnemonic == "array_insert") {
            s.stmt[i] = s.a(ins, 0) + ".Insert(" + s.a(ins, 1) + ", " + s.a(ins, 2) + ")";
        } else if (mnemonic == "array_removelast") {
            s.stmt[i] = s.a(ins, 0) + ".RemoveLast()";
        } else if (mnemonic == "array_remove") {
            s.stmt[i] = s.a(ins, 0) + ".Remove(" + s.a(ins, 1) + ", " + s.a(ins, 2) + ")";
        } else if (mnemonic == "array_clear") {
            s.stmt[i] = s.a(ins, 0) + ".Clear()";
        } else if (mnemonic == "struct_create") {
            s.put(static_cast<int>(i), s.dest(ins, 0), "new " + s.typeOf(s.dest(ins, 0)));
        } else if (mnemonic == "struct_get") {
            s.put(static_cast<int>(i), s.dest(ins, 0), s.a(ins, 1) + "." + ins.args[2].str);
        } else if (mnemonic == "struct_set") {
            s.stmt[i] = s.a(ins, 0) + "." + ins.args[1].str + " = " + s.a(ins, 2);
        } else if (mnemonic == "callmethod") {
            const std::string method = ins.args[0].str;
            const std::string self = s.a(ins, 1);
            const std::string destination = s.dest(ins, 2);
            s.put(static_cast<int>(i), destination,
                  selfDot(self) + method + "(" + args(ins) + ")");
        } else if (mnemonic == "callstatic") {
            const std::string type = ins.args[0].str;
            const std::string method = ins.args[1].str;
            const std::string destination = s.dest(ins, 2);
            s.put(static_cast<int>(i), destination, type + "." + method + "(" + args(ins) + ")");
        } else if (mnemonic == "callparent") {
            const std::string method = ins.args[0].str;
            const std::string destination = s.dest(ins, 1);
            s.put(static_cast<int>(i), destination, "Parent." + method + "(" + args(ins) + ")");
        } else if (mnemonic == "return") {
            const auto& rv = ins.args[0];
            const bool none = rv.isNone() ||
                              (rv.type == PexValueType::Identifier &&
                               iequals(rv.str, "::NoneVar"));
            s.stmt[i] = none ? "Return" : "Return " + s.op(rv);
        } else if (mnemonic == "jmp") {
            JInfo info;
            info.kind = JKind::Jmp;
            info.target = static_cast<int>(i) + ins.args[0].int_;
            s.jumps[i] = std::move(info);
        } else if (mnemonic == "jmpt") {
            JInfo info;
            info.kind = JKind::JmpT;
            info.target = static_cast<int>(i) + ins.args[1].int_;
            info.cond = s.a(ins, 0);
            info.condVar = ins.args[0].str;
            s.jumps[i] = std::move(info);
        } else if (mnemonic == "jmpf") {
            JInfo info;
            info.kind = JKind::JmpF;
            info.target = static_cast<int>(i) + ins.args[1].int_;
            info.cond = s.a(ins, 0);
            info.condVar = ins.args[0].str;
            s.jumps[i] = std::move(info);
        } else {
            s.stmt[i] = "; [unhandled " + mnemonic + "]";
        }
    }

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
        if (inlineTemps && BodyState::isTemp(l.name)) continue;

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

std::string Decompiler::disassemble(const PexFile& pex) {
    std::ostringstream sb;
    sb << "; Disassembly of " << pex.sourceFileName << "\n";
    sb << "; compiled by " << pex.userName << "@" << pex.computerName << ", FO4 PEX v"
       << pex.majorVersion << "." << pex.minorVersion << "\n";
    sb << "\n";
    for (const auto& obj : pex.objects) {
        sb << ".object " << obj.name << " extends " << obj.parentClassName << "\n";
        for (const auto& st : obj.states) {
            sb << "  .state \"" << st.name << "\"\n";
            for (const auto& fn : st.functions) dumpFn(sb, fn, "    ");
        }
        for (const auto& p : obj.properties) {
            if (p.readHandler) {
                sb << "  .propertyGet " << p.name << "\n";
                dumpFn(sb, *p.readHandler, "    ");
            }
            if (p.writeHandler) {
                sb << "  .propertySet " << p.name << "\n";
                dumpFn(sb, *p.writeHandler, "    ");
            }
        }
        sb << ".endObject\n";
    }
    return sb.str();
}

void Decompiler::dumpFn(std::ostringstream& sb, const PexFunction& fn, std::string_view indent) {
    sb << indent << ".function " << fn.name << " -> " << fn.returnType
       << (fn.isGlobal ? " global" : "") << (fn.isNative ? " native" : "") << "\n";
    if (!fn.params.empty()) {
        std::vector<std::string> parts;
        for (const auto& p : fn.params) parts.push_back(p.type + " " + p.name);
        sb << indent << "  .params " << join(parts, ", ") << "\n";
    }
    if (!fn.locals.empty()) {
        std::vector<std::string> parts;
        for (const auto& l : fn.locals) parts.push_back(l.type + " " + l.name);
        sb << indent << "  .locals " << join(parts, ", ") << "\n";
    }
    sb << asmBody(fn, std::string(indent) + "  ");
    sb << indent << ".endFunction\n";
}

std::string Decompiler::asmBody(const PexFunction& fn, std::string_view indent) {
    std::ostringstream sb;
    for (std::size_t i = 0; i < fn.instructions.size(); ++i) {
        const auto& ins = fn.instructions[i];
        std::vector<std::string> parts;
        for (const auto& a : ins.args) parts.push_back(a.toString());
        const std::string argText = join(parts, " ");
        const std::string line = ins.line > 0 ? "L" + std::to_string(ins.line) : "";
        std::string lineText = std::string(indent) + "[" + std::to_string(i) + "] " + line + " " +
                               ins.mnemonic + " " + argText;
        while (!lineText.empty() && lineText.back() == ' ') lineText.pop_back();
        sb << lineText << "\n";
    }
    return sb.str();
}

}  // namespace fo4::papyrus
