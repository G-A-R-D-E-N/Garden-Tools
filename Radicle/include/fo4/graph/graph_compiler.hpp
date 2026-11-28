#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <fo4/graph/graph_diagnostic.hpp>
#include <fo4/graph/graph_document.hpp>
#include <fo4/graph/graph_linearizer.hpp>
#include <fo4/graph/graph_type_resolver.hpp>
#include <fo4/graph/graph_validator.hpp>
#include <fo4/graph/node_palette.hpp>
#include <fo4/graph/papyrus_source_writer.hpp>
#include <fo4/papyrus/compile_script.hpp>
#include <fo4/papyrus/diagnostic.hpp>
#include <fo4/pex/pex.hpp>

namespace fo4::graph {

struct GraphCompileOptions {
    papyrus::PapyrusCompileOptions papyrus;
    PapyrusSourceWriterOptions writer;
    bool stopAfterSource{false};
    bool treatPapyrusErrorsAsInternalFaults{false};
};

struct GraphCompileResult {
    std::optional<std::string> source;
    std::optional<GraphSourceMap> sourceMap;
    std::unique_ptr<ir::IrScript> ir;
    std::unique_ptr<fo4::pex::PexFile> pex;
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

class GraphCompiler {
public:
    GraphCompiler(papyrus::ScriptIndex& index, NodePalette* palette = nullptr);

    static papyrus::ScriptIndex indexFor(const std::vector<std::filesystem::path>& roots);

    static std::filesystem::path scratchRoot();
    static void clearScratch();

    NodePalette& palette() { return *palette_; }

    GraphValidation validate(GraphDocument& document);

    GraphCompileResult compile(GraphDocument& document, GraphCompileOptions options = {});

    GraphCompileResult compileFile(const std::filesystem::path& graphPath,
                                   GraphCompileOptions options = {});

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
    std::map<std::string, std::filesystem::path> scratchRoots_;
};

}
