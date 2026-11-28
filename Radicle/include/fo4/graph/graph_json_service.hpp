#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::graph {

enum class GraphJsonStatus { Ok, Invalid, NotFound, Parse, Io };

struct GraphJsonResult {
    GraphJsonStatus status{GraphJsonStatus::Ok};
    std::string text;

    bool ok() const { return status == GraphJsonStatus::Ok; }
};

class GraphJsonService {
public:
    using Roots = std::vector<std::filesystem::path>;

    static GraphJsonResult validate(std::string_view json, const std::string* imports,
                                    const Roots& baseImports);

    static GraphJsonResult compile(std::string_view json, const std::filesystem::path* outputDir,
                                   const std::string* imports, bool sourceOnly,
                                   const Roots& baseImports);

    static GraphJsonResult linearize(std::string_view json, const std::string* imports,
                                     const Roots& baseImports);

    static GraphJsonResult corePalette(const std::string* imports, const Roots& baseImports);

    static GraphJsonResult searchPalette(std::string_view kind, std::string_view query,
                                         std::string_view scriptFilter, int limit,
                                         const std::string* imports, const Roots& baseImports);

    static GraphJsonResult describeNode(const std::string& nodeType, const std::string* imports,
                                        const Roots& baseImports);

    static GraphJsonResult loadScript(const std::filesystem::path& scriptPath,
                                      const std::string* imports, const Roots& baseImports);

    static GraphJsonResult normalize(std::string_view json);

    static GraphJsonResult loadDocument(const std::filesystem::path& path);

    static GraphJsonResult save(std::string_view json, const std::filesystem::path& path);
};

}
