#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <fo4/papyrus/script_index.hpp>

namespace fo4::papyrus {

class AnalysisService {
public:
    static constexpr int k_maxReportedFiles = 200;
    static constexpr int k_maxCompletions = 200;
    static constexpr int k_defaultWorkspaceLimit = 100;
    static constexpr int k_maxWorkspaceLimit = 500;

    static int lineColumnToOffset(std::string_view text, int line, int column);

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

    static std::string completionJson(const std::string& text, const std::string* filePath,
                                      int offset, const std::string* imports = nullptr,
                                      const std::vector<std::filesystem::path>& baseImports = {});

    static std::string signatureHelpJson(const std::string& text, const std::string* filePath,
                                         int offset, const std::string* imports = nullptr,
                                         const std::vector<std::filesystem::path>& baseImports = {});

    static std::string checkTextJson(const std::string& text, const std::string* filePath,
                                     int revision, const std::string* imports = nullptr,
                                     const std::vector<std::filesystem::path>& baseImports = {});

    static std::string referencesJson(const std::string& text, const std::string* filePath,
                                      int offset, const std::string* imports = nullptr,
                                      const std::vector<std::filesystem::path>& baseImports = {});

    static std::string renameJson(const std::string& text, const std::string* filePath, int offset,
                                  const std::string& newName, const std::string* imports = nullptr,
                                  const std::vector<std::filesystem::path>& baseImports = {});

    static std::string workspaceSymbolsJson(const std::string& query, int limit,
                                            const std::string* imports = nullptr,
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

}  // namespace fo4::papyrus
