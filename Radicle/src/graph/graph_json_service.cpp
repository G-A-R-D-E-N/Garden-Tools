#include <fo4/graph/graph_json_service.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include <fo4/graph/graph_compiler.hpp>
#include <fo4/graph/graph_ir_json.hpp>
#include <fo4/graph/graph_script_loader.hpp>
#include <fo4/graph/graph_tool_service.hpp>
#include <fo4/graph/node_palette.hpp>

namespace fo4::graph {

namespace {

using ojson = nlohmann::ordered_json;

GraphJsonResult done(const ojson& value) {
    return GraphJsonResult{GraphJsonStatus::Ok,
                           value.dump(2, ' ', false, ojson::error_handler_t::replace)};
}

GraphJsonResult failure(GraphJsonStatus status, std::string message) {
    return GraphJsonResult{status, std::move(message)};
}

std::string utf8(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return std::string(text.begin(), text.end());
}

std::string pascalKind(GraphNodeKind kind) {
    std::string name(graphNodeKindName(kind));
    if (!name.empty()) name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
    return name;
}

bool blank(std::string_view text) {
    return std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isspace(c); });
}

bool ordinalIgnoreCaseLess(const std::string& left, const std::string& right) {
    const auto size = std::min(left.size(), right.size());
    for (std::size_t i = 0; i < size; ++i) {
        const auto a = std::toupper(static_cast<unsigned char>(left[i]));
        const auto b = std::toupper(static_cast<unsigned char>(right[i]));
        if (a != b) return a < b;
    }
    return left.size() < right.size();
}

ojson diagnosticJson(const GraphDiagnostic& diagnostic) {
    ojson out = ojson::object();
    out["code"] = diagnostic.code;
    out["severity"] = diagnostic.severity == GraphSeverity::Error ? "error" : "warning";
    out["message"] = diagnostic.message;
    if (diagnostic.nodeId) out["nodeId"] = *diagnostic.nodeId;
    if (diagnostic.pinId) out["pinId"] = *diagnostic.pinId;
    if (diagnostic.wireId) out["wireId"] = *diagnostic.wireId;
    out["relatedNodes"] = diagnostic.relatedNodes;
    out["line"] = diagnostic.sourceLine;
    return out;
}

ojson diagnosticsJson(const std::vector<GraphDiagnostic>& diagnostics) {
    ojson out = ojson::array();
    for (const auto& diagnostic : diagnostics) out.push_back(diagnosticJson(diagnostic));
    return out;
}

bool hasError(const std::vector<GraphDiagnostic>& diagnostics) {
    return std::any_of(diagnostics.begin(), diagnostics.end(), [](const GraphDiagnostic& d) {
        return d.severity == GraphSeverity::Error;
    });
}

GraphJsonResult diagnosticsResult(const std::vector<GraphDiagnostic>& diagnostics) {
    ojson out = ojson::object();
    out["diagnostics"] = diagnosticsJson(diagnostics);
    out["ok"] = !hasError(diagnostics);
    return done(out);
}

GraphJsonResult loadFailureResult(const documentJson::LoadResult& loaded) {
    return diagnosticsResult(loaded.error ? std::vector<GraphDiagnostic>{*loaded.error}
                                          : std::vector<GraphDiagnostic>{});
}

GraphJsonResult parseFailure(const documentJson::LoadResult& loaded) {
    return failure(GraphJsonStatus::Parse,
                   loaded.error ? loaded.error->message
                                : std::string("The graph document could not be read."));
}

ojson pinJson(const PinDefinition& pin) {
    ojson out = ojson::object();
    out["id"] = pin.id;
    out["name"] = pin.label.empty() ? pin.id : pin.label;
    out["kind"] = pin.kind == PinKind::Exec ? "exec" : "data";
    out["dir"] = pin.direction == PinDirection::In ? "in" : "out";
    out["dataType"] = pin.type ? pin.type->toString() : std::string();
    out["optional"] = pin.isOptional;
    if (pin.declaredDefault) out["defaultLiteral"] = *pin.declaredDefault;
    if (pin.description) out["description"] = *pin.description;
    return out;
}

ojson definitionJson(const NodeDefinition& definition) {
    ojson out = ojson::object();
    out["type"] = definition.id;
    out["label"] = definition.title;
    out["category"] = definition.category;
    out["kind"] = pascalKind(definition.kind);
    if (definition.summary) out["summary"] = *definition.summary;
    out["isPure"] = definition.isPure;
    out["isGlobal"] = definition.isGlobal;
    if (definition.ownerScript) out["script"] = *definition.ownerScript;
    ojson pins = ojson::array();
    for (const auto& pin : definition.pins) pins.push_back(pinJson(pin));
    out["pins"] = std::move(pins);
    return out;
}

struct Session {
    papyrus::ScriptIndex index;
    NodePalette palette;
    GraphCompiler compiler;

    Session(const std::string* imports, const GraphJsonService::Roots& baseImports)
        : index(graphIndexFor(nullptr, imports, baseImports)),
          palette(index),
          compiler(index, &palette) {}
};

void writeText(const std::filesystem::path& path, const std::string& text) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    stream.flush();
    if (!stream) throw std::runtime_error("Could not write '" + utf8(path) + "'.");
}

}

GraphJsonResult GraphJsonService::validate(std::string_view json, const std::string* imports,
                                           const Roots& baseImports) {
    auto loaded = documentJson::tryDeserialize(json);
    if (!loaded.document) return loadFailureResult(loaded);

    Session session(imports, baseImports);
    return diagnosticsResult(session.compiler.validate(*loaded.document).diagnostics);
}

GraphJsonResult GraphJsonService::compile(std::string_view json,
                                          const std::filesystem::path* outputDir,
                                          const std::string* imports, bool sourceOnly,
                                          const Roots& baseImports) {
    auto loaded = documentJson::tryDeserialize(json);
    if (!loaded.document) return loadFailureResult(loaded);

    Session session(imports, baseImports);
    GraphCompileOptions options;
    options.stopAfterSource = sourceOnly;
    auto& document = *loaded.document;
    const auto result = session.compiler.compile(document, options);

    ojson out = ojson::object();
    if (result.source) out["source"] = *result.source;
    out["diagnostics"] = diagnosticsJson(result.diagnostics);
    out["ok"] = sourceOnly ? !hasError(result.diagnostics) : result.success();

    if (!sourceOnly && result.success() && outputDir != nullptr && !outputDir->empty()) {
        try {
            std::filesystem::create_directories(*outputDir);
            const auto& name = document.header.scriptName;
            const auto pexPath = *outputDir / (name + ".pex");
            const auto bytes = result.pex->toBytes();
            writeText(pexPath, std::string(bytes.begin(), bytes.end()));
            writeText(*outputDir / (name + ".psc"), result.source.value_or(""));
            out["written"] = utf8(pexPath);
        } catch (const std::exception& error) {
            return failure(GraphJsonStatus::Io, error.what());
        }
    }
    return done(out);
}

GraphJsonResult GraphJsonService::linearize(std::string_view json, const std::string* imports,
                                            const Roots& baseImports) {
    auto loaded = documentJson::tryDeserialize(json);
    if (!loaded.document) return loadFailureResult(loaded);

    Session session(imports, baseImports);
    GraphCompileOptions options;
    options.stopAfterSource = true;
    const auto result = session.compiler.compile(*loaded.document, options);

    ojson out = ojson::object();
    if (result.ir) out["ir"] = ojson::parse(irJson::serialize(*result.ir));
    out["diagnostics"] = diagnosticsJson(result.diagnostics);
    out["ok"] = result.ir != nullptr && !hasError(result.diagnostics);
    return done(out);
}

GraphJsonResult GraphJsonService::corePalette(const std::string* imports, const Roots& baseImports) {
    Session session(imports, baseImports);

    ojson builtins = ojson::array();
    for (const auto& definition : session.palette.builtins()) {
        builtins.push_back(definitionJson(definition));
    }

    auto names = session.palette.scriptNames();
    std::stable_sort(names.begin(), names.end(), ordinalIgnoreCaseLess);

    const auto stats = session.palette.wikiStats();
    ojson wiki = ojson::object();
    wiki["available"] = stats.available();
    wiki["pages"] = stats.pagesIndexed;

    ojson out = ojson::object();
    out["version"] = GraphDocument::kCurrentSchema;
    out["builtins"] = std::move(builtins);
    out["scripts"] = names;
    out["wiki"] = std::move(wiki);
    return done(out);
}

GraphJsonResult GraphJsonService::searchPalette(std::string_view kind, std::string_view query,
                                                std::string_view scriptFilter, int limit,
                                                const std::string* imports,
                                                const Roots& baseImports) {
    Session session(imports, baseImports);
    std::optional<std::string_view> filter;
    if (!blank(scriptFilter)) filter = scriptFilter;
    const auto result = session.palette.search(query, limit <= 0 ? 50 : limit, filter);

    const bool filterKind = !blank(kind) && kind != "any";
    ojson entries = ojson::array();
    for (const auto& entry : result.entries) {
        const auto entryKind = pascalKind(entry.kind);
        if (filterKind && !equalsIgnoreCase(entryKind, kind)) continue;
        ojson row = ojson::object();
        row["id"] = entry.id;
        row["title"] = entry.title;
        row["category"] = entry.category;
        row["kind"] = entryKind;
        row["signature"] = entry.signature;
        row["isPure"] = entry.isPure;
        entries.push_back(std::move(row));
    }

    ojson out = ojson::object();
    out["entries"] = std::move(entries);
    out["total"] = result.total;
    return done(out);
}

GraphJsonResult GraphJsonService::describeNode(const std::string& nodeType,
                                               const std::string* imports,
                                               const Roots& baseImports) {
    Session session(imports, baseImports);
    const auto* definition = session.palette.find(nodeType);
    if (definition == nullptr) {
        return failure(GraphJsonStatus::NotFound,
                       "no node type '" + nodeType + "' on this palette");
    }
    return done(definitionJson(*definition));
}

GraphJsonResult GraphJsonService::loadScript(const std::filesystem::path& scriptPath,
                                             const std::string* imports, const Roots& baseImports) {
    const auto index = graphIndexFor(&scriptPath, imports, baseImports);
    const auto loaded = GraphScriptLoader::load(scriptPath, index.roots());
    if (loaded.failure) return failure(GraphJsonStatus::Parse, *loaded.failure);

    ojson out = ojson::object();
    out["ok"] = loaded.success();
    if (loaded.document) out["document"] = ojson::parse(documentJson::serialize(*loaded.document));
    out["diagnostics"] = diagnosticsJson(loaded.diagnostics);
    return done(out);
}

GraphJsonResult GraphJsonService::normalize(std::string_view json) {
    const auto loaded = documentJson::tryDeserialize(json);
    if (!loaded.document) return parseFailure(loaded);
    return GraphJsonResult{GraphJsonStatus::Ok, documentJson::serialize(*loaded.document)};
}

GraphJsonResult GraphJsonService::loadDocument(const std::filesystem::path& path) {
    std::error_code errorCode;
    if (!std::filesystem::is_regular_file(path, errorCode)) {
        return failure(GraphJsonStatus::Io, "no file at " + utf8(path));
    }
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return failure(GraphJsonStatus::Io, "could not read " + utf8(path));
    std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);

    const auto loaded = documentJson::tryDeserialize(text);
    if (!loaded.document) return parseFailure(loaded);
    return GraphJsonResult{GraphJsonStatus::Ok, std::move(text)};
}

GraphJsonResult GraphJsonService::save(std::string_view json, const std::filesystem::path& path) {
    const auto loaded = documentJson::tryDeserialize(json);
    if (!loaded.document) return parseFailure(loaded);
    try {
        documentJson::saveFile(*loaded.document, path);
    } catch (const std::exception& error) {
        return failure(GraphJsonStatus::Io, error.what());
    }
    return GraphJsonResult{};
}

}
