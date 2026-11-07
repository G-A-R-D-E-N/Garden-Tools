#include <fo4/papyrus/compile_script.hpp>

#include <fo4/papyrus/code_generator.hpp>
#include <fo4/papyrus/creation_kit_fidelity.hpp>
#include <fo4/papyrus/declaration_check.hpp>
#include <fo4/papyrus/resolver.hpp>
#include <fo4/papyrus/type_checker.hpp>
#include <fo4/papyrus/user_flags.hpp>

#include <optional>
#include <utility>
#include <vector>

namespace fo4::papyrus {

namespace {

bool hasError(const std::vector<Diagnostic>& diagnostics) {
    for (const auto& d : diagnostics) {
        if (d.severity == Severity::Error) return true;
    }
    return false;
}

}  // namespace

PapyrusCompileResult compileScript(ScriptIndex& index, const Script* script,
                                   const std::string& sourceFileName,
                                   const PapyrusCompileOptions& options) {
    std::vector<Diagnostic> diagnostics = script->diagnostics;
    if (script->hasErrors()) {
        PapyrusCompileResult result;
        result.script = script;
        result.diagnostics = std::move(diagnostics);
        result.sourcesComplete = false;
        return result;
    }

    const auto declarationProblems = declarationCheck(*script);
    diagnostics.insert(diagnostics.end(), declarationProblems.begin(), declarationProblems.end());
    if (hasError(diagnostics)) {
        PapyrusCompileResult result;
        result.script = script;
        result.diagnostics = std::move(diagnostics);
        result.sourcesComplete = false;
        return result;
    }

    const auto resolution = Resolver(index).resolve(script);
    diagnostics.insert(diagnostics.end(), resolution.diagnostics.begin(),
                       resolution.diagnostics.end());

    TypeChecker checker(index);
    const auto checked = checker.check(resolution);
    diagnostics.insert(diagnostics.end(), checked.begin(), checked.end());

    if (hasError(diagnostics)) {
        PapyrusCompileResult result;
        result.script = script;
        result.diagnostics = std::move(diagnostics);
        result.sourcesComplete = resolution.baseChainComplete;
        return result;
    }

    const auto flagFile =
        options.flagFile ? *options.flagFile : UserFlagTable::findFlagFile(index.roots());
    std::optional<UserFlagTable> ownedTable;
    CodeGenOptions codegenOptions;
    codegenOptions.sourceFileName = sourceFileName;
    codegenOptions.userName = options.userName;
    codegenOptions.computerName = options.computerName;
    codegenOptions.compilationTime = options.compilationTime;
    codegenOptions.emitDebugInfo = options.emitDebugInfo;
    codegenOptions.emitDebugOnlyCode = options.emitDebugOnlyCode;
    codegenOptions.emitBetaOnlyCode = options.emitBetaOnlyCode;
    if (flagFile) {
        ownedTable = UserFlagTable::fromFileOrDefault(*flagFile);
        codegenOptions.userFlags = &*ownedTable;
    }

    std::vector<Diagnostic> codegenDiagnostics;
    CodeGenerator generator(index);
    auto* pex = generator.generate(*script, resolution, codegenOptions, codegenDiagnostics);
    if (pex) CreationKitFidelity::normalize(*pex, index);
    diagnostics.insert(diagnostics.end(), codegenDiagnostics.begin(), codegenDiagnostics.end());

    PapyrusCompileResult result;
    result.script = script;
    result.pex.reset(pex);
    result.diagnostics = std::move(diagnostics);
    result.sourcesComplete = resolution.baseChainComplete;
    return result;
}

}  // namespace fo4::papyrus
