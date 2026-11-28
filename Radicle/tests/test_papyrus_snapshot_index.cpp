#include "papyrus_scratch_root.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include <fo4/papyrus/compile_script.hpp>
#include <fo4/papyrus/parser.hpp>
#include <fo4/papyrus/script_index.hpp>

namespace {

using fo4::papyrus::PapyrusCompileOptions;
using fo4::papyrus::Parser;
using fo4::papyrus::ScriptIndex;
using fo4::papyrus::SnapshotSource;
using fo4::papyrus::compileScript;
using fo4::papyrus::test::ScratchRoot;

SnapshotSource source(const char* relative, const char* text) {
  return SnapshotSource{std::filesystem::path(relative), text};
}

}

TEST(PapyrusSnapshotIndex, SnapshotScriptsResolveByQualifiedAndBareName) {
  ScratchRoot scratch("snapshot-index");
  ScriptIndex index;
  index.addSnapshot(scratch.path(), {source("Quest.psc", "Scriptname Quest\n"),
                                     source("Fragments/Quests/QF_One.psc",
                                            "Scriptname Fragments:Quests:QF_One Extends Quest\n")});
  EXPECT_TRUE(index.snapshotOnly());
  EXPECT_EQ(index.count(), 2u);
  ASSERT_NE(index.resolve("fragments:quests:qf_one"), nullptr);
  EXPECT_EQ(index.resolve("QF_One"), index.resolve("Fragments:Quests:QF_One"));
  const auto names = index.scriptNames();
  ASSERT_EQ(names.size(), 2u);
  EXPECT_EQ(names[0], "Fragments:Quests:QF_One");
  EXPECT_EQ(names[1], "Quest");
  const auto chain = index.baseChain(index.resolve("QF_One"));
  ASSERT_EQ(chain.size(), 2u);
  EXPECT_EQ(chain[1], index.resolve("Quest"));
}

TEST(PapyrusSnapshotIndex, SnapshotNeverMixesWithFilesystemRoots) {
  ScratchRoot disk("snapshot-disk");
  disk.write("Disk.psc", "Scriptname Disk\n");
  ScratchRoot scratch("snapshot-mix");

  ScriptIndex snapshotFirst;
  snapshotFirst.addSnapshot(scratch.path(), {source("A.psc", "Scriptname A\n")});
  EXPECT_THROW(snapshotFirst.addRoot(disk.path()), std::runtime_error);

  ScriptIndex diskFirst;
  diskFirst.addRoot(disk.path());
  EXPECT_THROW(diskFirst.addSnapshot(scratch.path(), {source("A.psc", "Scriptname A\n")}),
               std::runtime_error);
  EXPECT_FALSE(diskFirst.snapshotOnly());
}

TEST(PapyrusSnapshotIndex, SeveralSnapshotRootsAccumulate) {
  ScratchRoot first("snapshot-first");
  ScratchRoot second("snapshot-second");
  ScriptIndex index;
  index.addSnapshot(first.path(), {source("A.psc", "Scriptname A\n")});
  index.addSnapshot(second.path(), {source("B.psc", "Scriptname B\n")});
  EXPECT_EQ(index.roots().size(), 2u);
  EXPECT_NE(index.resolve("A"), nullptr);
  EXPECT_NE(index.resolve("B"), nullptr);
}

TEST(PapyrusSnapshotIndex, EarlierRootWinsADuplicateNameAcrossSnapshots) {
  ScratchRoot first("snapshot-first");
  ScratchRoot second("snapshot-second");
  ScriptIndex index;
  index.addSnapshot(first.path(), {source("A.psc", "Scriptname A Extends Quest\n")});
  index.addSnapshot(second.path(), {source("A.psc", "Scriptname A Extends Actor\n")});
  const auto* script = index.resolve("A");
  ASSERT_NE(script, nullptr);
  ASSERT_TRUE(script->extends.has_value());
  EXPECT_EQ(*script->extends, "Quest");
}

TEST(PapyrusSnapshotIndex, MalformedSnapshotsAreRefusedWithoutRetainingAnything) {
  ScratchRoot scratch("snapshot-bad");
  const std::vector<std::vector<SnapshotSource>> bad{
      {source("A.psc", "Scriptname A\n"), source("a.psc", "Scriptname a\n")},
      {source("../Escape.psc", "Scriptname Escape\n")},
      {source("/Rooted.psc", "Scriptname Rooted\n")},
      {source("Sub/../Escape.psc", "Scriptname Escape\n")},
      {source("Notes.txt", "text\n")},
      {SnapshotSource{std::filesystem::path{}, "Scriptname Empty\n"}},
      {source("Institute_Papyrus_Flags.flg", "Flag Hidden 5\n"),
       source("Institute_Papyrus_Flags.flg", "Flag Hidden 6\n")},
  };
  for (const auto& sources : bad) {
    ScriptIndex index;
    EXPECT_THROW(index.addSnapshot(scratch.path(), sources), std::runtime_error);
    EXPECT_EQ(index.count(), 0u);
    EXPECT_TRUE(index.roots().empty());
    EXPECT_FALSE(index.snapshotOnly());
  }
}

TEST(PapyrusSnapshotIndex, SnapshotFlagFileIsCapturedNotIndexedAsAScript) {
  ScratchRoot scratch("snapshot-flags");
  ScriptIndex index;
  index.addSnapshot(scratch.path(), {source("Institute_Papyrus_Flags.flg", "Flag Hidden 5\n"),
                                     source("A.psc", "Scriptname A\n")});
  ASSERT_TRUE(index.snapshotFlags().has_value());
  EXPECT_EQ(*index.snapshotFlags(), "Flag Hidden 5\n");
  EXPECT_EQ(index.count(), 1u);
}

TEST(PapyrusSnapshotIndex, CompileUsesTheCapturedFlagTableNotTheDisk) {
  ScratchRoot scratch("snapshot-compile");
  scratch.write("Institute_Papyrus_Flags.flg", "Flag Hidden 3\n");
  ScriptIndex index;
  index.addSnapshot(scratch.path(), {source("Institute_Papyrus_Flags.flg", "Flag Hidden 5\n")});
  auto script = Parser::parse("Scriptname Probe Hidden\n");
  const auto result =
      compileScript(index, &script, (scratch.path() / "Probe.psc").string(), PapyrusCompileOptions{});
  ASSERT_TRUE(result.success());
  ASSERT_EQ(result.pex->objects.size(), 1u);
  EXPECT_EQ(result.pex->objects[0].userFlags, 1u << 5);
}

TEST(PapyrusSnapshotIndex, CompileRefusesAnExplicitFlagFileAgainstASnapshot) {
  ScratchRoot scratch("snapshot-explicit");
  ScriptIndex index;
  index.addSnapshot(scratch.path(), {source("Institute_Papyrus_Flags.flg", "Flag Hidden 5\n")});
  auto script = Parser::parse("Scriptname Probe Hidden\n");
  PapyrusCompileOptions options;
  options.flagFile = scratch.path() / "Other.flg";
  const auto result =
      compileScript(index, &script, (scratch.path() / "Probe.psc").string(), options);
  EXPECT_FALSE(result.success());
  EXPECT_FALSE(result.sourcesComplete);
}

TEST(PapyrusSnapshotIndex, FilesystemRootRefusesAnOversizedTreeWithoutRetainingPartialResults) {
  ScratchRoot scratch("snapshot-entries");
  scratch.write("One.psc", "Scriptname One\n");
  scratch.write("Sub/Two.psc", "Scriptname Two\n");
  scratch.write("Sub/Three.psc", "Scriptname Three\n");
  ScriptIndex index;
  EXPECT_THROW(index.addRoot(scratch.path(), 1), std::runtime_error);
  EXPECT_EQ(index.count(), 0u);
  EXPECT_TRUE(index.roots().empty());
  index.addRoot(scratch.path(), 100);
  EXPECT_EQ(index.count(), 3u);
}

TEST(PapyrusSnapshotIndex, ReadLimitsRefuseOversizedAndAggregateSources) {
  ScratchRoot scratch("snapshot-limits");
  scratch.write("Large.psc", std::string(1024, 'x'));
  ScriptIndex index;
  index.addRoot(scratch.path(), 100);
  index.setReadLimits(64, 128);
  EXPECT_EQ(index.resolve("Large"), nullptr);

  scratch.write("Small1.psc", "Scriptname Small1\n");
  scratch.write("Small2.psc", "Scriptname Small2\n");
  ScriptIndex aggregate;
  aggregate.addRoot(scratch.path(), 100);
  aggregate.setReadLimits(64, 25);
  EXPECT_NE(aggregate.resolve("Small1"), nullptr);
  EXPECT_EQ(aggregate.resolve("Small2"), nullptr);
  EXPECT_THROW(aggregate.setReadLimits(1, 1), std::logic_error);
}

TEST(PapyrusSnapshotIndex, BoundedParseFileReportsBytesReadAndRefusesOverTheBound) {
  ScratchRoot scratch("snapshot-parsefile");
  const auto path = scratch.write("A.psc", "Scriptname A\n");
  std::size_t bytes = 0;
  const auto parsed = Parser::parseFile(path, 64, &bytes);
  ASSERT_TRUE(parsed.has_value());
  EXPECT_EQ(bytes, std::string("Scriptname A\n").size());
  EXPECT_FALSE(Parser::parseFile(path, 4, &bytes).has_value());
  EXPECT_TRUE(Parser::parseFile(path, 0, nullptr).has_value());
}

TEST(PapyrusSnapshotIndex, SnapshotNeverReopensCapturedSourceFiles) {
  ScratchRoot scratch("snapshot-never-reopens");
  const auto path = scratch.write("nested/Quest.psc", "Scriptname Changed\n");
  ScriptIndex index;
  index.addSnapshot(scratch.path(), {source("nested/Quest.psc", "Scriptname Quest\n")});
  const auto names = index.scriptNames();
  ASSERT_EQ(names.size(), 1u);
  EXPECT_EQ(names[0], "nested:Quest");
  ASSERT_NE(index.resolve("nested:Quest"), nullptr);
  EXPECT_EQ(index.resolve("nested:Quest")->name, "Quest");
  std::filesystem::remove(path);
  ASSERT_NE(index.resolve("nested:Quest"), nullptr);
  EXPECT_EQ(index.resolve("nested:Quest")->name, "Quest");
  std::filesystem::remove(scratch.path() / "nested");
  std::error_code linkError;
  std::filesystem::create_directory_symlink(scratch.path().parent_path(), scratch.path() / "nested", linkError);
  if (!linkError) {
    const auto afterSwap = index.scriptNames();
    ASSERT_EQ(afterSwap.size(), 1u);
    EXPECT_EQ(afterSwap[0], "nested:Quest");
  }
  EXPECT_EQ(index.parseCached(scratch.path() / "nested" / "other.psc"), nullptr);
  EXPECT_THROW(index.addSnapshot(scratch.path(), {source("../outside.psc", "Scriptname Outside\n")}),
               std::runtime_error);
}
