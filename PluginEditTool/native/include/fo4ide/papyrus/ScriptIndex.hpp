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

#include <fo4ide/papyrus/Ast.hpp>

namespace fo4ide::papyrus {

// Port of PapyrusScriptIndex: walks data roots for .psc files, resolves script
// names to files (qualified or bare), parses on demand with an
// (size, mtime)-keyed cache, and computes base chains. Owns every parsed
// Script; resolutions and bindings point into these, so the index must outlive
// them (mirrors the C# lifetime contract).
class ScriptIndex {
public:
    void addRoot(const std::filesystem::path& root);

    std::size_t count() const { return byQualifiedName_.size(); }
    const std::vector<std::filesystem::path>& roots() const { return roots_; }
    std::vector<std::string> scriptNames() const;

    // Resolves a script name to its file (qualified name first, then bare
    // last-segment match); nullopt if not indexed.
    std::optional<std::filesystem::path> findFile(std::string_view scriptName) const;

    // Returns the parsed Script for `scriptName`, or nullptr when the script
    // is not on any root.
    const Script* resolve(std::string_view scriptName);

    // Parses (or serves from cache) the script at `path`; nullptr on I/O
    // failure or a parse failure that prevents a Script from existing.
    const Script* parseCached(const std::filesystem::path& path);

    void invalidate(const std::filesystem::path& path);

    // [script, parent, grandparent, ...] following `Extends`, cycle-guarded.
    // Non-const like the C#: resolving parents may populate the parse cache.
    std::vector<const Script*> baseChain(const Script* script);

    const Declaration* findMember(const Script* script, std::string_view memberName,
                                  const Script*& owner);

    static const Declaration* findMemberOn(const Script* script, std::string_view memberName);

private:
    std::vector<std::filesystem::path> roots_;
    // Keys are lowercased; values keep the canonical spelling.
    std::unordered_map<std::string, std::filesystem::path> byQualifiedName_;
    std::unordered_map<std::string, std::filesystem::path> byBaseName_;

    struct CacheEntry {
        std::uintmax_t size{0};
        std::filesystem::file_time_type mtime{};
        std::unique_ptr<Script> script;
    };
    // Keyed on the absolute path string.
    std::unordered_map<std::string, CacheEntry> parsed_;
};

}  // namespace fo4ide::papyrus
