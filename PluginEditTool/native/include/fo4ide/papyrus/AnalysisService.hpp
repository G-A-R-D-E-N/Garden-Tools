#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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
                             const std::string* imports = nullptr);

    static std::string outline(const std::string& source);

    static std::string definition(const std::string& source, int line, int column,
                                  const std::string* imports = nullptr);

    static std::string analyzeJson(const std::string& text,
                                   const std::string* filePath = nullptr);

    static std::string symbolAtJson(const std::string& text, const std::string* filePath,
                                    int offset, const std::string* imports = nullptr);

    static ScriptIndex buildIndex(const std::string& sourceFile, const std::string* imports);

    static std::vector<std::filesystem::path> naturalRootsFor(
        const std::filesystem::path& folder);

    static std::string compile(const std::string& source, const std::string* output = nullptr,
                               const std::string* imports = nullptr, bool release = false,
                               bool debugInfo = true, const std::string* flagFile = nullptr);
};

// Roots the C# ToolPaths.PapyrusBaseImports() would add: the semicolon
// separated PAPYRUS_BASE_IMPORTS environment variable (trimmed, quote-stripped,
// deduplicated, existing directories only). The C# also probes app-relative and
// hard-coded Windows locations; those are machine conveniences and are not
// replicated.
std::vector<std::filesystem::path> papyrusBaseImports();

// Case-insensitive *.psc walk mirroring PapyrusFileWalk: iterative, skips
// symlinked directories, tolerates inaccessible subtrees.
std::vector<std::filesystem::path> enumeratePapyrusFiles(
    const std::filesystem::path& root);

}  // namespace fo4ide::papyrus
