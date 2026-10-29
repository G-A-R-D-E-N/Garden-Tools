#pragma once

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <fo4/papyrus/code_generator.hpp>
#include <fo4/papyrus/declaration_check.hpp>
#include <fo4/papyrus/diagnostic.hpp>
#include <fo4/papyrus/resolver.hpp>
#include <fo4/papyrus/script_index.hpp>
#include <fo4/papyrus/type_checker.hpp>
#include <fo4/pex/pex.hpp>

namespace fo4::papyrus::codegen_test {

inline std::string uniqueRoot() {
    const auto base = std::filesystem::temp_directory_path() / "fo4re-codegen";
    std::filesystem::create_directories(base);
    return (base / std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))
        .string();
}

struct CompileResult {
    bool success{false};
    bool sourcesComplete{false};
    std::unique_ptr<fo4::pex::PexFile> pex;
    std::vector<Diagnostic> diagnostics;
};

struct CodegenFixture {
    std::string root = uniqueRoot();
    ScriptIndex index;

    ~CodegenFixture() {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }

    void write(std::string_view scriptName, std::string_view source) {
        std::string relative(scriptName);
        std::replace(relative.begin(), relative.end(), ':', '/');
        const auto path = std::filesystem::path(root) / (relative + ".psc");
        std::filesystem::create_directories(path.parent_path());
        std::ofstream out(path);
        out << source;
    }

    CompileResult compile(std::string_view scriptName, bool emitDebugOnly = true) {
        index.addRoot(root);
        const Script* script = index.resolve(scriptName);
        if (script == nullptr) throw std::runtime_error("script not resolved: " + std::string(scriptName));

        CompileResult result;
        if (script->hasErrors()) {
            result.diagnostics = script->diagnostics;
            result.sourcesComplete = false;
            return result;
        }

        result.diagnostics = declarationCheck(*script);
        for (const auto& d : result.diagnostics) {
            if (d.severity == Severity::Error) return result;
        }

        const auto resolution = Resolver(index).resolve(script);
        result.diagnostics.insert(result.diagnostics.end(), resolution.diagnostics.begin(),
                                  resolution.diagnostics.end());

        TypeChecker checker(index);
        const auto checked = checker.check(resolution);
        result.diagnostics.insert(result.diagnostics.end(), checked.begin(), checked.end());
        for (const auto& d : result.diagnostics) {
            if (d.severity == Severity::Error) {
                result.sourcesComplete = resolution.baseChainComplete;
                return result;
            }
        }

        CodeGenOptions options;
        options.sourceFileName = std::string(scriptName) + ".psc";
        options.emitDebugInfo = true;
        options.emitDebugOnlyCode = emitDebugOnly;
        CodeGenerator generator(index);
        std::vector<Diagnostic> genDiags;
        auto* pex = generator.generate(*script, resolution, options, genDiags);
        result.diagnostics.insert(result.diagnostics.end(), genDiags.begin(), genDiags.end());
        result.sourcesComplete = resolution.baseChainComplete;
        if (pex) {
            result.success = true;
            result.pex.reset(pex);
        }
        return result;
    }

    fo4::pex::PexObject compileObject(std::string_view scriptName) {
        auto result = compile(scriptName);
        if (!result.success) {
            std::string joined;
            for (const auto& d : result.diagnostics) joined += d.toString() + " | ";
            throw std::runtime_error("compilation should succeed; diagnostics were: " + joined);
        }
        if (result.pex == nullptr || result.pex->objects.size() != 1) {
            throw std::runtime_error("compilation should yield exactly one object");
        }
        return std::move(result.pex->objects[0]);
    }

    void writeWithScriptObject() { write("ScriptObject", "Scriptname ScriptObject Native Hidden\n"); }
};

inline std::string operand(const fo4::pex::PexValue& v) {
    switch (v.type) {
        case fo4::pex::PexValueType::Identifier:
            return v.str.rfind("::temp", 0) == 0 ? "T" : v.str;
        case fo4::pex::PexValueType::String: {
            std::string out = "\"";
            for (const char ch : v.str) {
                if (ch == '"') out += "\\\"";
                else out += ch;
            }
            out += "\"";
            return out;
        }
        case fo4::pex::PexValueType::Integer:
            return std::to_string(v.int_);
        case fo4::pex::PexValueType::Float: {
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%.3g", static_cast<double>(v.float_));
            std::string text = buffer;
            if (text.find('.') == std::string::npos) text += ".0";
            return text;
        }
        case fo4::pex::PexValueType::Bool:
            return v.bool_ ? "true" : "false";
        default:
            return "None";
    }
}

inline const fo4::pex::PexFunction& function(const fo4::pex::PexObject& obj, std::string_view name,
                                             std::string_view state = "") {
    const fo4::pex::PexFunction* found = nullptr;
    for (const auto& s : obj.states) {
        if (s.name != state) continue;
        for (const auto& fn : s.functions) {
            if (found == nullptr && fn.name == name) found = &fn;
        }
    }
    if (found == nullptr) {
        throw std::runtime_error("no function " + std::string(name) + " in state '" +
                                 std::string(state) + "'");
    }
    return *found;
}

inline std::vector<std::string> listing(const fo4::pex::PexFunction& fn) {
    std::vector<std::string> out;
    for (const auto& instruction : fn.instructions) {
        std::string line = instruction.mnemonic;
        for (const auto& arg : instruction.args) line += " " + operand(arg);
        out.push_back(std::move(line));
    }
    return out;
}

}  // namespace fo4::papyrus::codegen_test
