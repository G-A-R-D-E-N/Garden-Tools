#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <fo4ide/graph/GraphDiagnostic.hpp>
#include <fo4ide/graph/GraphDocument.hpp>
#include <fo4ide/graph/GraphLifter.hpp>

namespace fo4ide::graph {

// Port of GraphScriptLoader.Result.
struct GraphScriptLoadResult {
    std::optional<GraphDocument> document;
    std::vector<GraphDiagnostic> diagnostics;
    std::optional<std::string> failure;

    bool success() const { return document.has_value(); }
};

// Port of GraphScriptLoader: loads a .psc (read as text) or a .pex
// (decompiled back to source) into a lifted GraphDocument, checking the
// script against its own folder as well as the given roots.
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

}  // namespace fo4ide::graph
