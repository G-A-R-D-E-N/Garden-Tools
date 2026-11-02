#include <fo4ide/app/ServiceTargets.hpp>
#include <fo4ide/app/WikiService.hpp>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <system_error>
#include <utility>
#include <fo4/papyrus/analysis_service.hpp>
#include <fo4/papyrus/decompile_service.hpp>

#include "PapyrusStaging.hpp"
#include "ServiceTargetSupport.hpp"

namespace fo4ide::app {

using namespace support;

namespace {

std::string stripQuotes(std::string value) {
    value = trim(std::move(value));
    while (!value.empty() && value.front() == '"') value.erase(value.begin());
    while (!value.empty() && value.back() == '"') value.pop_back();
    return value;
}

bool endsWithIgnoreCase(std::string_view value, std::string_view suffix) {
    return value.size() >= suffix.size() &&
           toLower(std::string(value.substr(value.size() - suffix.size()))) == toLower(std::string(suffix));
}

std::vector<std::filesystem::path> enumeratePscFiles(const std::filesystem::path& root) {
    std::vector<std::filesystem::path> files;
    std::vector<std::filesystem::path> stack{root};
    std::error_code ec;
    while (!stack.empty()) {
        const auto dir = stack.back();
        stack.pop_back();
        for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
            std::error_code statusEc;
            const auto& entry = *it;
            if (entry.is_symlink(statusEc)) continue;
            if (entry.is_directory(statusEc)) {
                stack.push_back(entry.path());
            } else if (entry.is_regular_file(statusEc) &&
                       endsWithIgnoreCase(entry.path().extension().string(), ".psc")) {
                files.push_back(entry.path());
            }
        }
    }
    return files;
}

struct CiLess {
    bool operator()(const std::string& left, const std::string& right) const {
        return toLower(left) < toLower(right);
    }
};

std::string diagnoseDependencies(const std::string& compilerOutput) {
    const std::regex patterns[] = {
        std::regex("unknown type (\\S+)"),
        std::regex("(\\S+) is not a known user-defined script type"),
        std::regex("cannot convert to unknown type (\\S+)"),
        std::regex("\\b([A-Za-z_]\\w*) is not a function or does not exist"),
        std::regex("[Ss]cript '?([A-Za-z_][\\w:]*)'? (?:does not exist|could not be "
                   "found|is not imported)"),
    };

    std::set<std::string, CiLess> symbols;
    for (const auto& pattern : patterns) {
        std::smatch match;
        auto cursor = compilerOutput.cbegin();
        while (std::regex_search(cursor, compilerOutput.cend(), match, pattern)) {
            auto symbol = trim(match[1].str());
            while (!symbol.empty() && (symbol.front() == '"' || symbol.front() == '\'')) {
                symbol.erase(symbol.begin());
            }
            while (!symbol.empty() && (symbol.back() == '"' || symbol.back() == '\'')) {
                symbol.pop_back();
            }
            if (!symbol.empty()) symbols.insert(symbol);
            cursor = match.suffix().first;
        }
    }
    if (symbols.empty()) return "";

    struct KnownExtender {
        std::string_view match;
        std::string_view name;
        std::string_view url;
    };
    constexpr KnownExtender kKnownExtenders[] = {
        {"hudframework", "HUDFramework", "https://www.nexusmods.com/fallout4/mods/20309"},
        {"workshopframework", "Workshop Framework", "https://www.nexusmods.com/fallout4/mods/35004"},
        {"wsfw", "Workshop Framework", "https://www.nexusmods.com/fallout4/mods/35004"},
        {"mcm", "Mod Configuration Menu", "https://www.nexusmods.com/fallout4/mods/21497"},
        {"baka", "Baka Framework", "https://www.nexusmods.com/fallout4/search/?gsearch=Baka+Framework"
                                  "&gsearchtype=mods"},
    };

    const auto escape = [](std::string_view value) {
        std::ostringstream out;
        for (const unsigned char c : value) {
            if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
                out << static_cast<char>(c);
            } else {
                out << '%' << std::uppercase << std::hex << static_cast<int>(c) << std::nouppercase
                    << std::dec;
            }
        }
        return out.str();
    };

    std::ostringstream sb;
    sb << "--- DEPENDENCY HELP ---\n";
    sb << "F4SE + vanilla base scripts are already on the import path. A remaining unresolved\n";
    sb << "type/function usually means another script extender / framework is required:\n";
    sb << "add that mod's Data\\Scripts\\Source folder to the Import roots and recompile.\n\n";
    for (const auto& symbol : symbols) {
        const auto low = toLower(symbol);
        const KnownExtender* known = nullptr;
        for (const auto& candidate : kKnownExtenders) {
            if (low.find(candidate.match) != std::string::npos) {
                known = &candidate;
                break;
            }
        }
        if (known) {
            sb << "  - " << symbol << "  ->  likely " << known->name << ".  Nexus: " << known->url
               << "\n";
        } else if (low.find(':') != std::string::npos) {
            sb << "  - " << symbol << "  ->  another script in the SAME mod (namespaced). Make sure "
                  "its .psc/.pex is present in the source folder (some mods ship helper scripts as "
                  "source-only, with no .pex to decompile).\n";
        } else {
            sb << "  - " << symbol << "  ->  unknown source (a script extender plugin or framework). "
                  "Search Nexus: https://www.nexusmods.com/fallout4/search/?gsearch=" << escape(symbol)
               << "&gsearchtype=mods\n";
        }
    }
    return sb.str();
}

enum class PapyrusEngine { Auto, BuiltIn, CreationKit };

PapyrusEngine parseEngine(std::string name) {
    name = toLower(trim(std::move(name)));
    if (name == "builtin" || name == "built-in" || name == "native" || name == "internal") {
        return PapyrusEngine::BuiltIn;
    }
    if (name == "creationkit" || name == "creation-kit" || name == "ck" || name == "external") {
        return PapyrusEngine::CreationKit;
    }
    return PapyrusEngine::Auto;
}


}

PapyrusTarget::PapyrusTarget(process::Runner& runner,
                             tools::Locator& locator,
                             platform::Services& platform)
    : runner_(runner), locator_(locator), platform_(platform) {}

Result<nlohmann::json> PapyrusTarget::invoke(std::string_view method,
                                             const nlohmann::json& args) {
    if (method == "BrowseForFile") {
        return pathResult(platform_.pickFile({stringArgument(args, 0), stringArgument(args, 1), {}}));
    }
    if (method == "BrowseForFolder") {
        return pathResult(platform_.pickFolder({stringArgument(args, 0), {}, {}}));
    }
    if (method == "Compile") return compile(args);
    if (method == "Decompile") return decompile(args);
    if (method == "Check") {
        try {
            const auto source = stripQuotes(stringArgument(args, 0));
            const auto* semanticValue = argument(args, 1);
            const bool semantic = !semanticValue || semanticValue->is_null() || boolArgument(args, 1);
            const auto imports = stripQuotes(stringArgument(args, 2));
            const std::string* importsPtr = imports.empty() ? nullptr : &imports;
            return nlohmann::json(fo4::papyrus::AnalysisService::check(
                source, true, semantic, importsPtr, locator_.papyrusBaseImports()));
        } catch (const std::exception& ex) {
            return nlohmann::json("Error: " + std::string(ex.what()));
        }
    }
    if (method == "Analyze") {
        try {
            const auto text = stringArgument(args, 0);
            const auto path = stringArgument(args, 1);
            const std::string* pathPtr = isWhitespace(path) ? nullptr : &path;
            return nlohmann::json(fo4::papyrus::AnalysisService::analyzeJson(text, pathPtr));
        } catch (const std::exception& ex) {
            return nlohmann::json(nlohmann::json{{"error", ex.what()}}.dump());
        }
    }
    if (method == "SymbolAt") {
        try {
            const auto text = stringArgument(args, 0);
            const auto path = stringArgument(args, 1);
            const auto offset = intNumberArgument(args, 2, 0);
            const auto imports = stringArgument(args, 3);
            const std::string* pathPtr = isWhitespace(path) ? nullptr : &path;
            const std::string* importsPtr = isWhitespace(imports) ? nullptr : &imports;
            return nlohmann::json(fo4::papyrus::AnalysisService::symbolAtJson(
                text, pathPtr, offset, importsPtr, locator_.papyrusBaseImports()));
        } catch (const std::exception& ex) {
            return nlohmann::json(nlohmann::json{{"error", ex.what()}}.dump());
        }
    }
    if (method == "ReadScript") {
        const auto path = trim(stringArgument(args, 0));
        if (path.empty()) return nlohmann::json("ERR:No path.");
        if (!std::filesystem::is_regular_file(path)) return nlohmann::json("ERR:Not found: " + path);
        try {
            return nlohmann::json(readFileText(path));
        } catch (const std::exception& ex) {
            return nlohmann::json("ERR:" + std::string(ex.what()));
        }
    }
    if (method == "WriteScript") {
        auto path = trim(stringArgument(args, 0));
        if (path.empty()) return nlohmann::json("ERR:No path.");
        if (!endsWithIgnoreCase(path, ".psc")) {
            return nlohmann::json("ERR:Refusing to write '" +
                                  std::filesystem::path(path).filename().string() +
                                  "' -- this saves Papyrus source, so the path must end in .psc.");
        }
        try {
            const auto parent = std::filesystem::path(path).parent_path();
            if (!parent.empty()) {
                std::error_code ec;
                std::filesystem::create_directories(parent, ec);
            }
            writeFileText(path, stringArgument(args, 1));
            return nlohmann::json("");
        } catch (const std::exception& ex) {
            return nlohmann::json("ERR:" + std::string(ex.what()));
        }
    }
    if (method == "OpenFolder") {
        auto path = stripQuotes(stringArgument(args, 0));
        if (path.empty()) return nlohmann::json("No path.");
        if (std::filesystem::is_regular_file(path)) {
            path = std::filesystem::path(path).parent_path().string();
        }
        if (!std::filesystem::is_directory(path)) return nlohmann::json("Folder not found: " + path);
        auto opened = platform_.openFolder(path);
        return nlohmann::json(opened ? std::string{} : "Error: " + opened.error().message);
    }
    if (method == "StageDroppedFile") return stageDroppedFile(args);
    if (method == "LookupFunction") {
        try {
            const auto root = locator_.resolve(tools::Tool::CkWiki).string();
            return nlohmann::json(wiki::lookupFunction(root, stringArgument(args, 0),
                                                       stringArgument(args, 1)));
        } catch (const std::exception& ex) {
            return nlohmann::json("Error: " + std::string(ex.what()));
        }
    }
    if (method == "LookupScriptInfo") {
        try {
            const auto root = locator_.resolve(tools::Tool::CkWiki).string();
            return nlohmann::json(wiki::lookupScriptInfo(root, stringArgument(args, 0)));
        } catch (const std::exception& ex) {
            return nlohmann::json("Error: " + std::string(ex.what()));
        }
    }
    return std::unexpected(Error{"UnknownMethod", "Unknown Papyrus RPC method: " + std::string(method)});
}

Result<nlohmann::json> PapyrusTarget::compile(const nlohmann::json& args) {
    auto source = stripQuotes(stringArgument(args, 0));
    if (source.empty()) {
        return nlohmann::json("Provide 'source' (a .psc file or a folder of scripts).");
    }
    const bool isDir = std::filesystem::is_directory(source);
    const bool isFile = std::filesystem::is_regular_file(source);
    if (!isDir && !isFile) return nlohmann::json("Source not found: " + source);

    const auto engine = parseEngine(stringArgument(args, 8));
    auto exePath = stripQuotes(stringArgument(args, 7));
    if (exePath.empty()) exePath = locator_.resolve(tools::Tool::PapyrusCompiler).string();
    const bool haveCreationKit = !exePath.empty() && std::filesystem::is_regular_file(exePath);

    if (engine == PapyrusEngine::BuiltIn || (engine == PapyrusEngine::Auto && !haveCreationKit)) {
        const auto output = stripQuotes(stringArgument(args, 1));
        const auto imports = stripQuotes(stringArgument(args, 2));
        const auto flags = stripQuotes(stringArgument(args, 3));
        const bool optimize = boolArgument(args, 5);
        const bool release = boolArgument(args, 6);
        const auto* debugInfoValue = argument(args, 9);
        const bool debugInfo =
            (!debugInfoValue || debugInfoValue->is_null() || !debugInfoValue->is_boolean())
                ? true
                : debugInfoValue->get<bool>();
        const std::string* outputPtr = output.empty() ? nullptr : &output;
        const std::string* importsPtr = imports.empty() ? nullptr : &imports;
        const std::string* flagsPtr = flags.empty() ? nullptr : &flags;
        auto result = fo4::papyrus::AnalysisService::compile(source, outputPtr, importsPtr, release,
                                                             debugInfo, flagsPtr,
                                                             locator_.papyrusBaseImports());
        if (engine == PapyrusEngine::Auto) {
            result += "\n\nNo PapyrusCompiler.exe was found, so the built-in compiler was used. "
                      "That is not a fallback to something lesser -- it is measured against the "
                      "Creation Kit's own output on real scripts -- but if you meant to use the CK, "
                      "point at it with compiler_path or " +
                      std::string(locator_.describe(tools::Tool::PapyrusCompiler)) + ".";
        }
        if (optimize) {
            result += "\n\nNote: optimize (-op) is a Creation Kit compiler switch and was ignored.";
        }
        return nlohmann::json(result);
    }

    if (!haveCreationKit) {
        return nlohmann::json("PapyrusCompiler.exe not found, and engine='creationkit' was asked "
                              "for explicitly. Pass compiler_path pointing at it, or " +
                              std::string(locator_.describe(tools::Tool::PapyrusCompiler)) +
                              ". Or use engine='builtin', which needs no Creation Kit.");
    }

    if (isFile && endsWithIgnoreCase(source, ".pas")) {
        return nlohmann::json("That's a Papyrus ASSEMBLY file (.pas), not source. PapyrusCompiler "
                              "compiles .psc SOURCE.\nFix: Decompile this script again with "
                              "'Assembly listing' UNCHECKED to get a .psc, then compile that.\n"
                              "(.pas is only used by PapyrusAssembler.exe, a separate tool, and is "
                              "not what you recompile.)");
    }

    std::string workDir;
    std::string target;
    std::optional<std::filesystem::path> stagingDir;
    if (isDir) {
        const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
        auto staged = stageNamespacedScripts(
            enumeratePscFiles(source),
            std::filesystem::temp_directory_path() / ("FO4RE_PapyrusStage_" + std::to_string(ticks)));
        if (!staged) return nlohmann::json(staged.error().message);
        stagingDir = *staged;
        workDir = stagingDir ? stagingDir->string() : source;
        target = workDir;
    } else {
        const auto full = std::filesystem::absolute(source);
        const auto fileDir = full.parent_path();
        std::string objName;
        try {
            objName = extractScriptName(readFileText(full));
        } catch (...) {
        }
        std::vector<std::string> segments;
        {
            std::istringstream stream(objName);
            std::string part;
            while (std::getline(stream, part, ':')) {
                if (!part.empty()) segments.push_back(part);
            }
        }
        const auto depth = static_cast<int>(segments.size()) - 1;
        std::filesystem::path root = fileDir;
        bool layoutMatches = depth > 0;
        for (int i = 0; i < depth; ++i) {
            if (toLower(root.filename().string()) !=
                toLower(segments[static_cast<std::size_t>(depth - 1 - i)])) {
                layoutMatches = false;
                break;
            }
            root = root.parent_path();
            if (root.empty()) {
                layoutMatches = false;
                break;
            }
        }
        if (layoutMatches) {
            workDir = root.string();
            target = objName;
        } else {
            workDir = fileDir.string();
            target = full.filename().string();
        }
    }

    std::vector<std::string> imp;
    const auto importsText = stringArgument(args, 2);
    if (!isWhitespace(importsText)) {
        std::istringstream stream(importsText);
        std::string part;
        while (std::getline(stream, part, ';')) {
            auto cleaned = stripQuotes(part);
            if (!cleaned.empty()) imp.push_back(std::move(cleaned));
        }
    }
    for (const auto& base : locator_.papyrusBaseImports()) imp.push_back(base.string());
    imp.push_back(workDir);

    std::vector<std::string> distinct;
    std::set<std::string, CiLess> seen;
    for (auto& entry : imp) {
        if (seen.insert(entry).second) distinct.push_back(std::move(entry));
    }
    std::ostringstream importArg;
    for (std::size_t i = 0; i < distinct.size(); ++i) {
        if (i) importArg << ';';
        importArg << distinct[i];
    }

    const auto outArg = stripQuotes(stringArgument(args, 1));
    const auto outDir = outArg.empty() ? (isDir ? source : workDir) : outArg;
    std::error_code ec;
    std::filesystem::create_directories(outDir, ec);
    if (ec) {
        return nlohmann::json("Cannot create output dir '" + outDir + "': " + ec.message());
    }

    auto flags = stripQuotes(stringArgument(args, 3));
    if (flags.empty()) flags = "Institute_Papyrus_Flags.flg";
    const bool all = boolArgument(args, 4);
    const bool optimize = boolArgument(args, 5);
    const bool release = boolArgument(args, 6);

    std::vector<std::string> processArgs{
        target, "-f=\"" + flags + "\"", "-i=\"" + importArg.str() + "\"",
        "-o=\"" + outDir + "\""};
    if (all || isDir) processArgs.push_back("-all");
    if (optimize) processArgs.push_back("-op");
    if (release) processArgs.push_back("-r");

    std::ostringstream displayArgs;
    for (std::size_t i = 0; i < processArgs.size(); ++i) {
        if (i) displayArgs << ' ';
        displayArgs << (i == 0 ? "\"" + processArgs[i] + "\"" : processArgs[i]);
    }

    process::Request request;
    request.executable = exePath;
    request.arguments = processArgs;
    request.workingDirectory = workDir;
    request.timeout = std::chrono::seconds(300);
    auto run = runner_.run(request);
    if (!run) return nlohmann::json("Failed to run the Papyrus compiler: " + run.error().message);
    if (!run->started) return nlohmann::json("Failed to start the Papyrus compiler process.");
    if (run->timedOut) return nlohmann::json("Papyrus compile timed out after 300s (killed).");

    std::string combined;
    combined.reserve(run->stdOut.size() + run->stdErr.size() + 1);
    for (const char c : run->stdOut + "\n" + run->stdErr) {
        if (c != '\r') combined.push_back(c);
    }

    std::ostringstream sb;
    std::string batch;
    {
        std::istringstream lines(combined);
        std::string line;
        while (std::getline(lines, line)) {
            if (toLower(line).find("batch compile") != std::string::npos) batch = line;
        }
    }
    std::smatch batchMatch;
    if (std::regex_search(batch, batchMatch, std::regex(R"((\d+)\s+succeeded,\s+(\d+)\s+failed)"))) {
        sb << "RESULT: " << batchMatch[1].str() << " succeeded, " << batchMatch[2].str()
           << " failed (output -> " << outDir << ")\n";
    } else {
        sb << "RESULT: " << (run->exitCode == 0 ? "success" : "FAILED")
           << " (output -> " << outDir << ")\n";
    }
    sb << "\ncompiler: " << exePath << "\n";
    sb << "workdir : " << workDir << "\n";
    sb << "args    : " << displayArgs.str() << "\n\n";

    std::vector<std::string> important;
    {
        std::istringstream lines(combined);
        std::string line;
        while (std::getline(lines, line)) {
            if (trim(line).empty()) continue;
            const auto low = toLower(line);
            if (low.find("error") != std::string::npos ||
                low.find("compilation succeeded") != std::string::npos ||
                low.find("failed") != std::string::npos ||
                low.find("no output generated") != std::string::npos ||
                low.find("batch compile") != std::string::npos) {
                important.push_back(line);
            }
        }
    }
    if (!important.empty()) {
        for (const auto& line : important) sb << line << "\n";
    } else if (!trim(combined).empty()) {
        sb << trim(combined) << "\n";
    }
    sb << "\nexit code: " << run->exitCode << " (" << (run->exitCode == 0 ? "success" : "FAILED")
       << ")\n";
    if (run->exitCode != 0) {
        const auto hint = diagnoseDependencies(combined);
        if (!hint.empty()) sb << "\n" << hint;
    }
    if (stagingDir) {
        std::error_code cleanupEc;
        std::filesystem::remove_all(*stagingDir, cleanupEc);
    }
    return nlohmann::json(sb.str());
}

Result<nlohmann::json> PapyrusTarget::decompile(const nlohmann::json& args) {
    return nlohmann::json(fo4::papyrus::DecompileService::decompile(
        stripQuotes(stringArgument(args, 0)), stripQuotes(stringArgument(args, 1)),
        boolArgument(args, 2), boolArgument(args, 3)));
}

Result<nlohmann::json> PapyrusTarget::stageDroppedFile(const nlohmann::json& args) const {
    try {
        auto name = std::filesystem::path(stringArgument(args, 0)).filename();
        if (name.empty()) name = "dropped.bin";
        auto decoded = decodeBase64(stringArgument(args, 1));
        if (!decoded) return nlohmann::json("ERR:" + decoded.error().message);

        const auto directory = uniqueStageDirectory();
        std::filesystem::create_directories(directory);
        const auto path = directory / name;
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        if (!stream) return nlohmann::json("ERR:Could not create staged file");
        stream.write(reinterpret_cast<const char*>(decoded->data()),
                     static_cast<std::streamsize>(decoded->size()));
        stream.close();
        if (!stream) return nlohmann::json("ERR:Could not write staged file");
        return nlohmann::json(path.string());
    } catch (const std::exception& ex) {
        return nlohmann::json("ERR:" + std::string(ex.what()));
    }
}

}
