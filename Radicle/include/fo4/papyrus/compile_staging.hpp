#pragma once

#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace fo4::papyrus {

class CompileStagingError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct CompilePlan {
    std::string workDir;
    std::string target;
    std::string importArg;
    std::string outDir;
    std::optional<std::filesystem::path> stagingDir;
};

std::optional<std::filesystem::path> stageNamespacedScripts(
    const std::vector<std::filesystem::path>& files, const std::filesystem::path& staging);

CompilePlan planCompile(const std::string& source, const std::string& output,
                        const std::string& importsText,
                        const std::vector<std::filesystem::path>& baseImports);

void removeCompileStaging(const CompilePlan& plan);

}  // namespace fo4::papyrus
