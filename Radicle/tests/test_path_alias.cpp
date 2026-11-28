#include "fo4/io/path_alias.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#if defined(_WIN32)
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <windows.h>
#endif

namespace {

namespace fs = std::filesystem;

struct Cleanup {
  fs::path root;
  ~Cleanup() {
    std::error_code ec;
    fs::remove_all(root, ec);
  }
};

fs::path temp_root() {
  const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
  return fs::temp_directory_path() / ("radicle_path_alias_" + std::to_string(id));
}

void touch(const fs::path& path) {
  fs::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary) << "x";
}

TEST(PathAlias, ANameThatResolvesButIsNotListedIsAnAlias) {
  const auto root = temp_root();
  Cleanup cleanup{root};
  touch(root / "LongFileName.nif");
  fo4::io::DirectoryNameCache cache;
  const auto resolves = [](const fs::path& path) {
    return path.filename() == "LONGFI~1.NIF";
  };
  const auto alias = fo4::io::find_aliased_component(root, root / "LONGFI~1.NIF", cache, resolves);
  ASSERT_TRUE(alias.has_value());
  EXPECT_EQ(alias->filename(), "LONGFI~1.NIF");
}

TEST(PathAlias, ListedNamesAndCaseVariantsAreNotAliases) {
  const auto root = temp_root();
  Cleanup cleanup{root};
  touch(root / "Meshes" / "Gun.nif");
  fo4::io::DirectoryNameCache cache;
  EXPECT_FALSE(fo4::io::find_aliased_component(root, root / "meshes" / "GUN.NIF", cache).has_value());
  EXPECT_FALSE(fo4::io::find_aliased_component(root, root / "Meshes" / "New.nif", cache).has_value());
  EXPECT_FALSE(fo4::io::find_aliased_component(root, root / "Absent" / "Deep" / "x.nif", cache).has_value());
}

TEST(PathAlias, AnAliasedDirectoryComponentIsFound) {
  const auto root = temp_root();
  Cleanup cleanup{root};
  touch(root / "LongDirectoryName" / "a.nif");
  fo4::io::DirectoryNameCache cache;
  const auto resolves = [](const fs::path& path) { return path.filename() == "LONGDI~1"; };
  const auto alias =
      fo4::io::find_aliased_component(root, root / "LONGDI~1" / "a.nif", cache, resolves);
  ASSERT_TRUE(alias.has_value());
  EXPECT_EQ(alias->filename(), "LONGDI~1");
}

#if defined(_WIN32)
TEST(PathAlias, DirectoryListingsAreCachedUnderTheExactSpelling) {
  const auto root = temp_root();
  Cleanup cleanup{root};
  touch(root / "Meshes" / "Gun.nif");
  std::error_code ec;
  if (!fs::is_directory(root / "MESHES", ec)) GTEST_SKIP() << "volume is case sensitive";
  fo4::io::DirectoryNameCache cache;
  EXPECT_FALSE(fo4::io::find_aliased_component(root, root / "Meshes" / "Gun.nif", cache).has_value());
  EXPECT_FALSE(fo4::io::find_aliased_component(root, root / "MESHES" / "Gun.nif", cache).has_value());
  EXPECT_EQ(cache.folded_names.count((root / "Meshes").string()), 1u);
  EXPECT_EQ(cache.folded_names.count((root / "MESHES").string()), 1u);
}

TEST(PathAlias, ARealWindowsShortNameIsFound) {
  const auto root = temp_root();
  Cleanup cleanup{root};
  const auto longName = root / "LongFileNameForAlias.nif";
  touch(longName);
  wchar_t shortBuffer[MAX_PATH];
  const auto length = GetShortPathNameW(longName.c_str(), shortBuffer, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) GTEST_SKIP() << "no short name for this volume";
  const fs::path shortPath(shortBuffer);
  if (shortPath.filename() == longName.filename()) GTEST_SKIP() << "8.3 name generation is off for this volume";
  fo4::io::DirectoryNameCache cache;
  const auto alias = fo4::io::find_aliased_component(root, root / shortPath.filename(), cache);
  ASSERT_TRUE(alias.has_value());
  EXPECT_EQ(alias->filename(), shortPath.filename());
}
#endif

}  // namespace
