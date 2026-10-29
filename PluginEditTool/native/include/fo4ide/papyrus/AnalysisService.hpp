#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <fo4ide/papyrus/Diagnostic.hpp>
#include <fo4ide/papyrus/Pex.hpp>
#include <fo4ide/papyrus/ScriptIndex.hpp>
#include <fo4ide/papyrus/Symbols.hpp>

namespace fo4ide::papyrus {

// Port of PapyrusAnalysisService: the file-facing entry points that string
// the parser, index, resolver, checker and code generator together. Results
// are plain text (Check/Outline/Definition/Compile) or JSON
// (AnalyzeJson/SymbolAtJson) exactly as the C# service formats them.
class AnalysisService {
public:
    static constexpr int k_maxReportedFiles = 200;

    // Line/column are 1-based. Returns -1 for any out-of-range position.
    static int offsetOf(std::string_view text, int line, int column);

    static std::string check(const std::string& source, bool all = true, bool semantic = true,
                             const std::string* imports = nullptr,
                             const std::vector<std::filesystem::path>& baseImports = {});

    static std::string outline(const std::string& source);

    static std::string definition(const std::string& source, int line, int column,
                                  const std::string* imports = nullptr,
                                  const std::vector<std::filesystem::path>& baseImports = {});

    static std::string analyzeJson(const std::string& text,
                                   const std::string* filePath = nullptr);

    static std::string symbolAtJson(const std::string& text, const std::string* filePath,
                                    int offset, const std::string* imports = nullptr,
                                    const std::vector<std::filesystem::path>& baseImports = {});

    static ScriptIndex buildIndex(const std::string& sourceFile, const std::string* imports,
                                  const std::vector<std::filesystem::path>& baseImports);

    static std::vector<std::filesystem::path> naturalRootsFor(
        const std::filesystem::path& folder);

    static std::string compile(const std::string& source, const std::string* output = nullptr,
                               const std::string* imports = nullptr, bool release = false,
                               bool debugInfo = true, const std::string* flagFile = nullptr,
                               const std::vector<std::filesystem::path>& baseImports = {});
};

// Case-insensitive *.psc walk mirroring PapyrusFileWalk: iterative, skips
// symlinked directories, tolerates inaccessible subtrees.
std::vector<std::filesystem::path> enumeratePapyrusFiles(
    const std::filesystem::path& root);

// Port of PapyrusCompileOptions: the switches the script compiler honors.
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

// Port of PapyrusCompileResult.
struct PapyrusCompileResult {
    const Script* script{nullptr};
    std::unique_ptr<PexFile> pex;
    std::vector<Diagnostic> diagnostics;
    bool sourcesComplete{true};

    bool success() const { return pex != nullptr; }
};

// Port of PapyrusCompiler.Compile: declaration check -> resolve -> type check
// -> generate over an already-parsed script, with the Creation Kit fidelity
// pass applied to the emitted PEX.
PapyrusCompileResult compileScript(ScriptIndex& index, const Script* script,
                                   const std::string& sourceFileName,
                                   const PapyrusCompileOptions& options);

}  // namespace fo4ide::papyrus
