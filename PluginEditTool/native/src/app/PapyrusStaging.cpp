#include "PapyrusStaging.hpp"

#include <algorithm>
#include <sstream>
#include <system_error>
#include <utility>

#include <fo4/papyrus/script_names.hpp>

#include "ServiceTargetSupport.hpp"

namespace fo4ide::app {

using namespace support;

std::string extractScriptName(const std::string& text) {
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        auto l = trim(line);
        if (l.empty() || l.front() == ';') continue;
        if (toLower(l).rfind("scriptname ", 0) == 0) {
            std::istringstream tokens(l);
            std::string token;
            std::vector<std::string> parts;
            while (tokens >> token) parts.push_back(token);
            if (parts.size() >= 2) return parts[1];
        }
        break;
    }
    return "";
}

Result<std::optional<std::filesystem::path>> stageNamespacedScripts(
    const std::vector<std::filesystem::path>& files, const std::filesystem::path& staging) {
    if (files.empty()) return std::nullopt;

    try {
        const auto stagingRoot = std::filesystem::weakly_canonical(staging);
        std::vector<std::pair<std::filesystem::path, std::filesystem::path>> copies;
        for (const auto& file : files) {
            std::string obj;
            try {
                obj = extractScriptName(readFileText(file));
            } catch (...) {
            }
            if (!obj.empty() && !fo4::papyrus::safeScriptObjectName(obj)) {
                return std::unexpected(Error{"UnsafeScriptName",
                    "Refused to compile: '" + file.filename().string() + "' declares ScriptName '" +
                    obj + "', which has an unsafe namespace part. Nothing was staged."});
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
                return std::unexpected(Error{"UnsafeScriptName",
                    "Refused to compile: '" + file.filename().string() +
                    "' would be written outside the staging folder. Nothing was staged."});
            }
            copies.emplace_back(file, dest);
        }
        for (const auto& [file, dest] : copies) {
            std::filesystem::create_directories(dest.parent_path());
            std::filesystem::copy_file(file, dest, std::filesystem::copy_options::overwrite_existing);
        }
        return std::optional<std::filesystem::path>{staging};
    } catch (...) {
        std::error_code ec;
        std::filesystem::remove_all(staging, ec);
        return std::unexpected(Error{"UnsafeScriptName",
            "Refused to compile: the script folder could not be staged under its namespace folders. "
            "Nothing was compiled."});
    }
}

}
