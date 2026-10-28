#include <fo4ide/assets/NifService.hpp>

#include <fo4ide/assets/NifRender.hpp>

#include <chrono>
#include <fstream>
#include <functional>
#include <thread>
#include <utility>

namespace fo4ide::assets {
namespace {

std::string pathArg(const std::filesystem::path& path) {
    return path.string();
}

std::filesystem::path uniqueEditsPath() {
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto thread = std::hash<std::thread::id>{}(std::this_thread::get_id());
    return std::filesystem::temp_directory_path() /
        ("fo4ide_nif_edits_" + std::to_string(ticks) + "_" + std::to_string(thread) + ".json");
}

class Cleanup {
public:
    explicit Cleanup(std::filesystem::path path) : path_(std::move(path)) {}
    ~Cleanup() {
        std::error_code ec;
        if (!path_.empty()) std::filesystem::remove(path_, ec);
    }
private:
    std::filesystem::path path_;
};

}

NifService::NifService(process::Runner& runner, std::filesystem::path executable)
    : runner_(runner), executable_(std::move(executable)) {}

Result<void> NifService::requireFile(const std::filesystem::path& path, std::string_view label) const {
    if (path.empty()) return std::unexpected(Error{"InvalidPath", std::string(label) + " path is empty"});
    if (!std::filesystem::is_regular_file(path)) {
        return std::unexpected(Error{"FileNotFound", std::string(label) + " not found: " + path.string()});
    }
    return {};
}

Result<std::string> NifService::inspect(const std::filesystem::path& nifPath) const {
    if (auto valid = requireFile(nifPath, "NIF"); !valid) return std::unexpected(valid.error());
    return renderInspect(nifPath);
}

Result<std::string> NifService::geometry(const std::filesystem::path& nifPath) const {
    if (auto valid = requireFile(nifPath, "NIF"); !valid) return std::unexpected(valid.error());
    return renderGeometry(nifPath);
}

Result<std::string> NifService::verify(const std::filesystem::path& nifPath) const {
    if (auto valid = requireFile(nifPath, "NIF"); !valid) return std::unexpected(valid.error());
    return run({"verify", pathArg(nifPath)});
}

Result<std::string> NifService::tree(const std::filesystem::path& nifPath) const {
    if (auto valid = requireFile(nifPath, "NIF"); !valid) return std::unexpected(valid.error());
    return run({"tree", pathArg(nifPath)});
}

Result<std::string> NifService::fix(const std::filesystem::path& nifPath,
                                    const std::filesystem::path& outputPath) const {
    if (auto valid = requireFile(nifPath, "NIF"); !valid) return std::unexpected(valid.error());
    if (outputPath.empty()) return std::unexpected(Error{"InvalidPath", "Output NIF path is empty"});
    return run({"fix", pathArg(nifPath), pathArg(outputPath)});
}

Result<std::string> NifService::importObj(const std::filesystem::path& objPath,
                                          const std::filesystem::path& outputPath,
                                          const NifImportOptions& options) const {
    if (auto valid = requireFile(objPath, "OBJ"); !valid) return std::unexpected(valid.error());
    if (outputPath.empty()) return std::unexpected(Error{"InvalidPath", "Output NIF path is empty"});

    std::vector<std::string> arguments{"import", pathArg(objPath), pathArg(outputPath)};
    if (!options.material.empty()) {
        arguments.emplace_back("--material");
        arguments.push_back(options.material);
    }
    if (!options.diffuseTexture.empty()) {
        arguments.emplace_back("--tex-diffuse");
        arguments.push_back(options.diffuseTexture);
    }
    if (!options.normalTexture.empty()) {
        arguments.emplace_back("--tex-normal");
        arguments.push_back(options.normalTexture);
    }
    if (options.collisionBox) {
        arguments.emplace_back("--collision");
        arguments.emplace_back("box");
    }
    if (options.fromBlender) arguments.emplace_back("--from-blender");
    return run(std::move(arguments));
}

Result<std::string> NifService::applyEdits(const std::filesystem::path& nifPath,
                                           const nlohmann::json& edits,
                                           const std::filesystem::path& outputPath) const {
    if (auto valid = requireFile(nifPath, "NIF"); !valid) return std::unexpected(valid.error());
    if (edits.empty()) return std::unexpected(Error{"NoEdits", "No NIF edits to apply"});

    const auto target = outputPath.empty() ? nifPath : outputPath;
    auto writeTemp = target;
    writeTemp += ".niftmp";
    const auto editsPath = uniqueEditsPath();
    Cleanup editsCleanup(editsPath);
    Cleanup outputCleanup(writeTemp);

    try {
        std::ofstream stream(editsPath, std::ios::binary | std::ios::trunc);
        if (!stream) return std::unexpected(Error{"NifEditFailed", "Could not create temporary edits file"});
        stream << edits.dump();
        stream.close();
        if (!stream) return std::unexpected(Error{"NifEditFailed", "Could not write temporary edits file"});
    } catch (const std::exception& ex) {
        return std::unexpected(Error{"NifEditFailed", ex.what()});
    }

    auto result = run({"set", pathArg(nifPath), pathArg(writeTemp), pathArg(editsPath)});
    if (!result) return result;
    if (result->find("set OK") == std::string::npos) {
        return std::unexpected(Error{"NifEditFailed", *result});
    }
    if (!std::filesystem::is_regular_file(writeTemp)) {
        return std::unexpected(Error{"NifEditFailed", "niftool reported success but did not create the output file"});
    }

    try {
        std::filesystem::copy_file(writeTemp, target, std::filesystem::copy_options::overwrite_existing);
    } catch (const std::exception& ex) {
        return std::unexpected(Error{"NifEditFailed", ex.what()});
    }

    return *result + "\nSaved: " + target.string();
}

Result<std::string> NifService::run(std::vector<std::string> arguments) const {
    if (executable_.empty()) {
        return std::unexpected(Error{"NifToolNotFound",
            "niftool not installed; authoring needs it (set NIFTOOL_PATH or place niftool beside the application)"});
    }
    if (!std::filesystem::is_regular_file(executable_)) {
        return std::unexpected(Error{"NifToolNotFound", "niftool executable not found: " + executable_.string()});
    }

    process::Request request;
    request.executable = executable_;
    request.arguments = std::move(arguments);
    request.workingDirectory = executable_.parent_path();
    request.timeout = std::chrono::seconds(120);

    auto output = runner_.run(request);
    if (!output) return std::unexpected(output.error());
    if (!output->started) return std::unexpected(Error{"NifToolStartFailed", "Failed to start niftool"});
    if (output->timedOut) return std::unexpected(Error{"NifToolTimeout", "niftool timed out after 120 seconds"});

    auto combined = output->combined();
    if (combined.empty()) {
        combined = "niftool exited " + std::to_string(output->exitCode) + " with no output.";
    }
    return combined;
}

const std::filesystem::path& NifService::executable() const noexcept {
    return executable_;
}

}
