#include <fo4ide/assets/NifService.hpp>

#include <fo4/error.hpp>
#include <fo4/io/atomic_write.hpp>
#include <fo4/nif/nif.hpp>
#include <fo4/nif/nif_fix.hpp>
#include <fo4/nif/nif_import.hpp>
#include <fo4/nif/nif_write.hpp>
#include <fo4ide/assets/NifRender.hpp>

#include <exception>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <vector>

namespace fo4ide::assets {
namespace {

std::vector<char> readFile(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return std::vector<char>((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
}

fo4::nif::NifFile readNif(const std::filesystem::path& path) {
    const auto raw = readFile(path);
    return fo4::nif::read_nif(std::as_bytes(std::span(raw)));
}

Result<void> writeNif(const std::vector<std::byte>& bytes, const std::filesystem::path& target) {
    try {
        fo4::io::write_file_atomic(target, bytes);
    } catch (const std::exception& error) {
        return std::unexpected(Error{"NifEditFailed", error.what()});
    }
    return {};
}

Result<std::string> importInProcess(const std::filesystem::path& objPath, const std::filesystem::path& outputPath,
                                    const NifImportOptions& options) {
    std::ifstream stream(objPath, std::ios::binary);
    if (!stream) return std::unexpected(Error{"FileNotFound", "Could not open OBJ: " + objPath.string()});
    const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    const fo4::nif::ImportOptions nifOptions{options.material, options.diffuseTexture, options.normalTexture,
                                             options.collisionBox, options.fromBlender};
    std::vector<std::byte> bytes;
    try {
        bytes = fo4::nif::import_obj(text, nifOptions);
    } catch (const std::exception& ex) {
        return std::unexpected(Error{"NifImportFailed", ex.what()});
    }
    if (auto committed = writeNif(bytes, outputPath); !committed) {
        return std::unexpected(committed.error());
    }
    const auto verify = renderVerify(outputPath);
    return "RESULT: import OK (in-process)\nSaved: " + outputPath.string() + "\n" +
        (verify ? *verify : verify.error().message);
}

Result<std::string> fixInProcess(const std::filesystem::path& nifPath, const std::filesystem::path& outputPath) {
    std::vector<std::byte> bytes;
    try {
        bytes = fo4::nif::fix_nif(readNif(nifPath));
    } catch (const fo4::parse_error& error) {
        return std::unexpected(Error{"NifFixFailed", error.parse_message()});
    } catch (const std::exception& ex) {
        return std::unexpected(Error{"NifFixFailed", ex.what()});
    }
    if (auto committed = writeNif(bytes, outputPath); !committed) {
        return std::unexpected(committed.error());
    }
    const auto verify = renderVerify(outputPath);
    return "RESULT: fix OK (in-process)\nSaved: " + outputPath.string() + "\n" +
        (verify ? *verify : verify.error().message);
}

}  // namespace

NifService::NifService(process::Runner&, std::filesystem::path) {}

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
    return renderVerify(nifPath);
}

Result<std::string> NifService::tree(const std::filesystem::path& nifPath) const {
    if (auto valid = requireFile(nifPath, "NIF"); !valid) return std::unexpected(valid.error());
    return renderTree(nifPath);
}

Result<std::string> NifService::fix(const std::filesystem::path& nifPath,
                                    const std::filesystem::path& outputPath) const {
    if (auto valid = requireFile(nifPath, "NIF"); !valid) return std::unexpected(valid.error());
    if (outputPath.empty()) return std::unexpected(Error{"InvalidPath", "Output NIF path is empty"});
    return fixInProcess(nifPath, outputPath);
}

Result<std::string> NifService::importObj(const std::filesystem::path& objPath,
                                          const std::filesystem::path& outputPath,
                                          const NifImportOptions& options) const {
    if (auto valid = requireFile(objPath, "OBJ"); !valid) return std::unexpected(valid.error());
    if (outputPath.empty()) return std::unexpected(Error{"InvalidPath", "Output NIF path is empty"});
    return importInProcess(objPath, outputPath, options);
}

Result<std::string> NifService::applyEdits(const std::filesystem::path& nifPath,
                                           const nlohmann::json& edits,
                                           const std::filesystem::path& outputPath) const {
    if (auto valid = requireFile(nifPath, "NIF"); !valid) return std::unexpected(valid.error());
    if (edits.empty()) return std::unexpected(Error{"NoEdits", "No NIF edits to apply"});

    const auto target = outputPath.empty() ? nifPath : outputPath;
    std::vector<std::byte> bytes;
    try {
        bytes = fo4::nif::apply_edits_json(readNif(nifPath), edits.dump());
    } catch (const fo4::parse_error& error) {
        return std::unexpected(Error{"NifEditFailed", error.parse_message()});
    } catch (const std::exception& ex) {
        return std::unexpected(Error{"NifEditFailed", ex.what()});
    }
    if (auto committed = writeNif(bytes, target); !committed) {
        return std::unexpected(committed.error());
    }
    return "set OK (in-process)\nSaved: " + target.string();
}

}
