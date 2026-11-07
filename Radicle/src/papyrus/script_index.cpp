#include <fo4/papyrus/script_index.hpp>

#include <algorithm>
#include <cctype>
#include <set>
#include <stack>
#include <system_error>

#include <fo4/papyrus/parser.hpp>

namespace fo4::papyrus {

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

std::string qualifiedNameOf(const std::filesystem::path& path,
                            const std::string& fallbackKey,
                            const std::vector<std::filesystem::path>& roots) {
    std::error_code error;
    for (const auto& root : roots) {
        auto relative = std::filesystem::relative(path, root, error);
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

}

void ScriptIndex::addRoot(const std::filesystem::path& root) {
    std::error_code error;
    const auto full = std::filesystem::absolute(root, error);
    if (error || !std::filesystem::is_directory(full, error) || error) return;

    for (const auto& existing : roots_) {
        if (iequals(existing.string(), full.string())) return;
    }
    roots_.push_back(full);

    std::stack<std::filesystem::path> pending;
    pending.push(full);
    while (!pending.empty()) {
        const auto dir = pending.top();
        pending.pop();

        std::vector<std::filesystem::path> subdirs;
        std::error_code walkError;
        for (auto it = std::filesystem::directory_iterator(dir, walkError);
             !walkError && it != std::filesystem::directory_iterator(); it.increment(walkError)) {
            const auto& entry = *it;
            std::error_code entryError;
            const auto status = entry.symlink_status(entryError);
            if (entryError) continue;

            if (std::filesystem::is_regular_file(status)) {
                const auto filename = entry.path().filename().string();
                if (hasSuffix(lower(filename), ".psc")) {
                    std::error_code relError;
                    auto relative = std::filesystem::relative(entry.path(), full, relError);
                    if (relError) continue;
                    auto text = relative.generic_string();

                    if (text.starts_with("..")) continue;
                    if (!hasSuffix(lower(text), ".psc")) continue;
                    text.resize(text.size() - 4);
                    std::replace(text.begin(), text.end(), '/', ':');
                    if (!byQualifiedName_.contains(lower(text))) {
                        byQualifiedName_[lower(text)] = entry.path();
                    }
                    const auto bare = text.substr(text.find_last_of(':') + 1);
                    if (!byBaseName_.contains(lower(bare))) byBaseName_[lower(bare)] = entry.path();
                }
            } else if (std::filesystem::is_directory(status)) {

                if (std::filesystem::is_symlink(status)) continue;
                subdirs.push_back(entry.path());
            }
        }
        for (auto it = subdirs.rbegin(); it != subdirs.rend(); ++it) pending.push(*it);
    }
}

std::vector<std::string> ScriptIndex::scriptNames() const {
    std::vector<std::string> names;
    names.reserve(byQualifiedName_.size());
    for (const auto& [key, path] : byQualifiedName_) {
        names.push_back(qualifiedNameOf(path, key, roots_));
    }

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

const Script* ScriptIndex::parseCached(const std::filesystem::path& path) {
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

    auto parsed = Parser::parseFile(path);
    if (!parsed) return nullptr;

    auto& entry = parsed_[key];
    entry.size = size;
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

}
