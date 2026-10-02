#include "fo4/io/atomic_write.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>
#include <vector>

namespace {

std::filesystem::path temp_root() {
  const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path() /
         ("radicle_atomic_write_" + std::to_string(id));
}

struct Cleanup {
  std::filesystem::path root;
  ~Cleanup() {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }
};

std::vector<std::byte> bytes(std::string_view text) {
  return {reinterpret_cast<const std::byte*>(text.data()),
          reinterpret_cast<const std::byte*>(text.data() + text.size())};
}

std::string read_text(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string{std::istreambuf_iterator<char>(in),
                     std::istreambuf_iterator<char>()};
}

bool has_radicle_temp(const std::filesystem::path& parent) {
  if (!std::filesystem::exists(parent)) return false;
  for (const auto& entry : std::filesystem::directory_iterator(parent)) {
    if (entry.path().filename().string().find(".radicle-tmp") != std::string::npos) {
      return true;
    }
  }
  return false;
}

TEST(AtomicWrite, RoundTripReplacesExistingFileAtomically) {
  const auto root = temp_root();
  Cleanup cleanup{root};
  const auto target = root / "out" / "file.bin";

  ASSERT_NO_THROW((fo4::io::write_file_atomic(target, bytes("first"))));
  ASSERT_NO_THROW((fo4::io::write_file_atomic(target, bytes("second-over-write"))));

  std::ifstream in(target, std::ios::binary);
  std::string contents{std::istreambuf_iterator<char>(in), {}};
  EXPECT_EQ(contents, "second-over-write");

  bool stray_temp_files = false;
  for (const auto& entry : std::filesystem::directory_iterator(target.parent_path())) {
    if (entry.path().filename().string().find(".radicle-tmp") != std::string::npos) {
      stray_temp_files = true;
    }
  }
  EXPECT_FALSE(stray_temp_files);
}

// Forces the failure inside the replace primitive itself: staging succeeds
// (the parent directory is writable), but rename/MoveFileExW cannot replace
// a directory with a file. The pre-existing "destination" must come out of
// the failed attempt completely untouched.
TEST(AtomicWrite, ReplacePrimitiveFailureLeavesExistingDestinationIntact) {
  const auto root = temp_root();
  Cleanup cleanup{root};
  const auto target = root / "out" / "blocked";
  std::filesystem::create_directories(target / "inner");
  const auto sentinel = target / "inner" / "sentinel.txt";
  {
    std::ofstream out(sentinel, std::ios::binary);
    out << "original-content";
  }

  EXPECT_THROW((fo4::io::write_file_atomic(target, bytes("payload"))),
               std::exception);

  // The destination is still the same directory, with the same contents.
  std::error_code ec;
  EXPECT_TRUE(std::filesystem::is_directory(target, ec));
  std::ifstream in(sentinel, std::ios::binary);
  std::string contents{std::istreambuf_iterator<char>(in), {}};
  EXPECT_EQ(contents, "original-content");

  bool stray_temp_files = false;
  for (const auto& entry : std::filesystem::directory_iterator(target.parent_path())) {
    if (entry.path().filename().string().find(".radicle-tmp") != std::string::npos) {
      stray_temp_files = true;
    }
  }
  EXPECT_FALSE(stray_temp_files);
}

TEST(AtomicWrite, WritesNonAnsiFileNames) {
  const auto root = temp_root();
  Cleanup cleanup{root};
  std::filesystem::create_directories(root);
  const auto target = root / std::filesystem::path(std::u16string{0x041C, 0x043E, 0x0434, 0x044B, u'.', u'b', u'a', u'2'});
  fo4::io::write_file_atomic(target, bytes("payload"));
  EXPECT_EQ(read_text(target), "payload");
  EXPECT_EQ(std::distance(std::filesystem::directory_iterator(root), std::filesystem::directory_iterator()), 1);
}

}

// Regression: moving a StagedFile must transfer exclusive ownership of the
// OS handle. A defaulted move would copy the fd/HANDLE and double-close it
// when both destructors ran.
TEST(AtomicWrite, MoveTransfersHandleOwnershipExactlyOnce) {
  const auto root = temp_root();
  Cleanup cleanup{root};
  const auto target = root / "out" / "moved.bin";
  const auto other = root / "out" / "other.bin";
  std::filesystem::create_directories(target.parent_path());

  // Stage the payload, then pass it through a move-constructor chain.
  auto first = fo4::io::StagedFile::create(target);
  first.write(bytes("payload"));
  first.finish();

  auto second = std::move(first);   // move ctor: first is empty from here on
  auto third = std::move(second);   // chained move ctor

  // Build an independent stage, commit it, and then move-assign over the
  // spent object so it takes ownership of the target stage.
  {
    auto spare = fo4::io::StagedFile::create(other);
    spare.write(bytes("spare"));
    spare.finish();
    std::move(spare).commit();
  }
  auto holder = fo4::io::StagedFile::create(root / "out" / "holder.bin");
  holder.write(bytes("placeholder"));
  holder.finish();
  std::move(holder).commit();       // holder's handle is closed now
  holder = std::move(third);        // move assignment: takes over target stage

  // The final owner verifies and commits; moved-from shells (first, second,
  // third) destruct without touching the fd/HANDLE. A defaulted move would
  // double-close here.
  holder.verify(bytes("payload").size());
  std::move(holder).commit();

  std::ifstream in(target, std::ios::binary);
  std::string contents{std::istreambuf_iterator<char>(in), {}};
  EXPECT_EQ(contents, "payload");
  std::ifstream otherIn(other, std::ios::binary);
  std::string otherContents{std::istreambuf_iterator<char>(otherIn), {}};
  EXPECT_EQ(otherContents, "spare");

  bool stray_temp_files = false;
  for (const auto& entry : std::filesystem::directory_iterator(target.parent_path())) {
    if (entry.path().filename().string().find(".radicle-tmp") != std::string::npos) {
      stray_temp_files = true;
    }
  }
  EXPECT_FALSE(stray_temp_files);
}

TEST(AtomicWrite, MoveAssignmentDiscardsTheReplacedUncommittedStage) {
  const auto root = temp_root();
  Cleanup cleanup{root};
  const auto target = root / "out" / "target.bin";
  const auto replacedDestination = root / "out" / "replaced.bin";
  std::filesystem::create_directories(target.parent_path());

  auto incoming = fo4::io::StagedFile::create(target);
  incoming.write(bytes("payload"));
  incoming.finish();

  auto replaced = fo4::io::StagedFile::create(replacedDestination);
  replaced.write(bytes("discarded"));
  replaced.finish();

  // The uncommitted stage for replacedDestination must be removed before
  // assignment takes over incoming's stage.
  replaced = std::move(incoming);
  std::move(replaced).commit();

  std::ifstream in(target, std::ios::binary);
  std::string contents{std::istreambuf_iterator<char>(in), {}};
  EXPECT_EQ(contents, "payload");

  bool stray_temp_files = false;
  for (const auto& entry : std::filesystem::directory_iterator(target.parent_path())) {
    if (entry.path().filename().string().find(".radicle-tmp") != std::string::npos) {
      stray_temp_files = true;
    }
  }
  EXPECT_FALSE(stray_temp_files);
}

TEST(AtomicWrite, DependencyFirstPairCommitsBothOutputs) {
  const auto root = temp_root();
  Cleanup cleanup{root};
  std::filesystem::create_directories(root);
  const auto dependency = root / "Patch_en.DLSTRINGS";
  const auto primary = root / "Patch.esp";
  {
    std::ofstream out(dependency, std::ios::binary);
    out << "old-sidecar";
  }
  {
    std::ofstream out(primary, std::ios::binary);
    out << "old-plugin";
  }

  ASSERT_NO_THROW((fo4::io::write_file_pair_dependency_first(
      dependency, bytes("new-sidecar-superset"), primary, bytes("new-plugin"))));

  EXPECT_EQ(read_text(dependency), "new-sidecar-superset");
  EXPECT_EQ(read_text(primary), "new-plugin");
  EXPECT_FALSE(has_radicle_temp(root));
}

TEST(AtomicWrite, DependencyFirstPairMissingDependencyFailsBeforeStaging) {
  const auto root = temp_root();
  Cleanup cleanup{root};
  std::filesystem::create_directories(root);
  const auto dependency = root / "missing_en.DLSTRINGS";
  const auto primary = root / "Patch.esp";
  {
    std::ofstream out(primary, std::ios::binary);
    out << "old-plugin";
  }

  EXPECT_THROW((fo4::io::write_file_pair_dependency_first(
      dependency, bytes("new-sidecar"), primary, bytes("new-plugin"))),
               std::exception);

  EXPECT_FALSE(std::filesystem::exists(dependency));
  EXPECT_EQ(read_text(primary), "old-plugin");
  EXPECT_FALSE(has_radicle_temp(root));
}

#if !defined(_WIN32)
TEST(AtomicWrite, DependencyFirstPairRefusesSymlinkDependency) {
  const auto root = temp_root();
  Cleanup cleanup{root};
  std::filesystem::create_directories(root);
  const auto real_dependency = root / "real_en.DLSTRINGS";
  const auto dependency = root / "Patch_en.DLSTRINGS";
  const auto primary = root / "Patch.esp";
  {
    std::ofstream out(real_dependency, std::ios::binary);
    out << "real-sidecar";
  }
  {
    std::ofstream out(primary, std::ios::binary);
    out << "old-plugin";
  }
  std::filesystem::create_symlink(real_dependency.filename(), dependency);

  EXPECT_THROW((fo4::io::write_file_pair_dependency_first(
      dependency, bytes("new-sidecar"), primary, bytes("new-plugin"))),
               std::exception);

  EXPECT_EQ(read_text(real_dependency), "real-sidecar");
  EXPECT_EQ(read_text(primary), "old-plugin");
  EXPECT_TRUE(std::filesystem::is_symlink(dependency));
  EXPECT_FALSE(has_radicle_temp(root));
}
#endif

TEST(AtomicWrite, DependencyFirstPairStagingFailureChangesNeitherDestination) {
  const auto root = temp_root();
  Cleanup cleanup{root};
  std::filesystem::create_directories(root);
  const auto dependency = root / "Patch_en.DLSTRINGS";
  const auto primary = root / "missing" / "Patch.esp";
  {
    std::ofstream out(dependency, std::ios::binary);
    out << "old-sidecar";
  }

  EXPECT_THROW((fo4::io::write_file_pair_dependency_first(
      dependency, bytes("new-sidecar"), primary, bytes("new-plugin"))),
               std::exception);

  EXPECT_EQ(read_text(dependency), "old-sidecar");
  EXPECT_FALSE(std::filesystem::exists(primary));
  EXPECT_FALSE(has_radicle_temp(root));
}

TEST(AtomicWrite, DependencyFirstPairRestoresDependencyWhenPrimaryCommitFails) {
  const auto root = temp_root();
  Cleanup cleanup{root};
  std::filesystem::create_directories(root);
  const auto dependency = root / "Patch_en.DLSTRINGS";
  const auto primary = root / "blocked-primary";
  {
    std::ofstream out(dependency, std::ios::binary);
    out << "old-sidecar";
  }
  std::filesystem::create_directories(primary / "inner");
  const auto sentinel = primary / "inner" / "sentinel.txt";
  {
    std::ofstream out(sentinel, std::ios::binary);
    out << "old-primary";
  }

  EXPECT_THROW((fo4::io::write_file_pair_dependency_first(
      dependency, bytes("new-sidecar-superset"), primary, bytes("new-plugin"))),
               std::exception);

  // The primary replace failed, so its original destination is untouched and
  // the already-committed dependency has been restored from the rollback stage.
  EXPECT_EQ(read_text(dependency), "old-sidecar");
  EXPECT_TRUE(std::filesystem::is_directory(primary));
  EXPECT_EQ(read_text(sentinel), "old-primary");
  EXPECT_FALSE(has_radicle_temp(root));
}
