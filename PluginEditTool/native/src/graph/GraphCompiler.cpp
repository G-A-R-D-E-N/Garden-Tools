#include <fo4ide/graph/GraphCompiler.hpp>

#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <random>
#include <system_error>

#include <fo4ide/papyrus/Parser.hpp>

namespace fo4ide::graph {

namespace {

bool isBlank(std::string_view text) {
    for (const char ch : text) {
        if (!std::isspace(static_cast<unsigned char>(ch))) return false;
    }
    return true;
}

std::string lower(std::string_view text) {
    std::string out(text);
    for (auto& ch : out) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return out;
}

std::string randomHex() {
    std::random_device device;
    std::string out;
    out.reserve(32);
    for (int i = 0; i < 32; ++i) {
        const auto value = device() & 0x0F;
        out.push_back(value < 10 ? static_cast<char>('0' + value)
                                 : static_cast<char>('a' + value - 10));
    }
    return out;
}

void writeTextFile(const std::filesystem::path& path, const std::string& text) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) throw std::runtime_error("Could not write " + path.string());
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
}

class ScratchBase {
public:
    ~ScratchBase() { clear(); }

    std::filesystem::path root() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (root_.empty()) {
            const auto parent = parentPath();
            sweepOld(parent);
            root_ = parent / ("p-" + randomHex());
        }
        return root_;
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (root_.empty()) return;
        std::error_code ignored;
        std::filesystem::remove_all(root_, ignored);
        root_.clear();
    }

    static std::filesystem::path parentPath() {
        return std::filesystem::temp_directory_path() / "fo4re-graph-scratch";
    }

private:
    static void sweepOld(const std::filesystem::path& parent) {
        std::error_code error;
        const auto cutoff = std::filesystem::file_time_type::clock::now() - std::chrono::hours(24);
        std::size_t examined = 0;
        for (std::filesystem::directory_iterator it(parent, error), end;
             !error && it != end && examined < 64; it.increment(error)) {
            ++examined;
            std::error_code entryError;
            if (std::filesystem::last_write_time(it->path(), entryError) < cutoff && !entryError) {
                std::filesystem::remove_all(it->path(), entryError);
            }
        }
    }

    std::mutex mutex_;
    std::filesystem::path root_;
};

ScratchBase& scratchBase() {
    static ScratchBase base;
    return base;
}

bool hasError(const std::vector<GraphDiagnostic>& diagnostics) {
    for (const auto& diagnostic : diagnostics) {
        if (diagnostic.severity == GraphSeverity::Error) return true;
    }
    return false;
}

}  // namespace

std::filesystem::path GraphCompiler::scratchRoot() { return scratchBase().root(); }

void GraphCompiler::clearScratch() { scratchBase().clear(); }

GraphCompiler::GraphCompiler(papyrus::ScriptIndex& index, NodePalette* palette)
    : index_(index) {
    if (palette) {
        palette_ = palette;
    } else {
        ownedPalette_ = std::make_unique<NodePalette>(index);
        palette_ = ownedPalette_.get();
    }
}

papyrus::ScriptIndex GraphCompiler::indexFor(
    const std::vector<std::filesystem::path>& roots) {
    papyrus::ScriptIndex index;
    for (const auto& root : roots) index.addRoot(root);
    return index;
}

GraphValidation GraphCompiler::validate(GraphDocument& document) {
    GraphValidator validator(index_, *palette_);
    return validator.validate(document);
}

GraphCompileResult GraphCompiler::compile(GraphDocument& document, GraphCompileOptions options) {
    auto validation = validate(document);
    std::vector<GraphDiagnostic> problems = validation.diagnostics;
    if (!validation.ok()) {
        GraphCompileResult result;
        result.diagnostics = std::move(problems);
        result.sourcesComplete = validation.sourcesComplete;
        return result;
    }

    const auto owner = ownerScriptFor(document);
    GraphTypeResolver types(index_, document.header.scriptName, document.header.extends);
    GraphLinearizer linearizer(document, validation, types,
                               owner ? &*owner : nullptr);

    auto ir = linearizer.lower(index_);
    const auto& linearizerDiagnostics = linearizer.diagnostics();
    problems.insert(problems.end(), linearizerDiagnostics.begin(), linearizerDiagnostics.end());

    if (ir == nullptr || hasError(problems)) {
        GraphCompileResult result;
        result.ir = std::move(ir);
        result.diagnostics = std::move(problems);
        result.sourcesComplete = types.sourcesComplete();
        return result;
    }

    PapyrusSourceWriter writer(options.writer);
    GraphSourceMap map;
    const std::string source = writer.write(*ir, map);
    if (options.stopAfterSource) {
        GraphCompileResult result;
        result.source = source;
        result.sourceMap = std::move(map);
        result.ir = std::move(ir);
        result.diagnostics = std::move(problems);
        result.sourcesComplete = types.sourcesComplete();
        return result;
    }

    const auto fileName = document.header.scriptName + ".psc";
    publishForSelfReference(document.header.scriptName, source);

    auto parsed = papyrus::Parser::parse(source, fileName);
    auto compiled = papyrus::compileScript(index_, &parsed, fileName, options.papyrus);

    auto papyrusDiagnostics = std::move(compiled.diagnostics);
    for (const auto& diagnostic : papyrusDiagnostics) {
        problems.push_back(mapBack(diagnostic, map, options));
    }

    GraphCompileResult result;
    result.source = source;
    result.sourceMap = std::move(map);
    result.ir = std::move(ir);
    result.pex = std::move(compiled.pex);
    result.diagnostics = std::move(problems);
    result.papyrusDiagnostics = std::move(papyrusDiagnostics);
    result.sourcesComplete = types.sourcesComplete() && compiled.sourcesComplete;
    return result;
}

GraphCompileResult GraphCompiler::compileFile(const std::filesystem::path& graphPath,
                                              GraphCompileOptions options) {
    std::ifstream stream(graphPath, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(stream)),
                           std::istreambuf_iterator<char>());
    auto loaded = documentJson::tryDeserialize(text);

    if (!loaded.document) {
        GraphCompileResult result;
        if (loaded.error) result.diagnostics.push_back(*loaded.error);
        return result;
    }

    return compile(*loaded.document, std::move(options));
}

GraphCompileResult GraphCompiler::compileToFile(GraphDocument& document,
                                                const std::filesystem::path& pscPath,
                                                std::optional<std::filesystem::path> pexPath,
                                                GraphCompileOptions options) {
    (void)pexPath;  // The PexWriter is not ported yet; only the .psc is emitted.
    auto result = compile(document, std::move(options));
    if (!result.source) return result;

    const auto directory = pscPath.parent_path();
    if (!directory.empty()) std::filesystem::create_directories(directory);
    writeTextFile(pscPath, *result.source);
    return result;
}

void GraphCompiler::publishForSelfReference(const std::string& scriptName,
                                            const std::string& source) {
    if (isBlank(scriptName)) return;

    try {
        const auto fileName = [&] {
            std::string out;
            out.reserve(scriptName.size());
            for (const char ch : scriptName) out.push_back(ch == ':' ? '_' : ch);
            return out + ".psc";
        }();

        const auto key = lower(scriptName);
        const auto known = scratchRoots_.find(key);
        if (known != scratchRoots_.end()) {
            const auto existing = known->second / fileName;
            std::filesystem::create_directories(known->second);
            writeTextFile(existing, source);
            index_.invalidate(existing);
            return;
        }

        const auto root = scratchBase().root() / randomHex();
        std::filesystem::create_directories(root);

        const auto path = root / fileName;
        writeTextFile(path, source);

        index_.addRoot(root);
        scratchRoots_[key] = root;
    } catch (const std::exception&) {
        // The C# swallows IOException and UnauthorizedAccessException here.
    }
}

std::optional<papyrus::Script> GraphCompiler::ownerScriptFor(
    const GraphDocument& document) const {
    if (isBlank(document.header.scriptName)) return std::nullopt;

    std::string header = "Scriptname " + document.header.scriptName;
    if (document.header.extends && !isBlank(*document.header.extends)) {
        header += " extends " + *document.header.extends;
    }
    for (const auto& import : document.header.imports) header += "\nImport " + import;
    header += "\n";
    return papyrus::Parser::parse(header, document.header.scriptName + ".psc");
}

GraphDiagnostic GraphCompiler::mapBack(const papyrus::Diagnostic& diagnostic,
                                       const GraphSourceMap& map,
                                       const GraphCompileOptions& options) {
    const auto severity = diagnostic.severity == papyrus::Severity::Error
                              ? GraphSeverity::Error
                              : GraphSeverity::Warning;

    auto entry = map.find(diagnostic.span.start);
    if (!entry) entry = map.findByLine(diagnostic.span.line);
    if (!entry) entry = map.functionAt(diagnostic.span.start);

    if (entry) {
        const std::string note =
            options.treatPapyrusErrorsAsInternalFaults && severity == GraphSeverity::Error
                ? " The graph validator should have caught this."
                : "";

        GraphDiagnostic result;
        result.code = std::string(codes::kInternalEmitterFault);
        result.severity = severity;
        result.message = diagnostic.message + note;
        result.nodeId = entry->nodeId;
        result.pinId = entry->pinId;
        result.sourceLine = diagnostic.span.line;
        result.papyrusCode = std::string(papyrus::codeString(diagnostic.code));
        return result;
    }

    GraphDiagnostic result;
    result.code = std::string(codes::kInternalEmitterFault);
    result.severity = severity;
    result.message = diagnostic.message + " (generated line " +
                     std::to_string(diagnostic.span.line) +
                     ", which no node claims: this is a defect in the compiler)";
    result.sourceLine = diagnostic.span.line;
    result.papyrusCode = std::string(papyrus::codeString(diagnostic.code));
    return result;
}

}  // namespace fo4ide::graph
