#include "f4lod/btr_probe.hpp"

#include <fstream>
#include <random>
#include <system_error>
#include <utility>

namespace f4lod {
namespace {

class TempFileGuard {
public:
    explicit TempFileGuard(std::filesystem::path path) : path_(std::move(path)) {}
    ~TempFileGuard() {
        if (!path_.empty()) {
            std::error_code ec;
            std::filesystem::remove(path_, ec);
        }
    }

    TempFileGuard(const TempFileGuard&) = delete;
    TempFileGuard& operator=(const TempFileGuard&) = delete;

private:
    std::filesystem::path path_;
};

std::filesystem::path make_temp_btr_path(std::string& error) {
    std::error_code ec;
    const auto directory = std::filesystem::temp_directory_path(ec);
    if (ec) {
        error = "Could not resolve the system temporary directory: " + ec.message();
        return {};
    }

    std::random_device random;
    for (int attempt = 0; attempt < 32; ++attempt) {
        const auto high = static_cast<std::uint64_t>(random()) << 32U;
        const auto low = static_cast<std::uint64_t>(random());
        const auto token = high ^ low;
        const auto candidate = directory /
            ("f4lodgen-btr-probe-" + std::to_string(token) + ".btr");
        if (!std::filesystem::exists(candidate, ec) && !ec) {
            return candidate;
        }
        ec.clear();
    }

    error = "Could not allocate a unique temporary BTR path.";
    return {};
}

bool write_temp_file(
    const std::filesystem::path& path,
    const std::vector<unsigned char>& data,
    std::string& error) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        error = "Could not create temporary BTR file.";
        return false;
    }
    if (!data.empty()) {
        output.write(
            reinterpret_cast<const char*>(data.data()),
            static_cast<std::streamsize>(data.size()));
    }
    if (!output) {
        error = "Failed while writing temporary BTR file.";
        return false;
    }
    return true;
}

} // namespace

PackedBtrProbe probe_btr_from_general_ba2(
    const Ba2Inspection& archive,
    const std::string& game_path) {
    PackedBtrProbe result;
    result.archive = archive.path;
    result.game_path = game_path;

    if (!archive.valid || archive.type != Ba2ArchiveType::general) {
        result.error = "Packed BTR probe requires a valid General BA2 inspection.";
        return result;
    }

    const auto extraction = extract_ba2_general_file(archive, game_path);
    if (!extraction.valid) {
        result.error = "Could not extract packed BTR: " + extraction.error;
        return result;
    }

    std::string temp_error;
    const auto temp_path = make_temp_btr_path(temp_error);
    if (!temp_error.empty()) {
        result.error = std::move(temp_error);
        return result;
    }
    TempFileGuard cleanup(temp_path);
    if (!write_temp_file(temp_path, extraction.data, temp_error)) {
        result.error = std::move(temp_error);
        return result;
    }

    result.btr = inspect_fallout4_btr(temp_path);
    if (!result.btr.valid) {
        result.error = "Packed asset is not a valid Fallout 4 BTR: " + result.btr.error;
        return result;
    }

    result.geometry = inspect_fallout4_btr_geometry(
        temp_path, result.btr.terrain_shape);
    if (!result.geometry.valid) {
        result.error = "Packed BTR terrain geometry is invalid: " + result.geometry.error;
        return result;
    }

    for (std::size_t slot = 0; slot < result.materials.size(); ++slot) {
        const auto property_ref = result.geometry.shader_properties[slot];
        if (property_ref < 0 ||
            static_cast<std::size_t>(property_ref) >= result.btr.nif.blocks.size()) {
            continue;
        }
        if (result.btr.nif.blocks[static_cast<std::size_t>(property_ref)].type !=
            "BSLightingShaderProperty") {
            continue;
        }

        result.material_present[slot] = true;
        result.materials[slot] = inspect_btr_material(temp_path, property_ref);
        if (!result.materials[slot].valid) {
            result.error = "Could not decode packed BTR shader property " +
                           std::to_string(slot) + ": " + result.materials[slot].error;
            return result;
        }
    }

    result.valid = true;
    return result;
}

} // namespace f4lod
