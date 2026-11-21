#pragma once

// Constraint: both previs command files need the same file read, file write and NOPREVIS edit, so they share one definition.

#include <fo4/formid.hpp>
#include <fo4/io/atomic_write.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/plugin/visit.hpp>
#include <fo4/previs/cdx.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace fo4ide::plugin {

// Flag layout: Fallout 4 CELL record flags put "No Pre Vis" on bit 7 and "Compressed" on bit 18, so the previs switch must use bit 7.
inline constexpr std::uint32_t k_no_pre_vis = 1u << 7;

inline std::vector<std::byte> readBinaryFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    in.seekg(0, std::ios::end);
    const auto size = in.tellg();
    if (size <= 0) return {};
    in.seekg(0, std::ios::beg);
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    in.read(reinterpret_cast<char*>(bytes.data()), size);
    if (!in) return {};
    return bytes;
}

inline bool writeBinaryFile(const std::filesystem::path& path, std::span<const std::byte> bytes) {
    if (path.empty() || bytes.empty()) return false;
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(out);
}

// Invariant: commit() must stage every file before it replaces any target, and a failed commit must put back what it changed.
class FileTransaction {
public:
    void write(std::filesystem::path path, std::vector<std::byte> bytes) {
        entries_.push_back(Entry{std::move(path), std::move(bytes), false, false, false, {}});
    }

    void remove(std::filesystem::path path) { entries_.push_back(Entry{std::move(path), {}, true, false, false, {}}); }

    std::optional<std::string> commit() {
        std::error_code ec;
        for (auto& entry : entries_) {
            entry.existed = std::filesystem::is_regular_file(entry.path, ec);
            if (!entry.existed) continue;
            entry.before = readBinaryFile(entry.path);
            if (entry.before.empty() && std::filesystem::file_size(entry.path, ec) > 0) {
                return "could not read " + entry.path.string() + " to keep a copy for rollback";
            }
        }
        for (const auto& entry : entries_) {
            if (entry.removal) continue;
            std::vector<std::filesystem::path> missing;
            for (auto folder = entry.path.parent_path(); !folder.empty() && folder != folder.parent_path();
                 folder = folder.parent_path()) {
                if (std::filesystem::exists(folder, ec)) break;
                missing.push_back(folder);
            }
            std::filesystem::create_directories(entry.path.parent_path(), ec);
            if (ec) {
                removeCreatedFolders();
                return "could not create " + entry.path.parent_path().string() + ": " + ec.message();
            }
            createdFolders_.insert(createdFolders_.end(), missing.rbegin(), missing.rend());
        }
        std::vector<fo4::io::StagedFile> staged;
        staged.reserve(entries_.size());
        try {
            for (const auto& entry : entries_) {
                if (entry.removal) continue;
                auto file = fo4::io::StagedFile::create(entry.path);
                file.write(entry.bytes);
                file.finish();
                staged.push_back(std::move(file));
            }
        } catch (const std::exception& error) {
            staged.clear();
            removeCreatedFolders();
            return std::string(error.what());
        }
        std::size_t next = 0;
        for (auto& entry : entries_) {
            try {
                if (entry.removal) {
                    if (entry.existed && !std::filesystem::remove(entry.path, ec)) {
                        throw std::runtime_error("could not remove " + entry.path.string());
                    }
                } else {
                    std::move(staged[next++]).commit();
                }
                entry.applied = true;
            } catch (const std::exception& error) {
                staged.clear();
                std::string text = error.what();
                if (const auto unrestored = rollback(); !unrestored.empty()) text += "; could not restore: " + unrestored;
                return text;
            }
        }
        return std::nullopt;
    }

private:
    struct Entry {
        std::filesystem::path path;
        std::vector<std::byte> bytes;
        bool removal{false};
        bool existed{false};
        bool applied{false};
        std::vector<std::byte> before;
    };

    // Constraint: the paths that could not be put back must be returned, so the caller reports them.
    std::string rollback() {
        std::string unrestored;
        for (auto it = entries_.rbegin(); it != entries_.rend(); ++it) {
            if (!it->applied) continue;
            std::error_code ec;
            bool restored = true;
            try {
                if (it->existed) {
                    fo4::io::write_file_atomic(it->path, it->before);
                } else if (!it->removal) {
                    std::filesystem::remove(it->path, ec);
                    restored = !ec;
                }
            } catch (const std::exception&) {
                restored = false;
            }
            if (!restored) unrestored += (unrestored.empty() ? "" : ", ") + it->path.string();
            it->applied = false;
        }
        removeCreatedFolders();
        return unrestored;
    }

    void removeCreatedFolders() {
        std::error_code ec;
        for (auto it = createdFolders_.rbegin(); it != createdFolders_.rend(); ++it) std::filesystem::remove(*it, ec);
        createdFolders_.clear();
    }

    std::vector<Entry> entries_;
    std::vector<std::filesystem::path> createdFolders_;
};

inline std::optional<fo4::previs::CdxFile> readCdxFile(const std::filesystem::path& path) {
    const auto bytes = readBinaryFile(path);
    if (bytes.empty()) return std::nullopt;
    try {
        return fo4::previs::parse_cdx(bytes);
    } catch (...) {
        return std::nullopt;
    }
}

inline void setNoPreVisFlag(fo4::Plugin& plugin, const fo4::FormKey& key, bool enabled) {
    fo4::visit_records(plugin, [&](fo4::Record& record) {
        if (record.header.signature != fo4::Signature::from_string("CELL") || record.form_key != key) return;
        fo4::require_decodable(record);
        auto flags = record.header.flags.value();
        flags = enabled ? (flags | k_no_pre_vis) : (flags & ~k_no_pre_vis);
        record.header.flags = fo4::RecordFlags(flags);
        record.original_range.clear();
        plugin.original_range.clear();
    });
}

}  // namespace fo4ide::plugin

