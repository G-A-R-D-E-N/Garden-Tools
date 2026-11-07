#include <fo4/papyrus/file_walk.hpp>
#include <fo4/papyrus/script_index.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

class ScratchRoot {
public:
  ScratchRoot()
      : path_(std::filesystem::temp_directory_path() /
              ("fo4re-script-index-" +
               std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))) {
    std::filesystem::create_directories(path_);
  }

  ~ScratchRoot() {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }

  const std::filesystem::path& path() const { return path_; }

  std::filesystem::path write(const std::string& relative, const std::string& content) const {
    const auto file = path_ / relative;
    std::filesystem::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary);
    out << content;
    return file;
  }

private:
  std::filesystem::path path_;
};

}

TEST(PapyrusFileWalk, FindsPscFilesAnyCaseAndSortsThemIgnoringCase) {
  ScratchRoot root;
  root.write("Zed.psc", "ScriptName Zed\n");
  root.write("sub/Beta.psc", "ScriptName Beta\n");
  root.write("alpha.PSC", "ScriptName Alpha\n");
  root.write("notes.txt", "not a script\n");

  const auto files = fo4::papyrus::enumeratePapyrusFiles(root.path());
  ASSERT_EQ(files.size(), 3u);
  EXPECT_EQ(files[0].filename().string(), "alpha.PSC");
  EXPECT_EQ(files[1].filename().string(), "Beta.psc");
  EXPECT_EQ(files[2].filename().string(), "Zed.psc");
}

TEST(PapyrusFileWalk, MissingFolderYieldsNothingInsteadOfThrowing) {
  ScratchRoot root;
  EXPECT_TRUE(fo4::papyrus::enumeratePapyrusFiles(root.path() / "absent").empty());
  EXPECT_TRUE(fo4::papyrus::enumeratePapyrusFiles({}).empty());
}

TEST(PapyrusScriptIndex, FolderBelowARootIsReachableByItsColonQualifiedName) {
  ScratchRoot root;
  const auto file = root.write("Folder/Sub/Thing.psc", "ScriptName Folder:Sub:Thing\n");

  fo4::papyrus::ScriptIndex index;
  index.addRoot(root.path());
  ASSERT_TRUE(index.findFile("Folder:Sub:Thing").has_value());
  EXPECT_EQ(index.findFile("folder:sub:thing")->filename().string(), file.filename().string());
  EXPECT_TRUE(index.findFile("Thing").has_value());
  EXPECT_FALSE(index.findFile("Missing").has_value());
  EXPECT_EQ(index.resolve("Missing"), nullptr);
}

TEST(PapyrusScriptIndex, EditedFileIsReparsedWhileAnUnchangedFileIsServedFromTheCache) {
  ScratchRoot root;
  const auto file = root.write("Cache.psc", "ScriptName Cache\nInt Property A Auto\n");

  fo4::papyrus::ScriptIndex index;
  index.addRoot(root.path());
  const auto* first = index.parseCached(file);
  ASSERT_NE(first, nullptr);
  EXPECT_EQ(index.parseCached(file), first);

  root.write("Cache.psc", "ScriptName Cache\nInt Property A Auto\nInt Property Bee Auto\n");
  const auto* reparsed = index.parseCached(file);
  ASSERT_NE(reparsed, nullptr);
  EXPECT_EQ(reparsed->properties.size(), 2u);
}
