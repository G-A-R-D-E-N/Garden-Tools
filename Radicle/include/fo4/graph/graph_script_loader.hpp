#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <fo4/graph/graph_diagnostic.hpp>
#include <fo4/graph/graph_document.hpp>
#include <fo4/graph/graph_lifter.hpp>

namespace fo4::graph {

struct GraphScriptLoadResult {
    std::optional<GraphDocument> document;
    std::vector<GraphDiagnostic> diagnostics;
    std::optional<std::string> failure;

    bool success() const { return document.has_value(); }
};

class GraphScriptLoader {
public:
    static bool isScript(const std::filesystem::path& path);

    static GraphScriptLoadResult load(const std::filesystem::path& path,
                                      const std::vector<std::filesystem::path>& roots);

private:
    static std::vector<std::filesystem::path> rootsFor(
        const std::filesystem::path& path, const std::vector<std::filesystem::path>& roots);
    static GraphScriptLoadResult failed(std::string message);
};

}
