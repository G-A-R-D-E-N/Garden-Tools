#include <fo4ide/archive/ArchiveService.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <sstream>
#include <string>
#include <system_error>
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

std::filesystem::path stagingPathFor(const std::filesystem::path& destination) {
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    return destination.parent_path() /
        (destination.stem().string() + ".packing-" + std::to_string(ticks) + ".ba2");
}

Result<void> publishArchive(const std::filesystem::path& staged,
                            const std::filesystem::path& destination) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(staged, ec)) {
        return std::unexpected(Error{"Archive2Failed",
            "Archive2.exe reported success but wrote no archive"});
    }
    std::filesystem::path backup;
    if (std::filesystem::exists(destination, ec)) {
        backup = destination;
        backup += ".previous";
        std::filesystem::remove(backup, ec);
        std::filesystem::rename(destination, backup, ec);
        if (ec) {
            return std::unexpected(Error{"ArchiveOutputFailed",
                "Could not move the previous archive aside: " + ec.message()});
        }
    }
    std::filesystem::rename(staged, destination, ec);
    if (ec) {
        std::error_code restoreError;
        if (!backup.empty()) std::filesystem::rename(backup, destination, restoreError);
        return std::unexpected(Error{"ArchiveOutputFailed",
            "Could not publish the packed archive: " + ec.message()});
    }
    if (!backup.empty()) std::filesystem::remove(backup, ec);
    return {};
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
                                                 const std::filesystem::path& outputPath,
                                                 bool overwrite) {
    return textResult(backend_.invoke("extractFile", {
        {"archivePath", archivePath.string()},
        {"innerPath", std::move(innerPath)},
        {"outputPath", outputPath.string()},
        {"overwrite", overwrite}
    }));
}

Result<std::string> ArchiveService::extractSelected(
    const std::filesystem::path& archivePath,
    const std::vector<std::string>& innerPaths,
    const std::filesystem::path& outputDirectory,
    bool overwrite,
    bool protectNames) {
    return textResult(backend_.invoke("extractSelected", {
        {"archivePath", archivePath.string()},
        {"innerPaths", innerPaths},
        {"outputDirectory", outputDirectory.string()},
        {"overwrite", overwrite},
        {"protectNames", protectNames}
    }));
}

Result<std::string> ArchiveService::extractAll(const std::filesystem::path& archivePath,
                                                const std::filesystem::path& outputDirectory,
                                                std::string filter,
                                                int limit,
                                                std::string filterMode,
                                                bool overwrite,
                                                bool protectNames) {
    return textResult(backend_.invoke("extractAll", {
        {"archivePath", archivePath.string()},
        {"outputDirectory", outputDirectory.string()},
        {"filter", std::move(filter)},
        {"limit", limit},
        {"filterMode", std::move(filterMode)},
        {"overwrite", overwrite},
        {"protectNames", protectNames}
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
        if (source.string().find(',') != std::string::npos) {
            return std::unexpected(Error{"ArchiveSourceAmbiguous",
                "Source folder name contains a comma, which Archive2 reads as a separator: " + source.string()});
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

    const auto staging = stagingPathFor(outputBa2);
    process::Request request;
    request.executable = archive2;
    request.arguments = {
        joinSources(sources),
        "-f=" + format,
        "-c=" + staging.string(),
        "-r=" + rootDirectory.string()
    };
    if (!compress) request.arguments.emplace_back("-compression=None");
    request.timeout = std::chrono::minutes(10);

    auto run = runner_.run(request);
    const auto fail = [&](Error error) -> Result<std::string> {
        std::error_code cleanupError;
        std::filesystem::remove(staging, cleanupError);
        return std::unexpected(std::move(error));
    };
    if (!run) return fail(run.error());
    if (run->timedOut) return fail(Error{"Archive2Timeout", "Archive2.exe timed out after 10 minutes"});
    if (run->exitCode != 0) {
        return fail(Error{
            "Archive2Failed",
            "Archive2.exe failed (exit " + std::to_string(run->exitCode) + "): " + run->combined()
        });
    }
    if (auto published = publishArchive(staging, outputBa2); !published) return fail(published.error());

    return "RESULT: success (output -> " + outputBa2.string() + ")\n\n" + run->combined();
}

bool ArchiveService::readBackendAvailable() const noexcept {
    return backend_.available();
}

}
