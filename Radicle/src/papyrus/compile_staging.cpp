#include <fo4/papyrus/compile_staging.hpp>

#include <fo4/papyrus/script_names.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace fo4::papyrus {

namespace {

bool isWhitespace(std::string_view value) {
    return std::all_of(value.begin(), value.end(), [](unsigned char c) { return std::isspace(c); });
}

std::string trim(std::string_view value) {
    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) ++begin;
    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) --end;
    return std::string(value.substr(begin, end - begin));
}

std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string stripQuotes(std::string_view value) {
    auto cleaned = trim(value);
    while (!cleaned.empty() && cleaned.front() == '"') cleaned.erase(cleaned.begin());
    while (!cleaned.empty() && cleaned.back() == '"') cleaned.pop_back();
    return cleaned;
}

struct CiLess {
    bool operator()(const std::string& left, const std::string& right) const {
        return toLower(left) < toLower(right);
    }
};

std::string readText(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("Could not read '" + path.string() + "'.");
    return std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
}

std::vector<std::filesystem::path> pscFilesUnder(const std::filesystem::path& root) {
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
                       toLower(entry.path().extension().string()) == ".psc") {
                files.push_back(entry.path());
            }
        }
    }
    return files;
}

}  // namespace

std::optional<std::filesystem::path> stageNamespacedScripts(
    const std::vector<std::filesystem::path>& files, const std::filesystem::path& staging) {
    if (files.empty()) return std::nullopt;

    try {
        const auto stagingRoot = std::filesystem::weakly_canonical(staging);
        std::vector<std::pair<std::filesystem::path, std::filesystem::path>> copies;
        for (const auto& file : files) {
            std::string obj;
            try {
                obj = extractScriptName(readText(file));
            } catch (...) {
            }
            if (!obj.empty() && !safeScriptObjectName(obj)) {
                throw CompileStagingError("Refused to compile: '" + file.filename().string() +
                                          "' declares ScriptName '" + obj +
                                          "', which has an unsafe namespace part. Nothing was staged.");
            }
            std::filesystem::path rel;
            if (obj.find(':') != std::string::npos) {
                std::istringstream stream(obj);
                std::string part;
                while (std::getline(stream, part, ':')) rel /= part;
                rel += ".psc";
            } else {
                rel = file.filename();
            }
            const auto dest = staging / rel;
            const auto resolved = std::filesystem::weakly_canonical(dest);
            const auto split = std::mismatch(stagingRoot.begin(), stagingRoot.end(),
                                             resolved.begin(), resolved.end());
            if (split.first != stagingRoot.end()) {
                throw CompileStagingError("Refused to compile: '" + file.filename().string() +
                                          "' would be written outside the staging folder. "
                                          "Nothing was staged.");
            }
            copies.emplace_back(file, dest);
        }
        for (const auto& [file, dest] : copies) {
            std::filesystem::create_directories(dest.parent_path());
            std::filesystem::copy_file(file, dest, std::filesystem::copy_options::overwrite_existing);
        }
        return staging;
    } catch (const CompileStagingError&) {
        throw;
    } catch (...) {
        std::error_code ec;
        std::filesystem::remove_all(staging, ec);
        throw CompileStagingError(
            "Refused to compile: the script folder could not be staged under its namespace folders. "
            "Nothing was compiled.");
    }
}

CompilePlan planCompile(const std::string& source, const std::string& output,
                        const std::string& importsText,
                        const std::vector<std::filesystem::path>& baseImports) {
    CompilePlan plan;
    const bool isDir = std::filesystem::is_directory(source);
    if (isDir) {
        const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
        plan.stagingDir = stageNamespacedScripts(
            pscFilesUnder(source),
            std::filesystem::temp_directory_path() / ("FO4RE_PapyrusStage_" + std::to_string(ticks)));
        plan.workDir = plan.stagingDir ? plan.stagingDir->string() : source;
        plan.target = plan.workDir;
    } else {
        const auto full = std::filesystem::absolute(source);
        const auto fileDir = full.parent_path();
        std::string objName;
        try {
            objName = extractScriptName(readText(full));
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
            plan.workDir = root.string();
            plan.target = objName;
        } else {
            plan.workDir = fileDir.string();
            plan.target = full.filename().string();
        }
    }

    std::vector<std::string> imp;
    if (!isWhitespace(importsText)) {
        std::istringstream stream(importsText);
        std::string part;
        while (std::getline(stream, part, ';')) {
            auto cleaned = stripQuotes(part);
            if (!cleaned.empty()) imp.push_back(std::move(cleaned));
        }
    }
    for (const auto& base : baseImports) imp.push_back(base.string());
    imp.push_back(plan.workDir);

    std::ostringstream importArg;
    std::set<std::string, CiLess> seen;
    bool first = true;
    for (const auto& entry : imp) {
        if (!seen.insert(entry).second) continue;
        if (!first) importArg << ';';
        importArg << entry;
        first = false;
    }
    plan.importArg = importArg.str();

    plan.outDir = !output.empty() ? output : (isDir ? source : plan.workDir);
    std::error_code ec;
    std::filesystem::create_directories(plan.outDir, ec);
    if (ec) {
        removeCompileStaging(plan);
        throw CompileStagingError("Cannot create output dir '" + plan.outDir + "': " + ec.message());
    }
    return plan;
}

void removeCompileStaging(const CompilePlan& plan) {
    if (!plan.stagingDir) return;
    std::error_code ec;
    std::filesystem::remove_all(*plan.stagingDir, ec);
}

}  // namespace fo4::papyrus
