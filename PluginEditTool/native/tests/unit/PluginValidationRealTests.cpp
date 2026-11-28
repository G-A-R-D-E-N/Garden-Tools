#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>

#include <catch2/catch_test_macros.hpp>

#include <fo4/plugin/reader.hpp>
#include <fo4ide/plugin/PluginValidation.hpp>

// Detects: a structural rule that calls shipped data an error. The rules for record FormIDs, nextObjectId
// and required subrecords were written against synthetic plugins; the official masters are what every mod
// is built on, so none of them may fail validate_plugin or `--check`.
TEST_CASE("PluginValidationReal.OfficialMastersHaveNoValidationErrors", "[validation-real]") {
    const auto* dir = std::getenv("FO4_DATA_DIR");
    if (dir == nullptr || *dir == '\0') SKIP("Set FO4_DATA_DIR to a Fallout 4 Data folder");
    const std::filesystem::path data{dir};
    REQUIRE(std::filesystem::is_regular_file(data / "Fallout4.esm"));

    for (const auto* name : {"Fallout4.esm", "DLCRobot.esm", "DLCworkshop01.esm", "DLCCoast.esm",
                             "DLCworkshop02.esm", "DLCworkshop03.esm", "DLCNukaWorld.esm"}) {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(data / name, ec)) continue;
        const auto plugin = fo4::read_plugin_file(data / name);
        fo4ide::plugin::ValidationOptions options;
        options.plugin = &plugin;
        options.minimum = fo4ide::plugin::Severity::error;
        const auto report = fo4ide::plugin::validatePlugin(options);
        std::size_t shown = 0;
        for (const auto& finding : report.findings) {
            if (shown++ == 10) break;
            FAIL_CHECK(name << ": " << finding.code << " " << finding.formKey << " [" << finding.field
                            << "] " << finding.message);
        }
        INFO(name);
        CHECK(report.errors == 0u);
    }
}
