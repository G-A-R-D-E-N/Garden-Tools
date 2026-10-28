#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <fo4ide/papyrus/ScriptIndex.hpp>

namespace fo4ide::graph {

// Port of GraphToolService: the app-facing text surface over the graph
// pipeline — validate/compile a .fograph file, search and describe the
// palette, all returning RESULT: text rather than throwing.
class GraphToolService {
public:
    static std::string validate(const std::filesystem::path& graphPath,
                                const std::string* imports = nullptr,
                                const std::vector<std::filesystem::path>& baseImports = {});

    static std::string compile(const std::filesystem::path& graphPath,
                               const std::filesystem::path* output = nullptr,
                               const std::string* imports = nullptr, bool sourceOnly = false,
                               const std::vector<std::filesystem::path>& baseImports = {});

    static std::string searchPalette(const std::string& query,
                                     const std::string* imports = nullptr, int limit = 30,
                                     const std::vector<std::filesystem::path>& baseImports = {});

    static std::string describeNode(const std::string& nodeType,
                                    const std::string* imports = nullptr,
                                    const std::vector<std::filesystem::path>& baseImports = {});
};

// Port of PapyrusScriptIndexRoots: an index over the graph's own folder,
// the semicolon-separated imports, and the Papyrus base imports (existing
// directories only, deduplicated).
papyrus::ScriptIndex graphIndexFor(const std::filesystem::path* graphPath,
                                   const std::string* imports,
                                   const std::vector<std::filesystem::path>& baseImports);

}  // namespace fo4ide::graph
