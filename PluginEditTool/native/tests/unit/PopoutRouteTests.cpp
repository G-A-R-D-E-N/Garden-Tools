#include <catch2/catch_test_macros.hpp>

#include <fo4ide/platform/PopoutRoute.hpp>

using fo4ide::platform::popoutRouteFor;

TEST_CASE("popout route is read from a same-origin popout URL", "[popout]") {
    const std::string app = "http://127.0.0.1:49735/#/main?t=0123456789abcdef0123456789abcdef";
    CHECK(popoutRouteFor("http://127.0.0.1:49735/#/popout/cell-viewport?t=0123456789abcdef0123456789abcdef", app) == "cell-viewport");
    CHECK(popoutRouteFor("http://127.0.0.1:49735/#/popout/cell-viewport", app) == "cell-viewport");
}

TEST_CASE("popout route rejects other origins and paths", "[popout]") {
    const std::string app = "http://127.0.0.1:49735/#/main?t=abc";
    CHECK_FALSE(popoutRouteFor("http://127.0.0.1:49736/#/popout/cell-viewport", app));
    CHECK_FALSE(popoutRouteFor("https://example.com/#/popout/cell-viewport", app));
    CHECK_FALSE(popoutRouteFor("http://127.0.0.1:49735/#/main?t=abc", app));
    CHECK_FALSE(popoutRouteFor("http://127.0.0.1:49735/popout/cell-viewport", app));
}

TEST_CASE("popout route rejects empty or malformed route names", "[popout]") {
    const std::string app = "http://127.0.0.1:49735/#/main";
    CHECK_FALSE(popoutRouteFor("http://127.0.0.1:49735/#/popout/", app));
    CHECK_FALSE(popoutRouteFor("http://127.0.0.1:49735/#/popout/../main", app));
    CHECK_FALSE(popoutRouteFor("http://127.0.0.1:49735/#/popout/cell_viewport", app));
    CHECK_FALSE(popoutRouteFor("http://127.0.0.1:49735/#/popout/cell-viewport/extra", app));
}

TEST_CASE("popout route ignores an empty app URL", "[popout]") {
    CHECK_FALSE(popoutRouteFor("http://127.0.0.1:49735/#/popout/cell-viewport", ""));
}