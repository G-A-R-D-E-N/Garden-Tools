#pragma once

#include <fo4/archive/ba2_reader.hpp>
#include <fo4/environment/strings.hpp>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace fo4ide::plugin::detail {
class Ba2ReadRoot;
struct HeldStringTable {
    std::string filename;
    std::string language;
    fo4::env::StringSource source;
    std::vector<std::byte> bytes;
    fo4::localization::LocalizedStringTable table;
    std::shared_ptr<const fo4::archive::Ba2ByteSource> lease;
};

fo4::localization::LocalizedStringTable parseHeldStringTable(std::span<const std::byte> bytes,
    fo4::localization::StringTableKind kind, std::size_t& remainingBytes, std::size_t& remainingRows);

class HeldStringDirectory {
public:
    static HeldStringDirectory capture(std::shared_ptr<const Ba2ReadRoot> root,
                                       std::filesystem::path relative, std::string pluginFilename,
                                       std::size_t maxWorkingBytes = 512ull * 1024 * 1024);
    bool exists() const noexcept { return exists_; }
    std::size_t accountedRetainedBytes() const noexcept { return retainedBytes_; }
    const std::vector<HeldStringTable>& tables() const noexcept { return tables_; }
    std::filesystem::path directory() const;
    const std::filesystem::path& relativeDirectory() const noexcept { return relative_; }
    const std::filesystem::path& rootPath() const noexcept;
    void verify() const;
private:
    std::shared_ptr<const Ba2ReadRoot> root_;
    std::filesystem::path relative_;
    std::string plugin_;
    bool exists_{false};
    std::size_t retainedBytes_{0};
    std::vector<HeldStringTable> tables_;
};
}
