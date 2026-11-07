#include <fo4/graph/graph_tool_service.hpp>

#include <cctype>
#include <fstream>
#include <iterator>
#include <sstream>

#include <fo4/graph/graph_compiler.hpp>
#include <fo4/graph/graph_diagnostic.hpp>

namespace fo4::graph {

namespace {

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

std::string readText(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return std::string();
    return std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
}

void writeText(const std::filesystem::path& path, const std::string& text) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
}

void writeBytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
}

bool tryLoad(const std::filesystem::path& path, std::optional<GraphDocument>& document,
             std::optional<std::string>& failure) {
    std::error_code errorCode;
    if (!std::filesystem::is_regular_file(path, errorCode)) {
        failure = "RESULT: FAILED. No graph document at " + path.string() + ".";
        return false;
    }

    auto loaded = documentJson::tryDeserialize(readText(path));
    if (loaded.document) {
        document = std::move(*loaded.document);
        return true;
    }

    failure = "RESULT: FAILED. " +
              (loaded.error ? loaded.error->message : std::string("malformed document"));
    return false;
}

std::string padRight(std::string text, std::size_t width) {
    if (text.size() >= width) return text;
    text.append(width - text.size(), ' ');
    return text;
}

std::string describe(const GraphDiagnostic& diagnostic, const GraphDocument& document) {
    std::string where;
    if (diagnostic.nodeId) {
        const auto* node = document.node(*diagnostic.nodeId);
        where = " [node " + *diagnostic.nodeId;
        if (node != nullptr) where += " " + node->definition;
        if (diagnostic.pinId) where += " pin " + *diagnostic.pinId;
        where += "]";
    }

    std::string related;
    if (!diagnostic.relatedNodes.empty()) {
        related = " (also ";
        for (std::size_t i = 0; i < diagnostic.relatedNodes.size(); ++i) {
            if (i > 0) related += ", ";
            related += diagnostic.relatedNodes[i];
        }
        related += ")";
    }

    const auto severity = diagnostic.severity == GraphSeverity::Error ? "error" : "warning";
    return std::string("  ") + severity + " " + diagnostic.code + ": " + diagnostic.message +
           where + related;
}

}

papyrus::ScriptIndex graphIndexFor(const std::filesystem::path* graphPath,
                                   const std::string* imports,
                                   const std::vector<std::filesystem::path>& baseImports) {
    std::vector<std::filesystem::path> roots;

    if (graphPath != nullptr && !graphPath->empty()) {
        roots.push_back(graphPath->parent_path());
    }

    if (imports != nullptr) {
        std::string current;
        for (const char ch : *imports) {
            if (ch == ';') {
                if (!current.empty()) roots.push_back(std::filesystem::path(current));
                current.clear();
            } else {
                current.push_back(ch);
            }
        }
        if (!current.empty()) roots.push_back(std::filesystem::path(current));
    }

    for (const auto& root : baseImports) roots.push_back(root);

    papyrus::ScriptIndex index;
    std::error_code errorCode;
    for (const auto& root : roots) {
        if (!std::filesystem::is_directory(root, errorCode)) continue;
        bool seen = false;
        for (const auto& existing : index.roots()) {
            if (iequals(existing.string(), root.string())) {
                seen = true;
                break;
            }
        }
        if (!seen) index.addRoot(root);
    }
    return index;
}

std::string GraphToolService::validate(const std::filesystem::path& graphPath,
                                       const std::string* imports,
                                       const std::vector<std::filesystem::path>& baseImports) {
    std::optional<GraphDocument> document;
    std::optional<std::string> failure;
    if (!tryLoad(graphPath, document, failure)) return *failure;

    auto index = graphIndexFor(&graphPath, imports, baseImports);
    NodePalette palette(index);
    GraphCompiler compiler(index, &palette);
    const auto validation = compiler.validate(*document);

    std::ostringstream text;
    const std::size_t errors = validation.errors().size();
    text << (validation.ok()
                 ? "RESULT: OK. " + std::to_string(document->nodes.size()) + " nodes, " +
                       std::to_string(document->wires.size()) + " wires, no errors."
                 : "RESULT: " + std::to_string(errors) + " error(s) in " +
                       graphPath.filename().string() + ".")
         << "\n";

    for (const auto& diagnostic : validation.diagnostics) {
        text << describe(diagnostic, *document) << "\n";
    }
    if (!validation.sourcesComplete) {
        text << "Note: some types are not on the import roots, so checking was incomplete.\n";
    }

    auto out = text.str();
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
    return out;
}

std::string GraphToolService::compile(const std::filesystem::path& graphPath,
                                      const std::filesystem::path* output,
                                      const std::string* imports, bool sourceOnly,
                                      const std::vector<std::filesystem::path>& baseImports) {
    std::optional<GraphDocument> document;
    std::optional<std::string> failure;
    if (!tryLoad(graphPath, document, failure)) return *failure;

    auto index = graphIndexFor(&graphPath, imports, baseImports);
    NodePalette palette(index);
    GraphCompiler compiler(index, &palette);
    GraphCompileOptions options;
    options.stopAfterSource = sourceOnly;
    auto documentValue = std::move(*document);
    const auto result = compiler.compile(documentValue, options);

    std::ostringstream text;
    const auto errors = result.errors();
    if (!errors.empty()) {
        text << "RESULT: FAILED. " << errors.size() << " error(s) in "
             << graphPath.filename().string() << ".\n";
        for (const auto& diagnostic : result.diagnostics) {
            text << describe(diagnostic, documentValue) << "\n";
        }
        if (result.source) text << "\nGenerated source:\n" << *result.source << "\n";
        auto out = text.str();
        while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
        return out;
    }

    std::vector<std::string> written;
    if (output != nullptr && !output->empty()) {
        std::filesystem::create_directories(*output);
        const auto& name = documentValue.header.scriptName;

        const auto pscPath = *output / (name + ".psc");
        writeText(pscPath, result.source.value_or(""));
        written.push_back(pscPath.string());

        if (result.pex != nullptr) {
            const auto pexPath = *output / (name + ".pex");
            writeBytes(pexPath, result.pex->toBytes());
            written.push_back(pexPath.string());
        }
    }

    text << (sourceOnly
                 ? "RESULT: OK. Generated source for " + documentValue.header.scriptName + "."
                 : "RESULT: OK. Compiled " + documentValue.header.scriptName + ".")
         << "\n";

    for (const auto& path : written) text << "Wrote " << path << "\n";
    for (const auto& diagnostic : result.diagnostics) {
        if (diagnostic.severity == GraphSeverity::Warning) {
            text << describe(diagnostic, documentValue) << "\n";
        }
    }

    if (written.empty() && result.source) text << "\n" << *result.source << "\n";

    auto out = text.str();
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
    return out;
}

std::string GraphToolService::searchPalette(const std::string& query,
                                            const std::string* imports, int limit,
                                            const std::vector<std::filesystem::path>& baseImports) {
    auto index = graphIndexFor(nullptr, imports, baseImports);
    NodePalette palette(index);
    const auto result = palette.search(query, limit <= 0 ? 30 : limit);

    std::ostringstream text;
    text << "RESULT: " << result.entries.size() << " of " << result.total << " match '" << query
         << "'.\n";
    for (const auto& entry : result.entries) {
        text << "  " << entry.id << "\n      " << entry.signature << "\n";
    }
    if (result.truncated()) {
        text << "(" << (result.total - static_cast<int>(result.entries.size()))
             << " more not shown.)\n";
    }

    auto out = text.str();
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
    return out;
}

std::string GraphToolService::describeNode(const std::string& nodeType,
                                           const std::string* imports,
                                           const std::vector<std::filesystem::path>& baseImports) {
    auto index = graphIndexFor(nullptr, imports, baseImports);
    NodePalette palette(index);
    const auto* definition = palette.find(nodeType);
    if (definition == nullptr) {
        return "RESULT: FAILED. No node type '" + nodeType + "' on this palette.";
    }

    std::ostringstream text;
    text << "RESULT: " << definition->id << "\n";
    text << "  " << definition->title << " (" << graphNodeKindName(definition->kind) << ", "
         << definition->category << ")\n";
    if (definition->summary) text << "  " << *definition->summary << "\n";
    text << (definition->isPure
                 ? "  Pure: evaluates inline, no control flow pins.\n"
                 : "  Sequenced: has control flow pins.\n");

    for (const auto& pin : definition->pins) {
        const auto kind = pin.kind == PinKind::Exec
                              ? std::string("exec")
                              : (pin.type ? pin.type->toString() : std::string("value"));
        std::string optional;
        if (pin.isOptional) {
            optional = " (optional, default " + (pin.declaredDefault.value_or("None")) + ")";
        }
        text << "    " << padRight(pin.direction == PinDirection::In ? "In" : "Out", 3) << " "
             << padRight(pin.id, 24) << " " << kind << optional << "\n";
    }

    auto out = text.str();
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
    return out;
}

}
