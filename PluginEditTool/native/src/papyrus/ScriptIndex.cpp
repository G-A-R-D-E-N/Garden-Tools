#include <fo4ide/papyrus/ScriptIndex.hpp>

#include <algorithm>
#include <cctype>
#include <set>
#include <stack>
#include <stdexcept>
#include <system_error>

#include <fo4ide/papyrus/Parser.hpp>

namespace fo4ide::papyrus {

namespace {

std::string lower(std::string_view text) {
    std::string out(text);
    for (auto& ch : out) {
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    }
    return out;
}

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto x = static_cast<unsigned char>(a[i]);
        const auto y = static_cast<unsigned char>(b[i]);
        if (std::tolower(x) != std::tolower(y)) return false;
    }
    return true;
}

bool hasSuffix(std::string_view text, std::string_view suffix) {
    return text.size() >= suffix.size() &&
           text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Recomputes the qualified name from the file's on-disk path so the returned
// names carry the original casing, matching the C# (which keys by
// QualifiedNameFor and exposes the keys as ScriptNames). Falls back to the
// lowercased key when no root contains the path (should not happen).
std::string qualifiedNameOf(const std::filesystem::path& path,
                            const std::string& fallbackKey,
                            const std::vector<std::filesystem::path>& roots, bool lexical) {
    std::error_code error;
    for (const auto& root : roots) {
        auto relative = lexical ? path.lexically_relative(root) : std::filesystem::relative(path, root, error);
        if (error) {
            error.clear();
            continue;
        }
        const auto text = relative.generic_string();
        if (text.starts_with("..")) continue;
        if (!hasSuffix(lower(text), ".psc")) continue;
        auto qualified = text.substr(0, text.size() - 4);
        std::replace(qualified.begin(), qualified.end(), '/', ':');
        return qualified;
    }
    return fallbackKey;
}

}  // namespace

void ScriptIndex::addSnapshot(const std::filesystem::path& root, const std::vector<SnapshotSource>& sources) {
    if (!roots_.empty() && !snapshotOnly_) throw std::runtime_error("cannot mix snapshot and filesystem roots");
    std::error_code error;
    const auto full = std::filesystem::absolute(root, error).lexically_normal();
    if (error || full.empty()) throw std::runtime_error("invalid snapshot root");
    std::unordered_map<std::string, std::unique_ptr<Script>> captured;
    std::optional<std::string> capturedFlags;
    std::unordered_map<std::string, std::filesystem::path> qualified, bareNames;
    for (const auto& source : sources) {
        const auto& rel = source.relativePath;
        if (rel.empty() || rel.is_absolute() || rel.has_root_name()) throw std::runtime_error("invalid snapshot path");
        for (const auto& part : rel) if (part == "." || part == "..") throw std::runtime_error("invalid snapshot path");
        auto text = rel.generic_string();
#ifdef _WIN32
        const bool isFlags = lower(text) == "institute_papyrus_flags.flg";
#else
        const bool isFlags = text == "Institute_Papyrus_Flags.flg";
#endif
        if (isFlags) {
            if (capturedFlags) throw std::runtime_error("duplicate snapshot flag file");
            capturedFlags = source.text;
            continue;
        }
        if (!hasSuffix(lower(text), ".psc")) throw std::runtime_error("invalid snapshot extension");
        text.resize(text.size() - 4);
        std::replace(text.begin(), text.end(), '/', ':');
        const auto path = (full / rel).lexically_normal();
        const auto key = lower(text);
        const auto bare = text.substr(text.find_last_of(':') + 1);
        if (qualified.contains(key)) throw std::runtime_error("duplicate snapshot script");
        qualified.emplace(key, path);
        bareNames.try_emplace(lower(bare), path);
        captured.emplace(path.string(), std::make_unique<Script>(Parser::parse(source.text, path.string())));
    }
    for (auto& [key, path] : qualified) byQualifiedName_.try_emplace(key, std::move(path));
    for (auto& [key, path] : bareNames) byBaseName_.try_emplace(key, std::move(path));
    for (auto& [key, script] : captured) snapshots_.try_emplace(key, std::move(script));
    roots_.push_back(full);
    if (!snapshotFlags_ && capturedFlags) snapshotFlags_ = std::move(capturedFlags);
    snapshotOnly_ = true;
}

void ScriptIndex::addRoot(const std::filesystem::path& root, std::size_t maxEntries) {
    if (snapshotOnly_) throw std::runtime_error("cannot mix snapshot and filesystem roots");
    std::error_code error;
    const auto full = std::filesystem::absolute(root, error);
    if (error || !std::filesystem::is_directory(full, error) || error) return;

    for (const auto& existing : roots_) {
        if (iequals(existing.string(), full.string())) return;
    }
    std::unordered_map<std::string, std::filesystem::path> qualified;
    std::unordered_map<std::string, std::filesystem::path> bareNames;
    std::size_t entries = 0;

    // Manual DFS mirroring PapyrusFileWalk: case-insensitive "*.psc" match,
    // symlinked directories are skipped (the C# checks LinkTarget is null).
    std::stack<std::filesystem::path> pending;
    pending.push(full);
    while (!pending.empty()) {
        const auto dir = pending.top();
        pending.pop();

        std::vector<std::filesystem::path> subdirs;
        std::error_code walkError;
        for (auto it = std::filesystem::directory_iterator(dir, walkError);
             !walkError && it != std::filesystem::directory_iterator(); it.increment(walkError)) {
            if (maxEntries && ++entries > maxEntries) throw std::runtime_error("Papyrus import tree exceeds its entry limit");
            const auto& entry = *it;
            std::error_code entryError;
            const auto status = entry.symlink_status(entryError);
            if (entryError) {
                if (maxEntries) throw std::runtime_error("cannot inspect Papyrus import entry");
                continue;
            }

            if (std::filesystem::is_regular_file(status)) {
                const auto filename = entry.path().filename().string();
                if (hasSuffix(lower(filename), ".psc")) {
                    std::error_code relError;
                    auto relative = std::filesystem::relative(entry.path(), full, relError);
                    if (relError) continue;
                    auto text = relative.generic_string();
                    // Path.GetRelativePath never yields ".." when the file is
                    // under the root; a defensive check mirrors the C# guard.
                    if (text.starts_with("..")) continue;
                    if (!hasSuffix(lower(text), ".psc")) continue;
                    text.resize(text.size() - 4);
                    std::replace(text.begin(), text.end(), '/', ':');
                    if (!qualified.contains(lower(text))) {
                        qualified[lower(text)] = entry.path();
                    }
                    const auto bare = text.substr(text.find_last_of(':') + 1);
                    if (!bareNames.contains(lower(bare))) bareNames[lower(bare)] = entry.path();
                }
            } else if (std::filesystem::is_directory(status)) {
                // Skip symlinked directories (reparse points), like the C#.
                if (std::filesystem::is_symlink(status)) continue;
                subdirs.push_back(entry.path());
            }
        }
        if (walkError && maxEntries) throw std::runtime_error("cannot scan Papyrus import directory");
        for (auto it = subdirs.rbegin(); it != subdirs.rend(); ++it) pending.push(*it);
    }
    for (auto& [key, path] : qualified) byQualifiedName_.try_emplace(key, std::move(path));
    for (auto& [key, path] : bareNames) byBaseName_.try_emplace(key, std::move(path));
    roots_.push_back(full);
}

std::vector<std::string> ScriptIndex::scriptNames() const {
    std::vector<std::string> names;
    names.reserve(byQualifiedName_.size());
    for (const auto& [key, path] : byQualifiedName_) {
        names.push_back(qualifiedNameOf(path, key, roots_, snapshotOnly_));
    }
    // Mirrors the C# ScriptNames: OrdinalIgnoreCase sort of the keys.
    std::sort(names.begin(), names.end(),
              [](const std::string& a, const std::string& b) { return lower(a) < lower(b); });
    return names;
}

std::optional<std::filesystem::path> ScriptIndex::findFile(std::string_view scriptName) const {
    if (scriptName.empty()) return std::nullopt;
    const auto it = byQualifiedName_.find(lower(scriptName));
    if (it != byQualifiedName_.end()) return it->second;
    const auto text = std::string(scriptName);
    const auto bare = text.substr(text.find_last_of(':') + 1);
    const auto fallback = byBaseName_.find(lower(bare));
    if (fallback != byBaseName_.end()) return fallback->second;
    return std::nullopt;
}

const Script* ScriptIndex::resolve(std::string_view scriptName) {
    const auto file = findFile(scriptName);
    return file ? parseCached(*file) : nullptr;
}

void ScriptIndex::setReadLimits(std::size_t maxFileBytes, std::size_t maxTotalBytes) {
    if (!parsed_.empty()) throw std::logic_error("Papyrus read limits must be set before parsing");
    maxFileBytes_ = maxFileBytes;
    maxTotalBytes_ = maxTotalBytes;
}

const Script* ScriptIndex::parseCached(const std::filesystem::path& path) {
    const auto snapshot = snapshots_.find(path.string());
    if (snapshot != snapshots_.end()) return snapshot->second.get();
    if (snapshotOnly_) return nullptr;
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error) return nullptr;
    const auto mtime = std::filesystem::last_write_time(path, error);
    if (error) return nullptr;

    const auto key = std::filesystem::absolute(path, error).string();
    if (error) return nullptr;

    const auto cached = parsed_.find(key);
    if (cached != parsed_.end() && cached->second.size == size && cached->second.mtime == mtime) {
        return cached->second.script.get();
    }

    std::size_t limit = maxFileBytes_;
    if (maxTotalBytes_) {
        std::size_t used = 0;
        for (const auto& [cachedKey, entry] : parsed_) {
            if (cachedKey == key) continue;
            if (entry.size > maxTotalBytes_ - used) return nullptr;
            used += static_cast<std::size_t>(entry.size);
        }
        const auto remaining = maxTotalBytes_ - used;
        if (remaining == 0) return nullptr;
        limit = limit ? std::min(limit, remaining) : remaining;
    }
    if (limit && size > limit) return nullptr;
    std::size_t bytesRead = 0;
    auto parsed = Parser::parseFile(path, limit, &bytesRead);
    if (!parsed) return nullptr;

    auto& entry = parsed_[key];
    entry.size = bytesRead;
    entry.mtime = mtime;
    entry.script = std::make_unique<Script>(std::move(*parsed));
    return entry.script.get();
}

void ScriptIndex::invalidate(const std::filesystem::path& path) {
    std::error_code error;
    const auto key = std::filesystem::absolute(path, error).string();
    if (!error) parsed_.erase(key);
}

std::vector<const Script*> ScriptIndex::baseChain(const Script* script) {
    std::vector<const Script*> chain;
    if (!script) return chain;
    chain.push_back(script);
    std::set<std::string, std::less<>> seen;
    seen.insert(lower(script->name));

    const Script* current = script;
    while (current->extends) {
        const auto extendsKey = lower(*current->extends);
        if (!seen.insert(extendsKey).second) break;
        const Script* parent = resolve(*current->extends);
        if (!parent) break;
        chain.push_back(parent);
        current = parent;
    }
    return chain;
}

const Declaration* ScriptIndex::findMember(const Script* script, std::string_view memberName,
                                           const Script*& owner) {
    owner = nullptr;
    if (!script || memberName.empty()) return nullptr;
    for (const auto* level : baseChain(script)) {
        if (const auto* hit = findMemberOn(level, memberName)) {
            owner = level;
            return hit;
        }
    }
    return nullptr;
}

const Declaration* ScriptIndex::findMemberOn(const Script* script, std::string_view memberName) {
    if (!script || memberName.empty()) return nullptr;
    const auto match = [&](const auto& decl) {
        return iequals(decl.name, memberName);
    };

    if (const auto it = std::find_if(script->properties.begin(), script->properties.end(), match);
        it != script->properties.end()) {
        return &*it;
    }
    if (const auto it = std::find_if(script->variables.begin(), script->variables.end(), match);
        it != script->variables.end()) {
        return &*it;
    }
    if (const auto it = std::find_if(script->functions.begin(), script->functions.end(), match);
        it != script->functions.end()) {
        return &*it;
    }
    if (const auto it = std::find_if(script->events.begin(), script->events.end(), match);
        it != script->events.end()) {
        return &*it;
    }
    if (const auto it = std::find_if(script->structs.begin(), script->structs.end(), match);
        it != script->structs.end()) {
        return &*it;
    }
    if (const auto it = std::find_if(script->customEvents.begin(), script->customEvents.end(), match);
        it != script->customEvents.end()) {
        return &*it;
    }

    for (const auto& state : script->states) {
        if (const auto it = std::find_if(state.functions.begin(), state.functions.end(), match);
            it != state.functions.end()) {
            return &*it;
        }
        if (const auto it = std::find_if(state.events.begin(), state.events.end(), match);
            it != state.events.end()) {
            return &*it;
        }
    }
    return nullptr;
}

const Declaration* ScriptIndex::findCallableOn(const Script* script, std::string_view memberName) {
    if (!script || memberName.empty()) return nullptr;
    const auto match = [&](const auto& decl) {
        return iequals(decl.name, memberName);
    };

    if (const auto it = std::find_if(script->functions.begin(), script->functions.end(), match);
        it != script->functions.end()) {
        return &*it;
    }
    if (const auto it = std::find_if(script->events.begin(), script->events.end(), match);
        it != script->events.end()) {
        return &*it;
    }
    for (const auto& state : script->states) {
        if (const auto it = std::find_if(state.functions.begin(), state.functions.end(), match);
            it != state.functions.end()) {
            return &*it;
        }
        if (const auto it = std::find_if(state.events.begin(), state.events.end(), match);
            it != state.events.end()) {
            return &*it;
        }
    }
    return nullptr;
}

}  // namespace fo4ide::papyrus
