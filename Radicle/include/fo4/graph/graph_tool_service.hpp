#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <fo4/papyrus/script_index.hpp>

namespace fo4::graph {

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

papyrus::ScriptIndex graphIndexFor(const std::filesystem::path* graphPath,
                                   const std::string* imports,
                                   const std::vector<std::filesystem::path>& baseImports);

}
