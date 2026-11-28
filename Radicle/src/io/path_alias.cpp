#include "fo4/io/path_alias.hpp"
#include "fo4/io/path_text.hpp"
#include "fo4/text/encoding.hpp"

#include <stdexcept>
#include <system_error>

namespace fo4::io {
namespace {

bool resolves_on_disk(const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::exists(std::filesystem::symlink_status(path, error));
}

const std::set<std::string>& listing_of(const std::filesystem::path& directory, DirectoryNameCache& cache) {
    const auto key = utf8(directory);
    if (const auto found = cache.folded_names.find(key); found != cache.folded_names.end()) return found->second;
    std::set<std::string> names;
    std::error_code error;
    for (std::filesystem::directory_iterator it(directory, error), end; !error && it != end; it.increment(error)) {
        names.insert(fo4::text::fold(utf8(it->path().filename())));
    }
    if (error) {
        throw std::runtime_error("could not inspect output directory '" + utf8(directory) + "': " + error.message());
    }
    return cache.folded_names.emplace(key, std::move(names)).first->second;
}

}  // namespace

std::optional<std::filesystem::path> find_aliased_component(
    const std::filesystem::path& root, const std::filesystem::path& destination, DirectoryNameCache& cache,
    const std::function<bool(const std::filesystem::path&)>& resolves) {
    const auto exists = resolves ? resolves : std::function<bool(const std::filesystem::path&)>(resolves_on_disk);
    auto current = root;
    for (const auto& component : destination.lexically_relative(root)) {
        std::error_code error;
        if (!std::filesystem::is_directory(current, error)) return std::nullopt;
        const auto& names = listing_of(current, cache);
        current /= component;
        if (names.contains(fo4::text::fold(utf8(component)))) continue;
        if (exists(current)) return current;
        return std::nullopt;
    }
    return std::nullopt;
}

}  // namespace fo4::io
