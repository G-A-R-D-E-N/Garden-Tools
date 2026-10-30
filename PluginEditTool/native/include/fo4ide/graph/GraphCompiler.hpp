#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <fo4ide/graph/GraphDiagnostic.hpp>
#include <fo4ide/graph/GraphDocument.hpp>
#include <fo4ide/graph/GraphLinearizer.hpp>
#include <fo4ide/graph/GraphTypeResolver.hpp>
#include <fo4ide/graph/GraphValidator.hpp>
#include <fo4ide/graph/NodePalette.hpp>
#include <fo4ide/graph/PapyrusSourceWriter.hpp>
#include <fo4ide/papyrus/AnalysisService.hpp>
#include <fo4ide/papyrus/Diagnostic.hpp>
#include <fo4ide/papyrus/Pex.hpp>

namespace fo4ide::graph {

// Port of GraphCompileOptions.
struct GraphCompileOptions {
    papyrus::PapyrusCompileOptions papyrus;
    PapyrusSourceWriterOptions writer;
    bool stopAfterSource{false};
    bool treatPapyrusErrorsAsInternalFaults{false};
};

// Port of GraphCompileResult.
struct GraphCompileResult {
    std::optional<std::string> source;
    std::optional<GraphSourceMap> sourceMap;
    std::unique_ptr<ir::IrScript> ir;
    std::unique_ptr<papyrus::PexFile> pex;
    std::vector<GraphDiagnostic> diagnostics;
    std::vector<papyrus::Diagnostic> papyrusDiagnostics;
    bool sourcesComplete{true};

    bool success() const { return pex != nullptr; }

    std::vector<const GraphDiagnostic*> errors() const {
        std::vector<const GraphDiagnostic*> out;
        for (const auto& diagnostic : diagnostics) {
            if (diagnostic.severity == GraphSeverity::Error) out.push_back(&diagnostic);
        }
        return out;
    }
};

// Port of GraphCompiler: validate -> linearize -> write source -> publish the
// script for self-reference -> compile with the Papyrus compiler, mapping any
// Papyrus diagnostics back onto graph nodes via the source map.
class GraphCompiler {
public:
    GraphCompiler(papyrus::ScriptIndex& index, NodePalette* palette = nullptr);

    static papyrus::ScriptIndex indexFor(const std::vector<std::filesystem::path>& roots);

    static std::filesystem::path scratchRoot();
    static void clearScratch();

    NodePalette& palette() { return *palette_; }

    GraphValidation validate(GraphDocument& document);

    GraphCompileResult compile(GraphDocument& document, GraphCompileOptions options = {});

    // Deserializes the graph JSON file and compiles it.
    GraphCompileResult compileFile(const std::filesystem::path& graphPath,
                                   GraphCompileOptions options = {});

    // Writes the generated .psc (and, once the PexWriter is ported, the .pex)
    // next to pscPath.
    GraphCompileResult compileToFile(GraphDocument& document,
                                     const std::filesystem::path& pscPath,
                                     std::optional<std::filesystem::path> pexPath = std::nullopt,
                                     GraphCompileOptions options = {});

private:
    void publishForSelfReference(const std::string& scriptName, const std::string& source);
    std::optional<papyrus::Script> ownerScriptFor(const GraphDocument& document) const;
    static GraphDiagnostic mapBack(const papyrus::Diagnostic& diagnostic,
                                   const GraphSourceMap& map,
                                   const GraphCompileOptions& options);

    papyrus::ScriptIndex& index_;
    NodePalette* palette_{nullptr};
    std::unique_ptr<NodePalette> ownedPalette_;
    // Lowercased script name -> scratch root (mirrors OrdinalIgnoreCase keys).
    std::map<std::string, std::filesystem::path> scratchRoots_;
};

}  // namespace fo4ide::graph
