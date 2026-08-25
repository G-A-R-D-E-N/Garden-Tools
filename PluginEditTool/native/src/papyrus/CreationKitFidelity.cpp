#include <fo4ide/papyrus/CreationKitFidelity.hpp>

#include <cctype>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace fo4ide::papyrus {

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

std::uint8_t opcodeFor(const std::string& name) {
    for (std::size_t i = 0; i < std::size(PexFile::kOpCodes); ++i) {
        if (name == PexFile::kOpCodes[i].name) return static_cast<std::uint8_t>(i);
    }
    throw std::runtime_error("No PEX opcode named '" + name + "'.");
}

std::uint8_t assignOpcode() {
    static const std::uint8_t opcode = opcodeFor("assign");
    return opcode;
}

struct FunctionEntry {
    PexObject* obj;
    PexFunction* fn;
};

std::vector<FunctionEntry> functions(PexFile& pex) {
    std::vector<FunctionEntry> out;
    for (auto& obj : pex.objects) {
        for (auto& property : obj.properties) {
            if (property.readHandler) out.push_back({&obj, &*property.readHandler});
            if (property.writeHandler) out.push_back({&obj, &*property.writeHandler});
        }
        for (auto& state : obj.states) {
            for (auto& fn : state.functions) out.push_back({&obj, &fn});
        }
    }
    return out;
}

bool receiverType(const PexValue& receiver, const PexObject& obj, const PexFunction& fn,
                  std::string& out) {
    if (receiver.type != PexValueType::Identifier) return false;
    if (iequals(receiver.str, "self")) {
        out = obj.name;
        return true;
    }
    for (const auto& parameter : fn.params) {
        if (iequals(parameter.name, receiver.str)) {
            out = parameter.type;
            return true;
        }
    }
    for (const auto& local : fn.locals) {
        if (iequals(local.name, receiver.str)) {
            out = local.type;
            return true;
        }
    }
    for (const auto& variable : obj.variables) {
        if (iequals(variable.name, receiver.str)) {
            out = variable.type;
            return true;
        }
    }
    return false;
}

std::optional<std::string> declaredStateName(const PexObject& obj, const std::string& literal,
                                             bool receiverIsSelf) {
    if (!receiverIsSelf) return std::nullopt;
    for (const auto& state : obj.states) {
        if (!state.name.empty() && iequals(state.name, literal)) return state.name;
    }
    return std::nullopt;
}

bool canonicalizeGotoStateLiteral(PexInstruction& instruction, PexObject& obj, PexFunction& fn,
                                  ScriptIndex& index) {
    if (instruction.args.empty() ||
        instruction.args[0].type != PexValueType::Identifier ||
        !iequals(instruction.args[0].str, "GotoState")) {
        return false;
    }

    PexValue* argument = nullptr;
    const Script* receiverScript = nullptr;
    bool receiverIsSelf = false;

    if (instruction.mnemonic == "callmethod") {
        if (instruction.args.size() <= 3) return false;
        std::string receiver;
        if (!receiverType(instruction.args[1], obj, fn, receiver)) return false;
        receiverScript = index.resolve(receiver);
        receiverIsSelf = iequals(receiver, obj.name);
        argument = &instruction.args[3];
    } else if (instruction.mnemonic == "callparent") {
        if (instruction.args.size() <= 2) return false;
        receiverScript = index.resolve(obj.parentClassName);
        receiverIsSelf = false;
        argument = &instruction.args[2];
    } else {
        return false;
    }

    if (!receiverScript) return false;
    const Script* owner = nullptr;
    const Declaration* member = index.findMember(receiverScript, "GotoState", owner);
    if (!dynamic_cast<const CallableDecl*>(member) || !owner ||
        !iequals(owner->name, "ScriptObject") ||
        argument->type != PexValueType::String) {
        return false;
    }

    const auto canonical = declaredStateName(obj, argument->str, receiverIsSelf);
    if (!canonical || *canonical == argument->str) return false;
    argument->str = *canonical;
    return true;
}

void rewriteAsAssign(PexInstruction& instruction) {
    const auto opcode = assignOpcode();
    const auto& meta = PexFile::kOpCodes[opcode];
    instruction.opCode = opcode;
    instruction.mnemonic = meta.name;
    instruction.fixedArgCount = meta.args;
    instruction.hasVarArgs = meta.varargs;
}

bool foldNegativeLiteral(PexInstruction& instruction) {
    if (instruction.args.size() != 2) return false;
    auto& operand = instruction.args[1];

    if (instruction.mnemonic == "ineg" && operand.type == PexValueType::Integer) {
        rewriteAsAssign(instruction);
        operand.int_ = -operand.int_;
        return true;
    }
    if (instruction.mnemonic == "fneg" && operand.type == PexValueType::Float) {
        rewriteAsAssign(instruction);
        operand.float_ = -operand.float_;
        return true;
    }
    return false;
}

}  // namespace

void CreationKitFidelity::normalize(PexFile& pex, ScriptIndex& index) {
    bool stringsChanged = false;
    for (const auto& entry : functions(pex)) {
        for (auto& instruction : entry.fn->instructions) {
            stringsChanged |= canonicalizeGotoStateLiteral(instruction, *entry.obj, *entry.fn, index);
            foldNegativeLiteral(instruction);
        }
    }
    if (stringsChanged) pex.rebuildStringTable();
}

}  // namespace fo4ide::papyrus
