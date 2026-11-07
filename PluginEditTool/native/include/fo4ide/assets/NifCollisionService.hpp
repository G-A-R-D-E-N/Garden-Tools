#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>

#include <fo4/nif/havok_names.hpp>
#include <fo4ide/assets/NifService.hpp>
#include <fo4ide/core/Result.hpp>

namespace fo4ide::assets {

struct NifCollisionSet {
    std::filesystem::path nifPath;
    std::string node;
    std::filesystem::path outputPath;
    bool fromDonor{false};
    std::filesystem::path donorPath;
    std::optional<std::size_t> donorBody;
    NifCollisionShape shape{NifCollisionShape::box};
    double massKg{0.0};
};

class NifCollisionService {
public:
    NifCollisionService() = default;

    Result<std::string> info(const std::filesystem::path& nifPath,
                             std::span<const fo4::nif::HavokLayerRow> layers) const;
    Result<std::string> geometry(const std::filesystem::path& nifPath) const;
    Result<std::string> set(const NifCollisionSet& request) const;
    Result<std::string> remove(const std::filesystem::path& nifPath, const std::string& node,
                               const std::filesystem::path& outputPath) const;
};

}
