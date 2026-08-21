#include <fo4ide/archive/ArchiveService.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <sstream>
#include <string>
#include <utility>

namespace fo4ide::archive {
namespace {

std::string lower(std::string value) {
    std::ranges::transform(value, value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

Result<std::string> textResult(Result<nlohmann::json> result) {
    if (!result) return std::unexpected(result.error());
    if (result->is_string()) return result->get<std::string>();
    return result->dump();
}

std::string joinSources(const std::vector<std::filesystem::path>& sources) {
    std::ostringstream joined;
    for (std::size_t index = 0; index < sources.size(); ++index) {
        if (index != 0) joined << ',';
        joined << sources[index].string();
    }
    return joined.str();
}

}

ArchiveService::ArchiveService(process::Runner& runner,
                               const tools::Locator& tools,
                               IArchiveBackend& backend)
    : runner_(runner), tools_(tools), backend_(backend) {}

Result<nlohmann::json> ArchiveService::list(const std::filesystem::path& archivePath,
                                             std::string filter,
                                             int limit,
                                             std::string filterMode) {
    return backend_.invoke("list", {
        {"archivePath", archivePath.string()},
        {"filter", std::move(filter)},
        {"limit", limit},
        {"filterMode", std::move(filterMode)}
    });
}

Result<std::string> ArchiveService::extractFile(const std::filesystem::path& archivePath,
                                                 std::string innerPath,
                                                 const std::filesystem::path& outputPath) {
    return textResult(backend_.invoke("extractFile", {
        {"archivePath", archivePath.string()},
        {"innerPath", std::move(innerPath)},
        {"outputPath", outputPath.string()}
    }));
}

Result<std::string> ArchiveService::extractSelected(
    const std::filesystem::path& archivePath,
    const std::vector<std::string>& innerPaths,
    const std::filesystem::path& outputDirectory) {
    return textResult(backend_.invoke("extractSelected", {
        {"archivePath", archivePath.string()},
        {"innerPaths", innerPaths},
        {"outputDirectory", outputDirectory.string()}
    }));
}

Result<std::string> ArchiveService::extractAll(const std::filesystem::path& archivePath,
                                                const std::filesystem::path& outputDirectory,
                                                std::string filter,
                                                int limit,
                                                std::string filterMode) {
    return textResult(backend_.invoke("extractAll", {
        {"archivePath", archivePath.string()},
        {"outputDirectory", outputDirectory.string()},
        {"filter", std::move(filter)},
        {"limit", limit},
        {"filterMode", std::move(filterMode)}
    }));
}

Result<nlohmann::json> ArchiveService::compare(const std::filesystem::path& archiveA,
                                                const std::filesystem::path& archiveB) {
    return backend_.invoke("compare", {
        {"archiveA", archiveA.string()},
        {"archiveB", archiveB.string()}
    });
}

Result<std::string> ArchiveService::pack(const std::vector<std::filesystem::path>& sources,
                                          std::filesystem::path outputBa2,
                                          std::string format,
                                          const std::filesystem::path& rootDirectory,
                                          bool compress) const {
    if (sources.empty()) return std::unexpected(Error{"ArchiveSourcesMissing", "Provide at least one source folder to pack"});
    for (const auto& source : sources) {
        if (!std::filesystem::is_directory(source)) {
            return std::unexpected(Error{"ArchiveSourceMissing", "Source folder not found: " + source.string()});
        }
    }
    if (outputBa2.empty()) return std::unexpected(Error{"ArchiveOutputMissing", "Provide an output .ba2 path"});
    if (lower(outputBa2.extension().string()) != ".ba2") outputBa2 += ".ba2";
    if (!std::filesystem::is_directory(rootDirectory)) {
        return std::unexpected(Error{"ArchiveRootMissing", "Root folder not found: " + rootDirectory.string()});
    }

    if (format.empty()) format = "General";
    const auto normalizedFormat = lower(format);
    if (normalizedFormat != "general" && normalizedFormat != "dds") {
        return std::unexpected(Error{"ArchiveFormatInvalid", "format must be 'General' or 'DDS'"});
    }
    format = normalizedFormat == "dds" ? "DDS" : "General";

    const auto archive2 = tools_.resolve(tools::Tool::Archive2);
    if (archive2.empty()) {
        return std::unexpected(Error{
            "Archive2Unavailable",
            "Archive2.exe is not configured. Native in-process BA2 writing will be supplied by the replacement archive backend"
        });
    }

    try {
        if (!outputBa2.parent_path().empty()) std::filesystem::create_directories(outputBa2.parent_path());
    } catch (const std::exception& ex) {
        return std::unexpected(Error{"ArchiveOutputFailed", ex.what()});
    }

    process::Request request;
    request.executable = archive2;
    request.arguments = {
        joinSources(sources),
        "-f=" + format,
        "-c=" + outputBa2.string(),
        "-r=" + rootDirectory.string()
    };
    if (!compress) request.arguments.emplace_back("-compression=None");
    request.timeout = std::chrono::minutes(10);

    auto run = runner_.run(request);
    if (!run) return std::unexpected(run.error());
    if (run->timedOut) return std::unexpected(Error{"Archive2Timeout", "Archive2.exe timed out after 10 minutes"});
    if (run->exitCode != 0 || !std::filesystem::is_regular_file(outputBa2)) {
        return std::unexpected(Error{
            "Archive2Failed",
            "Archive2.exe failed (exit " + std::to_string(run->exitCode) + "): " + run->combined()
        });
    }

    return "RESULT: success (output -> " + outputBa2.string() + ")\n\n" + run->combined();
}

bool ArchiveService::readBackendAvailable() const noexcept {
    return backend_.available();
}

}
