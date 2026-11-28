#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>

namespace fo4::io {

struct DirectoryNameCache {
    std::map<std::string, std::set<std::string>> folded_names;
};

std::optional<std::filesystem::path> find_aliased_component(
    const std::filesystem::path& root, const std::filesystem::path& destination, DirectoryNameCache& cache,
    const std::function<bool(const std::filesystem::path&)>& resolves = {});

}  // namespace fo4::io
