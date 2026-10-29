#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <fo4/pex/pex.hpp>

namespace fo4::papyrus {

class UserFlagTable {
public:
    std::uint32_t maskFor(std::string_view flagName) const;
    bool knows(std::string_view flagName) const;
    std::uint32_t maskFor(const std::vector<std::string>& written) const;

    const std::vector<fo4::pex::PexUserFlag>& flags() const { return flags_; }

    static UserFlagTable fallout4Default();
    static UserFlagTable parse(std::string_view text);
    static UserFlagTable fromFileOrDefault(const std::filesystem::path& path);
    static std::optional<std::filesystem::path> findFlagFile(
        const std::vector<std::filesystem::path>& roots);

private:
    void declare(std::string name, std::uint8_t bit);
    void declareComposite(std::string name, std::uint32_t mask);
    void sort();

    std::unordered_map<std::string, std::uint32_t> masks_;
    std::vector<fo4::pex::PexUserFlag> flags_;
};

}  // namespace fo4::papyrus
