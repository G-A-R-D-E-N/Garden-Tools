#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "../../src/app/SwfTargetSupport.hpp"

namespace {

std::uint8_t* radicleBytes(const std::string& text) {
    auto* bytes = static_cast<std::uint8_t*>(std::malloc(text.empty() ? 1 : text.size()));
    REQUIRE(bytes != nullptr);
    if (!text.empty()) std::memcpy(bytes, text.data(), text.size());
    return bytes;
}

}  // namespace

TEST_CASE("swftarget takeText answers an empty string for a NULL payload") {
    CHECK(fo4ide::app::swftarget::takeText(nullptr, 0).empty());
}

TEST_CASE("swftarget takeText ignores the size when the payload is NULL") {
    CHECK(fo4ide::app::swftarget::takeText(nullptr, 5).empty());
}

TEST_CASE("swftarget takeText copies exactly size bytes and keeps embedded NULs") {
    const std::string text("ab\0cd", 5);
    CHECK(fo4ide::app::swftarget::takeText(radicleBytes(text), text.size()) == text);
}

TEST_CASE("swftarget takeText takes only the first size bytes of a larger buffer") {
    CHECK(fo4ide::app::swftarget::takeText(radicleBytes("abcdef"), 3) == "abc");
}

TEST_CASE("swftarget takeText answers an empty string for a non-NULL payload of size zero") {
    CHECK(fo4ide::app::swftarget::takeText(radicleBytes(""), 0).empty());
}
