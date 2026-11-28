#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <fo4/papyrus/ast.hpp>
#include <fo4/papyrus/diagnostic.hpp>
#include <fo4/papyrus/script_index.hpp>
#include <fo4/pex/pex.hpp>

namespace fo4::papyrus {

struct PapyrusCompileOptions {
    std::vector<std::filesystem::path> importRoots;
    std::optional<std::filesystem::path> flagFile;
    bool emitDebugInfo{true};
    bool emitDebugOnlyCode{true};
    bool emitBetaOnlyCode{true};
    std::string userName;
    std::string computerName;
    std::int64_t compilationTime{0};
};

struct PapyrusCompileResult {
    const Script* script{nullptr};
    std::unique_ptr<fo4::pex::PexFile> pex;
    std::vector<Diagnostic> diagnostics;
    bool sourcesComplete{true};

    bool success() const { return pex != nullptr; }
};

PapyrusCompileResult compileScript(ScriptIndex& index, const Script* script,
                                   const std::string& sourceFileName,
                                   const PapyrusCompileOptions& options);

}  // namespace fo4::papyrus
