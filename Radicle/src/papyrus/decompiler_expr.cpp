#include "decompiler_internal.hpp"

#include <string>
#include <utility>
#include <vector>

namespace fo4::papyrus::decompiler_detail {

void translateInstructions(BodyState& s) {
    const auto& instrs = s.fn->instructions;

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
}

}  // namespace fo4::papyrus::decompiler_detail
