#include <fo4ide/papyrus/AnalysisService.hpp>

#include <optional>

#include <fo4ide/papyrus/CodeGenerator.hpp>
#include <fo4ide/papyrus/CreationKitFidelity.hpp>
#include <fo4ide/papyrus/Resolver.hpp>
#include <fo4ide/papyrus/TypeChecker.hpp>

namespace fo4ide::papyrus {

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

    if (index.snapshotOnly() && options.flagFile) {
        PapyrusCompileResult result;
        result.script = script;
        result.diagnostics = std::move(diagnostics);
        result.diagnostics.push_back(Diagnostic{DiagnosticCode::CannotEmit, Severity::Error,
            "Explicit flag files require an authorized snapshot", Span{}, sourceFileName});
        result.sourcesComplete = false;
        return result;
    }
    const auto flagFile = index.snapshotOnly() ? std::optional<std::filesystem::path>{} :
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
    if (index.snapshotOnly() && index.snapshotFlags()) {
        ownedTable = UserFlagTable::parse(*index.snapshotFlags());
        if (ownedTable->flags().empty()) ownedTable = UserFlagTable::fallout4Default();
        codegenOptions.userFlags = &*ownedTable;
    } else if (flagFile) {
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

}  // namespace fo4ide::papyrus
