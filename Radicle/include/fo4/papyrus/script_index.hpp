#pragma once

#include <chrono>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <fo4/papyrus/ast.hpp>

namespace fo4::papyrus {

// Invariant: the index owns every parsed Script and must outlive the bindings and resolutions that point into them.
class ScriptIndex {
public:
    void addRoot(const std::filesystem::path& root);

    std::size_t count() const { return byQualifiedName_.size(); }
    const std::vector<std::filesystem::path>& roots() const { return roots_; }
    std::vector<std::string> scriptNames() const;

    std::optional<std::filesystem::path> findFile(std::string_view scriptName) const;

    const Script* resolve(std::string_view scriptName);

    const Script* parseCached(const std::filesystem::path& path);

    void invalidate(const std::filesystem::path& path);

    std::vector<const Script*> baseChain(const Script* script);

    const Declaration* findMember(const Script* script, std::string_view memberName,
                                  const Script*& owner);

    static const Declaration* findMemberOn(const Script* script, std::string_view memberName);

private:
    std::vector<std::filesystem::path> roots_;

    std::unordered_map<std::string, std::filesystem::path> byQualifiedName_;
    std::unordered_map<std::string, std::filesystem::path> byBaseName_;

    struct CacheEntry {
        std::uintmax_t size{0};
        std::filesystem::file_time_type mtime{};
        std::unique_ptr<Script> script;
    };

    std::unordered_map<std::string, CacheEntry> parsed_;
};

}
