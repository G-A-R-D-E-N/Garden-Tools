#include <fo4ide/app/ServiceTargets.hpp>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

#include <fo4ide/graph/GraphCompiler.hpp>
#include <fo4ide/graph/GraphDocument.hpp>
#include <fo4ide/graph/GraphScriptLoader.hpp>
#include <fo4ide/graph/GraphToolService.hpp>
#include <fo4ide/graph/NodeDefinition.hpp>
#include <fo4ide/graph/NodePalette.hpp>
#include <fo4ide/papyrus/ScriptIndex.hpp>

#include "ServiceTargetSupport.hpp"

namespace fo4ide::app {

using namespace support;

namespace {

bool graphIEquals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

void writeFileBytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) throw std::runtime_error("Could not open '" + path.string() + "' for writing.");
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    if (!stream) throw std::runtime_error("Could not write '" + path.string() + "'.");
}

nlohmann::json describeNodeDefinition(const graph::NodeDefinition& definition) {
    nlohmann::json pins = nlohmann::json::array();
    for (const auto& pin : definition.pins) {
        nlohmann::json entry;
        entry["id"] = pin.id;
        entry["name"] = pin.label.empty() ? pin.id : pin.label;
        entry["kind"] = pin.kind == graph::PinKind::Exec ? "exec" : "data";
        entry["dir"] = pin.direction == graph::PinDirection::In ? "in" : "out";
        entry["dataType"] = pin.type ? pin.type->toString() : std::string{};
        entry["optional"] = pin.isOptional;
        if (pin.declaredDefault) entry["defaultLiteral"] = *pin.declaredDefault;
        if (pin.description) entry["description"] = *pin.description;
        pins.push_back(std::move(entry));
    }

    nlohmann::json out;
    out["type"] = definition.id;
    out["label"] = definition.title;
    out["category"] = definition.category;
    out["kind"] = std::string(graph::graphNodeKindName(definition.kind));
    if (definition.summary) out["summary"] = *definition.summary;
    out["isPure"] = definition.isPure;
    out["isGlobal"] = definition.isGlobal;
    if (definition.ownerScript) out["script"] = *definition.ownerScript;
    out["pins"] = std::move(pins);
    return out;
}

nlohmann::json describeGraphDiagnostic(const graph::GraphDiagnostic& diagnostic) {
    nlohmann::json out;
    out["code"] = diagnostic.code;
    out["severity"] = diagnostic.severity == graph::GraphSeverity::Error ? "error" : "warning";
    out["message"] = diagnostic.message;
    if (diagnostic.nodeId) out["nodeId"] = *diagnostic.nodeId;
    if (diagnostic.pinId) out["pinId"] = *diagnostic.pinId;
    if (diagnostic.wireId) out["wireId"] = *diagnostic.wireId;
    out["relatedNodes"] = diagnostic.relatedNodes;
    out["line"] = diagnostic.sourceLine;
    return out;
}

std::string graphDiagnosticsJson(const std::vector<graph::GraphDiagnostic>& diagnostics) {
    nlohmann::json array = nlohmann::json::array();
    bool anyError = false;
    for (const auto& diagnostic : diagnostics) {
        array.push_back(describeGraphDiagnostic(diagnostic));
        if (diagnostic.severity == graph::GraphSeverity::Error) anyError = true;
    }
    nlohmann::json out;
    out["diagnostics"] = std::move(array);
    out["ok"] = !anyError;
    return out.dump(2);
}

std::vector<graph::GraphDiagnostic> singleDiagnostic(
    const std::optional<graph::GraphDiagnostic>& diagnostic) {
    if (diagnostic) return {*diagnostic};
    return {};
}

}  // namespace

GraphTarget::GraphTarget(platform::Services& platform, tools::Locator& locator)
    : platform_(platform), locator_(locator) {}

GraphTarget::~GraphTarget() = default;

graph::GraphCompiler& GraphTarget::compiler() {
    const auto roots = locator_.papyrusBaseImports();
    std::string key;
    for (const auto& root : roots) {
        key += root.string();
        key.push_back('|');
    }
    if (!compiler_ || key != rootsKey_) {
        index_ = std::make_unique<papyrus::ScriptIndex>(
            graph::graphIndexFor(nullptr, nullptr, roots));
        compiler_ = std::make_unique<graph::GraphCompiler>(*index_);
        rootsKey_ = std::move(key);
    }
    return *compiler_;
}

Result<nlohmann::json> GraphTarget::invoke(std::string_view method, const nlohmann::json& args) {
    // Must serialize: the RPC dispatcher calls invoke() concurrently.
    std::scoped_lock lock(mutex_);
    try {
        if (method == "Refresh") {
            compiler_.reset();
            index_.reset();
            rootsKey_.clear();
            return nlohmann::json(nullptr);
        }
        if (method == "GetCorePalette") {
            auto& palette = compiler().palette();
            nlohmann::json builtins = nlohmann::json::array();
            for (const auto& definition : palette.builtins()) {
                builtins.push_back(describeNodeDefinition(definition));
            }
            auto scripts = palette.scriptNames();
            std::sort(scripts.begin(), scripts.end(),
                      [](const std::string& a, const std::string& b) {
                          return std::lexicographical_compare(
                              a.begin(), a.end(), b.begin(), b.end(),
                              [](unsigned char x, unsigned char y) {
                                  return std::tolower(x) < std::tolower(y);
                              });
                      });
            const auto stats = palette.wikiStats();
            nlohmann::json out;
            out["version"] = graph::GraphDocument::kCurrentSchema;
            out["builtins"] = std::move(builtins);
            out["scripts"] = scripts;
            out["wiki"] = {{"available", stats.available()}, {"pages", stats.pagesIndexed}};
            return nlohmann::json(out.dump(2));
        }
        if (method == "SearchPalette") {
            const auto kind = stringArgument(args, 0);
            const auto query = stringArgument(args, 1);
            const auto scriptFilter = stringArgument(args, 2);
            const int requested = intNumberArgument(args, 3, 0, true);
            std::optional<std::string_view> filter;
            if (!isWhitespace(scriptFilter)) filter = std::string_view(scriptFilter);
            const auto result = compiler().palette().search(
                std::string_view(query), requested <= 0 ? 50 : requested, filter);

            const bool filterKind = !isWhitespace(kind) && !graphIEquals(kind, "any");
            nlohmann::json entries = nlohmann::json::array();
            for (const auto& entry : result.entries) {
                const auto kindName = std::string(graph::graphNodeKindName(entry.kind));
                if (filterKind && !graphIEquals(kindName, kind)) continue;
                entries.push_back({{"id", entry.id},
                                   {"title", entry.title},
                                   {"category", entry.category},
                                   {"kind", kindName},
                                   {"signature", entry.signature},
                                   {"isPure", entry.isPure}});
            }
            nlohmann::json out;
            out["entries"] = std::move(entries);
            out["total"] = result.total;
            return nlohmann::json(out.dump(2));
        }
        if (method == "GetNodeSignature") {
            const auto nodeType = stringArgument(args, 0);
            const auto* definition = compiler().palette().find(nodeType);
            if (definition == nullptr) {
                return nlohmann::json("Error: no node type '" + nodeType + "' on this palette");
            }
            return nlohmann::json(describeNodeDefinition(*definition).dump(2));
        }
        if (method == "ValidateGraph") {
            auto loaded = graph::documentJson::tryDeserialize(stringArgument(args, 0));
            if (!loaded.document) {
                return nlohmann::json(graphDiagnosticsJson(singleDiagnostic(loaded.error)));
            }
            const auto validation = compiler().validate(*loaded.document);
            return nlohmann::json(graphDiagnosticsJson(validation.diagnostics));
        }
        if (method == "CompileToSource") {
            auto loaded = graph::documentJson::tryDeserialize(stringArgument(args, 0));
            if (!loaded.document) {
                return nlohmann::json(graphDiagnosticsJson(singleDiagnostic(loaded.error)));
            }
            graph::GraphCompileOptions options;
            options.stopAfterSource = true;
            const auto result = compiler().compile(*loaded.document, options);
            nlohmann::json diagnostics = nlohmann::json::array();
            for (const auto& diagnostic : result.diagnostics) {
                diagnostics.push_back(describeGraphDiagnostic(diagnostic));
            }
            nlohmann::json out;
            if (result.source) out["source"] = *result.source;
            out["diagnostics"] = std::move(diagnostics);
            out["ok"] = result.errors().empty();
            return nlohmann::json(out.dump(2));
        }
        if (method == "CompileToPex") {
            auto loaded = graph::documentJson::tryDeserialize(stringArgument(args, 0));
            if (!loaded.document) {
                return nlohmann::json(graphDiagnosticsJson(singleDiagnostic(loaded.error)));
            }
            const auto outputDirectory = stringArgument(args, 1);
            const auto result = compiler().compile(*loaded.document);
            std::optional<std::string> written;
            if (result.success() && !isWhitespace(outputDirectory)) {
                const std::filesystem::path directory(outputDirectory);
                std::filesystem::create_directories(directory);
                const auto& name = loaded.document->header.scriptName;
                const auto pexPath = directory / (name + ".pex");
                writeFileBytes(pexPath, result.pex->toBytes());
                writeFileText(directory / (name + ".psc"), result.source.value_or(""));
                written = pexPath.string();
            }
            nlohmann::json diagnostics = nlohmann::json::array();
            for (const auto& diagnostic : result.diagnostics) {
                diagnostics.push_back(describeGraphDiagnostic(diagnostic));
            }
            nlohmann::json out;
            if (result.source) out["source"] = *result.source;
            out["diagnostics"] = std::move(diagnostics);
            out["ok"] = result.success();
            if (written) out["written"] = *written;
            return nlohmann::json(out.dump(2));
        }
        if (method == "LoadGraph") {
            const auto path = stringArgument(args, 0);
            if (!std::filesystem::is_regular_file(path)) {
                return nlohmann::json("Error: no file at " + path);
            }
            const auto text = readFileText(path);
            auto loaded = graph::documentJson::tryDeserialize(text);
            if (!loaded.document) {
                return nlohmann::json(
                    "Error: " +
                    (loaded.error ? loaded.error->message : std::string("malformed document")));
            }
            return nlohmann::json(text);
        }
        if (method == "LoadScript") {
            const auto result = graph::GraphScriptLoader::load(stringArgument(args, 0),
                                                               locator_.papyrusBaseImports());
            if (result.failure) return nlohmann::json("Error: " + *result.failure);
            nlohmann::json diagnostics = nlohmann::json::array();
            for (const auto& diagnostic : result.diagnostics) {
                diagnostics.push_back(describeGraphDiagnostic(diagnostic));
            }
            nlohmann::json out;
            out["ok"] = result.success();
            if (result.document) {
                out["document"] =
                    nlohmann::json::parse(graph::documentJson::serialize(*result.document, false));
            }
            out["diagnostics"] = std::move(diagnostics);
            return nlohmann::json(out.dump(2));
        }
        if (method == "SaveGraph") {
            const auto path = stringArgument(args, 0);
            auto loaded = graph::documentJson::tryDeserialize(stringArgument(args, 1));
            if (!loaded.document) {
                return nlohmann::json(
                    "Error: " +
                    (loaded.error ? loaded.error->message : std::string("malformed document")));
            }
            graph::documentJson::saveFile(*loaded.document, path);
            return nlohmann::json("");
        }
        if (method == "BrowseForGraph") {
            platform::FileDialogOptions options;
            options.filter = "Node graph (*.fograph)|*.fograph";
            if (boolArgument(args, 0)) {
                options.title = "Save graph";
                return pathResult(platform_.pickSavePath(options));
            }
            options.title = "Open graph";
            return pathResult(platform_.pickFile(options));
        }
        if (method == "BrowseForScript") {
            platform::FileDialogOptions options;
            options.title = "Open graph or script";
            options.filter =
                "Graph or script (*.fograph;*.psc;*.pex)|*.fograph;*.psc;*.pex"
                "|Node graph (*.fograph)|*.fograph"
                "|Papyrus source (*.psc)|*.psc"
                "|Compiled Papyrus (*.pex)|*.pex";
            return pathResult(platform_.pickFile(options));
        }
        return std::unexpected(
            Error{"UnknownMethod", "Unknown Graph RPC method: " + std::string(method)});
    } catch (const std::exception& ex) {
        return nlohmann::json("Error: " + std::string(ex.what()));
    }
}

}
