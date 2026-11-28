#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include <fo4/graph/graph_compiler.hpp>

using namespace fo4::graph;
namespace fs = std::filesystem;

namespace {

std::optional<std::string> readEnv(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr) return std::nullopt;
    return std::string(value);
}

void writeEnv(const char* name, const std::optional<std::string>& value) {
#ifdef _WIN32
    _putenv_s(name, value ? value->c_str() : "");
#else
    if (value) setenv(name, value->c_str(), 1);
    else unsetenv(name);
#endif
}

struct PrivateTemp {
    fs::path base;
    std::vector<std::pair<const char*, std::optional<std::string>>> saved;

    PrivateTemp() {
        GraphCompiler::clearScratch();
        base = fs::temp_directory_path() /
               ("fo4re-graph-scratch-test-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(base);
#ifdef _WIN32
        const std::vector<const char*> names{"TMP", "TEMP"};
#else
        const std::vector<const char*> names{"TMPDIR"};
#endif
        for (const auto* name : names) {
            saved.emplace_back(name, readEnv(name));
            writeEnv(name, base.string());
        }
    }

    ~PrivateTemp() {
        GraphCompiler::clearScratch();
        for (const auto& [name, value] : saved) writeEnv(name, value);
        std::error_code ignored;
        fs::remove_all(base, ignored);
    }

    fs::path parent() const { return fs::temp_directory_path() / "fo4re-graph-scratch"; }
};

void makeOld(const fs::path& directory) {
    fs::create_directories(directory);
    fs::last_write_time(directory, fs::file_time_type::clock::now() - std::chrono::hours(48));
}

}

TEST(GraphScratch, AScratchFolderLeftByACrashedRunIsSweptAndARecentOneIsKept) {
    PrivateTemp temp;
    const auto parent = temp.parent();
    const auto old = parent / "crashed-old";
    const auto recent = parent / "other-recent";
    makeOld(old);
    {
        std::ofstream out(old / "Old.psc");
        out << "x";
    }
    fs::last_write_time(old, fs::file_time_type::clock::now() - std::chrono::hours(48));
    fs::create_directories(recent);

    const auto root = GraphCompiler::scratchRoot();

    EXPECT_EQ(root.parent_path(), parent);
    EXPECT_FALSE(fs::exists(old));
    EXPECT_TRUE(fs::exists(recent));
}

TEST(GraphScratch, ASweepExaminesAtMost64EntriesSoALargeBacklogDrainsGradually) {
    PrivateTemp temp;
    const auto parent = temp.parent();
    for (int i = 0; i < 65; ++i) makeOld(parent / ("backlog-" + std::to_string(i)));

    GraphCompiler::scratchRoot();

    std::size_t remainingBacklog = 0;
    for (const auto& entry : fs::directory_iterator(parent)) {
        if (entry.path().filename().string().rfind("backlog-", 0) == 0) ++remainingBacklog;
    }
    EXPECT_EQ(remainingBacklog, 1u);
}
