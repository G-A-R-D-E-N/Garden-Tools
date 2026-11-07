#include <fo4ide/assets/NifGltfService.hpp>

#include <fo4/error.hpp>
#include <fo4/io/atomic_write.hpp>
#include <fo4/nif/gltf_export.hpp>
#include <fo4/nif/nif.hpp>

#include <cstddef>
#include <exception>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <system_error>
#include <vector>

namespace fo4ide::assets {
namespace {

std::vector<char> readFile(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return std::vector<char>((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
}

std::filesystem::path fromUtf8(std::string_view text) {
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

}

std::optional<fo4::nif::GltfTextureMode> parseGltfTextureMode(std::string_view name) {
    if (name.empty() || name == "png") return fo4::nif::GltfTextureMode::png;
    if (name == "copy") return fo4::nif::GltfTextureMode::copy;
    if (name == "uri") return fo4::nif::GltfTextureMode::uri;
    if (name == "png") return fo4::nif::GltfTextureMode::png;
    if (name == "embed") return fo4::nif::GltfTextureMode::embed;
    return std::nullopt;
}

Result<std::string> exportNifGltf(const std::filesystem::path& nifPath,
                                  const std::filesystem::path& outputPath,
                                  bool overwrite,
                                  const NifGltfOptions& options) {
    if (nifPath.empty()) return std::unexpected(Error{"InvalidPath", "NIF path is empty"});
    if (!std::filesystem::is_regular_file(nifPath)) {
        return std::unexpected(Error{"FileNotFound", "NIF not found: " + nifPath.string()});
    }
    if (outputPath.empty()) return std::unexpected(Error{"InvalidPath", "Output glTF path is empty"});
    std::error_code same;
    if (std::filesystem::equivalent(nifPath, outputPath, same)) {
        return std::unexpected(Error{"InvalidPath", "Output glTF path is the NIF itself; choose another file"});
    }
    std::error_code present;
    if (!overwrite && std::filesystem::exists(outputPath, present)) {
        return std::unexpected(Error{"OutputExists", "Refusing to replace an existing file without overwrite: " + outputPath.string()});
    }

    fo4::nif::GltfExport exported;
    try {
        const auto raw = readFile(nifPath);
        fo4::nif::GltfOptions gltf;
        gltf.materials = options.materials;
        gltf.textures = options.textures;
        gltf.tangents = options.tangents;
        gltf.vertex_colours = options.vertexColours;
        gltf.assets = options.assets;
        exported = fo4::nif::export_glb(fo4::nif::read_nif(std::as_bytes(std::span(raw))), gltf);
    } catch (const fo4::parse_error& error) {
        return std::unexpected(Error{"NifExportFailed", error.parse_message()});
    } catch (const std::exception& error) {
        return std::unexpected(Error{"NifExportFailed", error.what()});
    }

    const auto directory = outputPath.parent_path();
    std::vector<std::filesystem::path> targets;
    for (const auto& file : exported.files) targets.push_back(directory / fromUtf8(file.path));
    if (!overwrite) {
        for (const auto& target : targets) {
            std::error_code taken;
            if (std::filesystem::exists(target, taken)) {
                return std::unexpected(Error{"OutputExists", "Refusing to replace an existing file without overwrite: " + target.string()});
            }
        }
    }
    try {
        fo4::io::write_file_atomic(outputPath, exported.glb);
        for (std::size_t i = 0; i < targets.size(); ++i) {
            std::filesystem::create_directories(targets[i].parent_path());
            fo4::io::write_file_atomic(targets[i], exported.files[i].bytes);
        }
    } catch (const std::exception& error) {
        return std::unexpected(Error{"NifExportFailed", error.what()});
    }

    std::string text = "RESULT: gltf export OK\nSaved: " + outputPath.string() +
                       "\nBytes: " + std::to_string(exported.glb.size());
    if (!targets.empty()) text += "\nTextures: " + std::to_string(targets.size());
    for (const auto& warning : exported.warnings) text += "\nWarning: " + warning;
    return text;
}

}
