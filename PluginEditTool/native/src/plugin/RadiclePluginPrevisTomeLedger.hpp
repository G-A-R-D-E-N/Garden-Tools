#pragma once

#include <fo4/io/path_text.hpp>
#include <fo4/io/sha256.hpp>
#include <fo4/previs/uvd.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <vector>

#include "RadiclePluginPrevisIo.hpp"
#include "RadiclePluginSupport.hpp"

namespace fo4ide::plugin {

enum class TomeOrigin { generated, copied };

// Invariant: an entry is the sha256 of the exact bytes this tool wrote, so a tome edited or swapped afterwards no longer matches and is foreign; a bare string entry is a generated tome.
class GeneratedTomeLedger {
public:
    explicit GeneratedTomeLedger(std::filesystem::path ledgerPath) : path_(std::move(ledgerPath)) {
        const auto bytes = readBinaryFile(path_);
        if (bytes.empty()) {
            std::error_code sizeError;
            if (std::filesystem::file_size(path_, sizeError) > 0 && !sizeError) {
                problem_ = "the tome ledger " + path_.string() + " could not be read, so it was not replaced";
            }
            return;
        }
        const auto* first = reinterpret_cast<const char*>(bytes.data());
        const auto parsed = nlohmann::json::parse(first, first + bytes.size(), nullptr, false);
        if (parsed.is_object() && parsed.contains("tomes") && parsed["tomes"].is_object()) {
            tomes_ = parsed["tomes"];
        } else {
            problem_ = "the tome ledger " + path_.string() + " is not a readable ledger, so it was not replaced; move or delete it";
        }
    }

    const std::optional<std::string>& problem() const { return problem_; }

    std::optional<TomeOrigin> originOf(const std::filesystem::path& tome, std::span<const std::byte> bytes) const {
        const auto found = tomes_.find(key(tome));
        if (found == tomes_.end()) return std::nullopt;
        const auto hash = fo4::io::sha256_hex(bytes);
        if (found->is_string()) {
            if (found->get<std::string>() == hash) return TomeOrigin::generated;
            return std::nullopt;
        }
        if (!found->is_object() || !found->contains("hash") || !(*found)["hash"].is_string() ||
            (*found)["hash"].get<std::string>() != hash) {
            return std::nullopt;
        }
        const bool copied = found->contains("origin") && (*found)["origin"].is_string() &&
                            (*found)["origin"].get<std::string>() == "copied";
        return copied ? TomeOrigin::copied : TomeOrigin::generated;
    }

    bool owns(const std::filesystem::path& tome, std::span<const std::byte> bytes) const {
        return originOf(tome, bytes) == TomeOrigin::generated;
    }

    void record(const std::filesystem::path& tome, std::span<const std::byte> bytes, TomeOrigin origin) {
        const auto hash = fo4::io::sha256_hex(bytes);
        const nlohmann::json value = origin == TomeOrigin::copied
                                         ? nlohmann::json{{"hash", hash}, {"origin", "copied"}}
                                         : nlohmann::json(hash);
        auto& entry = tomes_[key(tome)];
        if (entry == value) return;
        entry = value;
        dirty_ = true;
    }

    void forget(const std::filesystem::path& tome) {
        if (tomes_.erase(key(tome)) > 0) dirty_ = true;
    }

    const std::filesystem::path& path() const { return path_; }

    std::vector<std::byte> pendingBytes() const {
        if (!dirty_) return {};
        const auto text =
            nlohmann::json{{"tomes", tomes_}}.dump(2, ' ', false, nlohmann::json::error_handler_t::replace);
        const auto* first = reinterpret_cast<const std::byte*>(text.data());
        return {first, first + text.size()};
    }

private:
    // Constraint: a path's narrow text is ANSI on Windows and not valid JSON, so the key is its UTF-8 form.
    static std::string key(const std::filesystem::path& tome) {
        auto text = fo4::io::utf8(tome);
        std::ranges::replace(text, '\\', '/');
        return support::asciiLower(text);
    }

    std::filesystem::path path_;
    nlohmann::json tomes_ = nlohmann::json::object();
    std::optional<std::string> problem_;
    bool dirty_{false};
};

// Constraint: only a tome this tool generated and nobody changed since may be replaced or removed; a copied cluster tome and any other validated tome may hold data for other cells and stay untouched.
inline std::optional<std::string> guardTomeWrite(const std::filesystem::path& path,
                                                 std::span<const std::byte> newBytes,
                                                 GeneratedTomeLedger& ledger,
                                                 TomeOrigin origin = TomeOrigin::generated) {
    const auto existing = readBinaryFile(path);
    if (!existing.empty() && fo4::previs::validate_uvd(existing).empty()) {
        const auto recorded = ledger.originOf(path, existing);
        if (recorded != TomeOrigin::generated) {
            if (std::ranges::equal(existing, newBytes)) return std::nullopt;
            return std::string(newBytes.empty() ? "refusing to remove" : "refusing to replace") +
                   " validated tome at " + path.string() +
                   (recorded == TomeOrigin::copied
                        ? "; it is a copied cluster tome that may hold data for other cells"
                        : "; this tool did not generate it or it changed since, so it may hold cluster data") +
                   "; move or delete it to let this tool generate its own";
        }
    }
    if (newBytes.empty()) {
        ledger.forget(path);
    } else {
        ledger.record(path, newBytes, origin);
    }
    return std::nullopt;
}

// Constraint: the guard, the ledger read and the commit must share one write lock, or two runs for one patch could each drop the other's entry.
inline std::optional<std::string> stageGeneratedTomes(FileTransaction& files,
                                                      GeneratedTomeLedger& ledger,
                                                      const std::filesystem::path& tomePath,
                                                      const std::optional<std::filesystem::path>& dataTarget,
                                                      const std::vector<std::byte>& bytes,
                                                      TomeOrigin origin) {
    if (ledger.problem()) return ledger.problem();
    if (const auto refused = guardTomeWrite(tomePath, bytes, ledger, origin)) return refused;
    if (dataTarget && !bytes.empty()) {
        if (const auto refused = guardTomeWrite(*dataTarget, bytes, ledger, origin)) return refused;
    }
    // Constraint: the ledger is staged before the tomes, so a failed tome write rolls its entry back with the rest of the run.
    if (auto pending = ledger.pendingBytes(); !pending.empty()) files.write(ledger.path(), std::move(pending));
    // Constraint: a tome left by an earlier run must not outlive a run that lists nothing.
    if (bytes.empty()) {
        files.remove(tomePath);
        return std::nullopt;
    }
    files.write(tomePath, bytes);
    if (dataTarget) files.write(*dataTarget, bytes);
    return std::nullopt;
}

}  // namespace fo4ide::plugin
