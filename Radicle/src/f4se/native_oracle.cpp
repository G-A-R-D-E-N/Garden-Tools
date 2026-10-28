#include <fo4/f4se/native_oracle.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

#include <fo4/papyrus/ast.hpp>
#include <fo4/papyrus/parser.hpp>

namespace fo4::f4se {

namespace {

std::string toLower(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (const auto ch : text) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return out;
}

bool readFile(const std::filesystem::path& path, std::string& out) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return false;
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    out = buffer.str();
    return true;
}

PapyrusTypeText typeTextOf(const std::optional<papyrus::TypeRef>& reference) {
    if (!reference) return PapyrusTypeText{"None", false};
    return PapyrusTypeText{reference->name, reference->isArray};
}

std::vector<OracleNative> nativesOf(const papyrus::Script& script,
                                    const std::string& scriptName) {
    std::vector<OracleNative> out;
    for (const auto& function : script.functions) {
        if (!function.isNative) continue;
        OracleNative native;
        native.script = scriptName;
        native.name = function.name;
        native.returnType = typeTextOf(function.returnType);
        native.isGlobal = function.isGlobal;
        for (const auto& parameter : function.parameters) {
            native.parameterTypes.push_back(typeTextOf(parameter.type));
        }
        out.push_back(std::move(native));
    }
    return out;
}

std::optional<papyrus::Script> parseRepaired(const std::filesystem::path& path,
                                             std::vector<std::string>& problems,
                                             int& repaired) {
    std::string text;
    if (!readFile(path, text)) {
        problems.push_back(path.filename().string() + ": could not read the file");
        return std::nullopt;
    }

    auto script = papyrus::Parser::parse(text, path.string());
    if (!script.name.empty()) return script;

    repaired++;
    const auto name = path.stem().string();
    auto patched = papyrus::Parser::parse("Scriptname " + name + "\n" + text, path.string());
    if (patched.name.empty()) {
        problems.push_back(path.filename().string() +
                           ": still has no script name after the synthetic header");
        return std::nullopt;
    }
    return patched;
}

}

const OracleNative* NativeOracle::find(const std::string& script,
                                       const std::string& name) const {
    for (const auto& native : natives_) {
        if (CiEqual{}(native.script, script) && CiEqual{}(native.name, name)) return &native;
    }
    return nullptr;
}

NativeOracle NativeOracle::build(const std::filesystem::path& mergedRoot,
                                 const std::filesystem::path* vanillaRoot) {
    std::vector<std::string> problems;
    std::vector<OracleNative> natives;
    int scripts = 0;
    int repaired = 0;

    if (!std::filesystem::exists(mergedRoot)) {
        problems.push_back("merged root '" + mergedRoot.string() + "' does not exist");
        return NativeOracle(natives, 0, 0, std::move(problems));
    }

    std::map<std::string, std::filesystem::path, CiLess> vanillaByName;
    if (vanillaRoot != nullptr && std::filesystem::exists(*vanillaRoot)) {
        for (const auto& entry : std::filesystem::directory_iterator(*vanillaRoot)) {
            if (!entry.is_regular_file()) continue;
            const auto path = entry.path();
            if (path.extension() != ".psc") continue;
            vanillaByName[path.stem().string()] = path;
        }
    }

    std::vector<std::filesystem::path> mergedFiles;
    for (const auto& entry : std::filesystem::directory_iterator(mergedRoot)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() == ".psc") mergedFiles.push_back(entry.path());
    }
    std::sort(mergedFiles.begin(), mergedFiles.end());

    for (const auto& path : mergedFiles) {
        scripts++;
        auto merged = parseRepaired(path, problems, repaired);
        if (!merged) continue;

        const auto scriptName = merged->name;
        auto mine = nativesOf(*merged, scriptName);

        const auto vanillaFound = vanillaByName.find(path.stem().string());
        if (vanillaFound != vanillaByName.end()) {
            auto vanilla = parseRepaired(vanillaFound->second, problems, repaired);
            if (vanilla) {
                std::set<std::pair<std::string, int>> baseline;
                for (const auto& native : nativesOf(*vanilla, scriptName)) {
                    baseline.insert({toLower(native.name), native.arity()});
                }
                std::vector<OracleNative> filtered;
                for (auto& native : mine) {
                    if (baseline.find({toLower(native.name), native.arity()}) ==
                        baseline.end()) {
                        filtered.push_back(std::move(native));
                    }
                }
                mine = std::move(filtered);
            }
        }

        for (auto& native : mine) natives.push_back(std::move(native));
    }

    return NativeOracle(std::move(natives), scripts, repaired, std::move(problems));
}

}
