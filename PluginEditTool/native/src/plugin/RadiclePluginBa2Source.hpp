#pragma once

#include <fo4/archive/ba2_reader.hpp>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace fo4ide::plugin::detail {
#ifdef FO4IDE_TEST_HOOKS
using Ba2ReadHook = void (*)(std::uint64_t, std::size_t, std::uint64_t);
void setBa2ReadHook(Ba2ReadHook hook);
#endif
class Ba2ReadRoot {
public:
    static Ba2ReadRoot authorize(const std::filesystem::path& root);
    const std::filesystem::path& path() const noexcept;
    bool sameRoot(const Ba2ReadRoot& other) const;
    std::shared_ptr<const fo4::archive::Ba2ByteSource> open(
        const std::filesystem::path& relative, std::uint64_t maxBytes, bool shareDelete = false) const;
    std::vector<std::string> list(const std::filesystem::path& relativeDirectory,
                                  std::size_t maxEntries) const;
    std::optional<std::vector<std::string>> listOptional(const std::filesystem::path& relativeDirectory,
                                                          std::size_t maxEntries) const;
private:
    struct Impl;
    explicit Ba2ReadRoot(std::shared_ptr<const Impl> impl) : impl_(std::move(impl)) {}
    std::shared_ptr<const Impl> impl_;
};
}
