#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4ide/papyrus/CodeGenerator.hpp>
#include <fo4ide/papyrus/Pex.hpp>

namespace {

using fo4ide::papyrus::PexUserFlag;
using fo4ide::papyrus::UserFlagTable;

}  // namespace

TEST_CASE("UserFlagTable the built in table matches the bits every real pex carries") {
    const auto table = UserFlagTable::fallout4Default();

    const std::vector<PexUserFlag>& flags = table.flags();
    REQUIRE(flags.size() == 6);
    CHECK(flags[0].name == "hidden");
    CHECK(flags[0].index == 0);
    CHECK(flags[1].name == "conditional");
    CHECK(flags[1].index == 1);
    CHECK(flags[2].name == "default");
    CHECK(flags[2].index == 2);
    CHECK(flags[3].name == "collapsedonref");
    CHECK(flags[3].index == 3);
    CHECK(flags[4].name == "collapsedonbase");
    CHECK(flags[4].index == 4);
    CHECK(flags[5].name == "mandatory");
    CHECK(flags[5].index == 5);
}

TEST_CASE("UserFlagTable a composite flag expands to its children and owns no bit") {
    const auto table = UserFlagTable::parse(
        "Flag CollapsedOnRef 3 { Group }\nFlag CollapsedOnBase 4 { Group }\n"
        "Flag Collapsed CollapsedOnRef & CollapsedOnBase\n");

    CHECK(table.maskFor("Collapsed") == ((1u << 3) | (1u << 4)));
    CHECK(table.flags().size() == 2);
}

TEST_CASE("UserFlagTable comments and the allowed kinds list are ignored") {
    const auto table = UserFlagTable::parse("/* header\n   comment */\n// Flag NotReal 9\n"
                                            "Flag Mandatory 5\n{\n    Property\n}\n");

    CHECK(table.maskFor("Mandatory") == 32u);
    CHECK_FALSE(table.knows("NotReal"));
}

TEST_CASE("UserFlagTable an unknown name contributes nothing so keywords pass through harmlessly") {
    const auto table = UserFlagTable::fallout4Default();
    const std::vector<std::string> written = {"auto", "const", "hidden"};
    CHECK(table.maskFor(written) == 1u);
}
