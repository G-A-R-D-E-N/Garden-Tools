#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <fo4ide/app/SwfTarget.hpp>
#include <fo4ide/core/PayloadLimits.hpp>
#include <fo4ide/settings/Settings.hpp>

namespace swft_target_test {


namespace fs = std::filesystem;

inline std::vector<std::byte> rectBytes(int xMin, int xMax, int yMin, int yMax, int nbits) {
    std::vector<std::byte> out;
    std::uint32_t pending = 0;
    int filled = 0;
    const auto push = [&](std::uint32_t value, int count) {
        for (int index = 0; index < count; ++index) {
            pending = (pending << 1) | ((value >> (count - 1 - index)) & 1u);
            if (++filled == 8) {
                out.push_back(static_cast<std::byte>(pending));
                pending = 0;
                filled = 0;
            }
        }
    };
    const std::uint32_t mask = (nbits >= 32) ? 0xFFFFFFFFu : ((1u << nbits) - 1u);
    push(static_cast<std::uint32_t>(nbits), 5);
    push(static_cast<std::uint32_t>(xMin) & mask, nbits);
    push(static_cast<std::uint32_t>(xMax) & mask, nbits);
    push(static_cast<std::uint32_t>(yMin) & mask, nbits);
    push(static_cast<std::uint32_t>(yMax) & mask, nbits);
    if (filled != 0) push(0, 8 - filled);
    return out;
}

inline void appendU16(std::vector<std::byte>& out, int value) {
    out.push_back(static_cast<std::byte>(value & 0xFF));
    out.push_back(static_cast<std::byte>((value >> 8) & 0xFF));
}

inline void appendU32(std::vector<std::byte>& out, std::uint32_t value) {
    for (int part = 0; part < 4; ++part)
        out.push_back(static_cast<std::byte>((value >> (8 * part)) & 0xFFu));
}

inline void appendZ(std::vector<std::byte>& out, const char* text) {
    for (const char* cursor = text; *cursor != '\0'; ++cursor)
        out.push_back(static_cast<std::byte>(*cursor));
    out.push_back(static_cast<std::byte>(0));
}

inline void appendTag(std::vector<std::byte>& out, int code, const std::vector<std::byte>& payload) {
    if (payload.size() >= 0x3F) {
        const auto header = static_cast<std::uint16_t>(((code & 0x3FF) << 6) | 0x3F);
        appendU16(out, header);
        appendU32(out, static_cast<std::uint32_t>(payload.size()));
    } else {
        const auto header = static_cast<std::uint16_t>(((code & 0x3FF) << 6) |
                                                       static_cast<int>(payload.size()));
        appendU16(out, header);
    }
    out.insert(out.end(), payload.begin(), payload.end());
}

// Constraint: the bytes must match SwfFixture.Movie(withAbc:true), so the native target reads the movie the C# tests own.
inline std::vector<std::byte> fixtureMovie() {
    std::vector<std::byte> editText;
    appendU16(editText, 11);
    const auto bounds = rectBytes(0, 400, 0, 200, 12);
    editText.insert(editText.end(), bounds.begin(), bounds.end());
    editText.push_back(static_cast<std::byte>(0));
    editText.push_back(static_cast<std::byte>(0));
    appendZ(editText, "variable");

    const std::vector<std::byte> place{static_cast<std::byte>(0x06), static_cast<std::byte>(0x01),
                                       static_cast<std::byte>(0x00), static_cast<std::byte>(0x0B),
                                       static_cast<std::byte>(0x00), static_cast<std::byte>(0x00)};

    std::vector<std::byte> sprite;
    appendU16(sprite, 14);
    appendU16(sprite, 1);
    std::vector<std::byte> innerLabel;
    appendZ(innerLabel, "inner");
    appendTag(sprite, 43, innerLabel);
    const std::vector<std::byte> spritePlace{
        static_cast<std::byte>(0x06), static_cast<std::byte>(0x02), static_cast<std::byte>(0x00),
        static_cast<std::byte>(0x0B), static_cast<std::byte>(0x00), static_cast<std::byte>(0x00)};
    appendTag(sprite, 26, spritePlace);
    appendTag(sprite, 1, {});
    appendTag(sprite, 0, {});

    std::vector<std::byte> tags;
    appendTag(tags, 37, editText);
    std::vector<std::byte> startLabel;
    appendZ(startLabel, "start");
    appendTag(tags, 43, startLabel);
    appendTag(tags, 26, place);
    appendTag(tags, 1, {});
    appendTag(tags, 39, sprite);

    std::vector<std::byte> abc;
    appendU32(abc, 0);
    appendZ(abc, "edit");
    const std::uint8_t abcBody[] = {0x10, 0x00, 0x2E, 0x00, 0x00, 0x00, 0x00, 0x02, 0x03, 0x46, 0x6F,
                                    0x6F, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                    0x00, 0x01, 0x00, 0x01, 0x01, 0x00, 0x00, 0x02, 0x26, 0x48, 0x00,
                                    0x00};
    for (const auto byte : abcBody) abc.push_back(static_cast<std::byte>(byte));
    appendTag(tags, 82, abc);
    appendTag(tags, 0, {});

    std::vector<std::byte> head;
    head.push_back(static_cast<std::byte>('F'));
    head.push_back(static_cast<std::byte>('W'));
    head.push_back(static_cast<std::byte>('S'));
    head.push_back(static_cast<std::byte>(10));
    appendU32(head, static_cast<std::uint32_t>(8 + 9 + 4 + tags.size()));
    const auto frame = rectBytes(0, 11000, 0, 8000, 15);
    head.insert(head.end(), frame.begin(), frame.end());
    appendU16(head, 0x0C00);
    appendU16(head, 1);
    head.insert(head.end(), tags.begin(), tags.end());
    return head;
}

inline nlohmann::json replyJson(const fo4ide::Result<nlohmann::json>& result) {
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    return nlohmann::json::parse(result->get<std::string>());
}

struct TempSwf {
    fs::path path;
    explicit TempSwf(const std::vector<std::byte>& bytes) {
        // Requires a per-process name: parallel test runners must never share a file.
#ifdef _WIN32
        const auto pid = _getpid();
#else
        const auto pid = getpid();
#endif
        const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
        path = fs::temp_directory_path() / ("fo4ide-swf-target-test-" + std::to_string(pid) +
                                            "-" + std::to_string(tick) + ".swf");
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        stream.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
        stream.close();
        REQUIRE(fs::exists(path));
    }
    ~TempSwf() {
        std::error_code code;
        fs::remove(path, code);
        fs::remove(fs::path(path.string() + ".bak"), code);
    }
};

struct RootedTarget {
    std::shared_ptr<std::vector<fs::path>> roots = std::make_shared<std::vector<fs::path>>();
    fo4ide::app::SwfTarget target{[shared = roots] { return *shared; }};
};

inline fs::path uniqueDir(const std::string& stem) {
#ifdef _WIN32
    const auto pid = _getpid();
#else
    const auto pid = getpid();
#endif
    const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path dir = fs::temp_directory_path() /
                         (stem + "-" + std::to_string(pid) + "-" + std::to_string(tick));
    fs::create_directories(dir);
    return dir;
}

struct RemoveAll {
    fs::path path;
    ~RemoveAll() {
        std::error_code code;
        fs::remove_all(path, code);
    }
};

inline void writeBytes(const fs::path& path, const std::vector<std::byte>& bytes) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

// Invariant: 262144 tags in all, the reader's cap, so one more top-level tag cannot be reopened.
inline std::vector<std::byte> tagCapMovie() {
    const std::vector<std::byte> none;
    std::vector<std::byte> sprite;
    appendU16(sprite, 1);
    appendU16(sprite, 1);
    for (int index = 0; index < 262140; ++index) appendTag(sprite, 1, none);
    appendTag(sprite, 0, none);
    std::vector<std::byte> tags;
    appendTag(tags, 39, sprite);
    appendTag(tags, 1, none);
    appendTag(tags, 0, none);
    std::vector<std::byte> head;
    head.push_back(static_cast<std::byte>('F'));
    head.push_back(static_cast<std::byte>('W'));
    head.push_back(static_cast<std::byte>('S'));
    head.push_back(static_cast<std::byte>(10));
    appendU32(head, static_cast<std::uint32_t>(8 + 9 + 4 + tags.size()));
    const auto frame = rectBytes(0, 11000, 0, 8000, 15);
    head.insert(head.end(), frame.begin(), frame.end());
    appendU16(head, 0x0C00);
    appendU16(head, 1);
    head.insert(head.end(), tags.begin(), tags.end());
    return head;
}

inline void allowPath(RootedTarget& rooted, const fs::path& path) {
    *rooted.roots = {path.parent_path()};
}

inline std::vector<std::byte> readFileBytes(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    REQUIRE(stream);
    const std::string text((std::istreambuf_iterator<char>(stream)),
                           std::istreambuf_iterator<char>());
    std::vector<std::byte> bytes;
    bytes.reserve(text.size());
    for (char c : text) bytes.push_back(static_cast<std::byte>(c));
    return bytes;
}

}  // namespace swft_target_test
