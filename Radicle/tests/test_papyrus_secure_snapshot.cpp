#include "papyrus_scratch_root.hpp"

#include <gtest/gtest.h>

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#include <fo4/papyrus/compile_script.hpp>
#include <fo4/papyrus/parser.hpp>
#include <fo4/papyrus/script_index.hpp>
#include <fo4/papyrus/secure_source_snapshot.hpp>

#ifdef __linux__
#include <fcntl.h>
#include <sched.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winioctl.h>
#endif

namespace {

using fo4::papyrus::test::ScratchRoot;

#if defined(__linux__) || defined(_WIN32)

class HeldRoot {
public:
  explicit HeldRoot(const std::filesystem::path& directory) {
#ifdef __linux__
    handle_ = ::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    valid_ = handle_ >= 0;
#else
    handle_ = ::CreateFileW(directory.c_str(), FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    valid_ = handle_ != INVALID_HANDLE_VALUE;
#endif
  }

  ~HeldRoot() {
    if (!valid_) return;
#ifdef __linux__
    ::close(handle_);
#else
    ::CloseHandle(handle_);
#endif
  }

  HeldRoot(const HeldRoot&) = delete;
  HeldRoot& operator=(const HeldRoot&) = delete;

  bool valid() const { return valid_; }

#ifdef __linux__
  std::intptr_t token() const { return handle_; }

private:
  int handle_{-1};
#else
  std::intptr_t token() const { return reinterpret_cast<std::intptr_t>(handle_); }

private:
  HANDLE handle_{INVALID_HANDLE_VALUE};
#endif
  bool valid_{false};
};

std::vector<fo4::papyrus::SnapshotSource> snapshot(const HeldRoot& root, std::size_t entries,
                                                   std::size_t fileBytes, std::size_t totalBytes) {
  return fo4::papyrus::snapshotPapyrusSources(root.token(), entries, fileBytes, totalBytes);
}

template <typename Action>
void expectRefusal(Action&& action, const char* fragment) {
  try {
    action();
  } catch (const std::runtime_error& error) {
    EXPECT_NE(std::string(error.what()).find(fragment), std::string::npos)
        << "expected \"" << fragment << "\" in: " << error.what();
    return;
  }
  ADD_FAILURE() << "expected a refusal mentioning \"" << fragment << "\"";
}

#endif

}

#if defined(__linux__) || defined(_WIN32)

TEST(PapyrusSecureSnapshot, CapturesRelativeSourcesInSortedOrder) {
  ScratchRoot scratch("secure-snapshot");
  scratch.write("nested/Quest.psc", "Scriptname Quest\n");
  scratch.write("Alpha.psc", "Scriptname Alpha\n");
  scratch.write("notes.txt", "ignored\n");
  HeldRoot root(scratch.path());
  ASSERT_TRUE(root.valid());
  const auto files = snapshot(root, 100, 128, 256);
  ASSERT_EQ(files.size(), 2u);
  EXPECT_EQ(files[0].relativePath, std::filesystem::path("Alpha.psc"));
  EXPECT_EQ(files[1].relativePath, std::filesystem::path("nested") / "Quest.psc");
  EXPECT_EQ(files[1].text, "Scriptname Quest\n");
}

TEST(PapyrusSecureSnapshot, RefusesOversizedFilesAndTrees) {
  ScratchRoot scratch("secure-snapshot-bounds");
  scratch.write("Quest.psc", "Scriptname Quest\n");
  scratch.write("Other.psc", "Scriptname Other\n");
  HeldRoot root(scratch.path());
  ASSERT_TRUE(root.valid());
  expectRefusal([&] { (void)snapshot(root, 100, 4, 256); }, "not a bounded regular file");
  expectRefusal([&] { (void)snapshot(root, 100, 128, 20); }, "exceeds its byte limit");
  expectRefusal([&] { (void)snapshot(root, 1, 128, 256); }, "exceeds its entry limit");
  expectRefusal([&] { (void)snapshot(root, 0, 128, 256); }, "invalid Papyrus snapshot limits");
}

TEST(PapyrusSecureSnapshot, CapsDirectoryDepth) {
  ScratchRoot scratch("secure-snapshot-depth");
  auto nested = scratch.path();
  for (int i = 0; i < 70; ++i) {
    nested /= "d";
    std::filesystem::create_directories(nested);
  }
  HeldRoot root(scratch.path());
  ASSERT_TRUE(root.valid());
  expectRefusal([&] { (void)snapshot(root, 1000, 128, 256); }, "exceeds its depth limit");
}

TEST(PapyrusSecureSnapshot, CapturedFlagFileFeedsTheCompilerNotTheDiskCopy) {
  ScratchRoot scratch("secure-snapshot-flags");
  scratch.write("Institute_Papyrus_Flags.flg", "Flag Hidden 5\n");
  HeldRoot root(scratch.path());
  ASSERT_TRUE(root.valid());
  const auto captured = snapshot(root, 20, 1024, 4096);
  fo4::papyrus::ScriptIndex index;
  index.addSnapshot(scratch.path(), captured);
  ASSERT_TRUE(index.snapshotFlags().has_value());
  scratch.write("Institute_Papyrus_Flags.flg", "Flag Hidden 3\n");
  auto script = fo4::papyrus::Parser::parse("Scriptname Probe Hidden\n");
  const auto result = fo4::papyrus::compileScript(
      index, &script, (scratch.path() / "Probe.psc").string(), fo4::papyrus::PapyrusCompileOptions{});
  ASSERT_TRUE(result.success());
  ASSERT_EQ(result.pex->objects.size(), 1u);
  EXPECT_EQ(result.pex->objects[0].userFlags, 1u << 5);
}

#endif

#ifdef __linux__

TEST(PapyrusSecureSnapshot, RefusesSymlinkedEntries) {
  ScratchRoot scratch("secure-snapshot-link");
  scratch.write("imports/Quest.psc", "Scriptname Quest\n");
  const auto outside = scratch.write("outside.flg", "Flag Hidden 5\n");
  std::error_code ec;
  std::filesystem::create_symlink(outside, scratch.path() / "imports" / "Institute_Papyrus_Flags.flg", ec);
  if (ec) GTEST_SKIP() << "symlinks are unavailable";
  HeldRoot root(scratch.path() / "imports");
  ASSERT_TRUE(root.valid());
  expectRefusal([&] { (void)snapshot(root, 20, 1024, 4096); }, "unsupported import entry");
}

#endif

#ifdef FO4_TEST_HOOKS
#ifdef __linux__

namespace {

std::filesystem::path hookedFile;

void rewriteAfterRead() {
  { std::ofstream out(hookedFile, std::ios::binary | std::ios::trunc); out << "Scriptname Other\n"; }
  const auto now = std::filesystem::last_write_time(hookedFile);
  const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch());
  std::filesystem::last_write_time(hookedFile,
                                   std::filesystem::file_time_type(seconds + std::chrono::milliseconds(200)));
}

void swapForFifo() {
  std::filesystem::remove(hookedFile);
  if (::mkfifo(hookedFile.c_str(), 0600) != 0) throw std::runtime_error("cannot make test FIFO");
}

struct HookReset {
  ~HookReset() {
    fo4::papyrus::setSnapshotReadHook(nullptr);
    fo4::papyrus::setSnapshotBeforeOpenHook(nullptr);
  }
};

}

TEST(PapyrusSecureSnapshot, RefusesASourceRewrittenWhileItIsRead) {
  ScratchRoot scratch("secure-snapshot-rewrite");
  hookedFile = scratch.write("Quest.psc", "Scriptname Quest\n");
  HeldRoot root(scratch.path());
  ASSERT_TRUE(root.valid());
  HookReset reset;
  fo4::papyrus::setSnapshotReadHook(rewriteAfterRead);
  expectRefusal([&] { (void)snapshot(root, 100, 128, 256); }, "changed while reading");
}

TEST(PapyrusSecureSnapshot, RefusesAFlagFileReplacedWhileItIsRead) {
  ScratchRoot scratch("secure-snapshot-flag-rewrite");
  hookedFile = scratch.write("Institute_Papyrus_Flags.flg", "Flag Hidden 5\n");
  HeldRoot root(scratch.path());
  ASSERT_TRUE(root.valid());
  HookReset reset;
  fo4::papyrus::setSnapshotReadHook(rewriteAfterRead);
  expectRefusal([&] { (void)snapshot(root, 100, 1024, 4096); }, "changed while reading");
}

TEST(PapyrusSecureSnapshot, RefusesAnEntrySwappedForASpecialFileBeforeOpen) {
  ScratchRoot scratch("secure-snapshot-fifo");
  hookedFile = scratch.write("Quest.psc", "Scriptname Quest\n");
  HeldRoot root(scratch.path());
  ASSERT_TRUE(root.valid());
  HookReset reset;
  fo4::papyrus::setSnapshotBeforeOpenHook(swapForFifo);
  expectRefusal([&] { (void)snapshot(root, 100, 128, 256); }, "changed while opening");
}

TEST(PapyrusSecureSnapshot, RefusesNestedBindMounts) {
  ScratchRoot scratch("secure-snapshot-mount");
  std::filesystem::create_directories(scratch.path() / "tree" / "mounted");
  scratch.write("outside/Quest.psc", "Scriptname Quest\n");
  constexpr int kRefusedCrossMount = 0;
  constexpr int kDidNotThrow = 1;
  constexpr int kRootOpenFailed = 2;
  constexpr int kWrongErrno = 3;
  constexpr int kWrongException = 4;
  constexpr int kNamespaceUnavailable = 77;
  const auto child = ::fork();
  ASSERT_GE(child, 0);
  if (child == 0) {
    if (::unshare(CLONE_NEWNS) != 0 || ::mount(nullptr, "/", nullptr, MS_REC | MS_PRIVATE, nullptr) != 0 ||
        ::mount((scratch.path() / "outside").c_str(), (scratch.path() / "tree" / "mounted").c_str(), nullptr,
                MS_BIND, nullptr) != 0) {
      _exit(kNamespaceUnavailable);
    }
    const int fd = ::open((scratch.path() / "tree").c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (fd < 0) _exit(kRootOpenFailed);
    int outcome = kDidNotThrow;
    try {
      (void)fo4::papyrus::snapshotPapyrusSources(fd, 100, 128, 256);
    } catch (const std::system_error& error) {
      outcome = error.code().value() == EXDEV ? kRefusedCrossMount : kWrongErrno;
    } catch (...) {
      outcome = kWrongException;
    }
    ::close(fd);
    _exit(outcome);
  }
  int status = 0;
  ASSERT_EQ(::waitpid(child, &status, 0), child);
  ASSERT_TRUE(WIFEXITED(status));
  if (WEXITSTATUS(status) == kNamespaceUnavailable) GTEST_SKIP() << "mount namespace or bind mount is unavailable";
  EXPECT_EQ(WEXITSTATUS(status), kRefusedCrossMount)
      << "1 did not throw, 2 root open failed, 3 threw a system_error other than EXDEV, 4 threw another type";
}

#endif

#ifdef _WIN32

namespace {

std::filesystem::path heldFlag;
bool writeBlocked = false;

void tryWriteHeldFlag() {
  HANDLE file = ::CreateFileW(heldFlag.c_str(), GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    if (::GetLastError() == ERROR_SHARING_VIOLATION) writeBlocked = true;
  } else {
    ::CloseHandle(file);
  }
}

struct HookReset {
  ~HookReset() { fo4::papyrus::setSnapshotReadHook(nullptr); }
};

}

TEST(PapyrusSecureSnapshot, WindowsHoldsAFlagFileAgainstConcurrentWriters) {
  ScratchRoot scratch("secure-snapshot-held");
  heldFlag = scratch.write("Institute_Papyrus_Flags.flg", "Flag Hidden 5\n");
  HeldRoot root(scratch.path());
  ASSERT_TRUE(root.valid());
  writeBlocked = false;
  HookReset reset;
  fo4::papyrus::setSnapshotReadHook(tryWriteHeldFlag);
  const auto captured = snapshot(root, 20, 1024, 4096);
  EXPECT_TRUE(writeBlocked);
  ASSERT_EQ(captured.size(), 1u);
  EXPECT_EQ(captured[0].text, "Flag Hidden 5\n");
}

TEST(PapyrusSecureSnapshot, WindowsRefusesAHardLinkedFlagFile) {
  ScratchRoot scratch("secure-snapshot-hardlink");
  const auto external = scratch.write("outside.flg", "Flag Hidden 5\n");
  std::filesystem::create_directories(scratch.path() / "imports");
  std::error_code error;
  std::filesystem::create_hard_link(external, scratch.path() / "imports" / "Institute_Papyrus_Flags.flg", error);
  if (error) GTEST_SKIP() << "hard links are unavailable on this filesystem";
  HeldRoot root(scratch.path() / "imports");
  ASSERT_TRUE(root.valid());
  expectRefusal([&] { (void)snapshot(root, 20, 1024, 4096); }, "not a bounded regular file");
}

namespace {

std::filesystem::path swapDirectory;
std::filesystem::path swapTarget;
bool swapSucceeded = false;

void convertDirectoryToJunction() {
  const HANDLE handle = ::CreateFileW(swapDirectory.c_str(), GENERIC_WRITE,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                      OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
                                      nullptr);
  if (handle == INVALID_HANDLE_VALUE) throw std::runtime_error("cannot open test junction");
  const auto name = std::wstring(L"\\??\\") + swapTarget.native();
  const auto length = static_cast<std::uint16_t>(name.size() * sizeof(wchar_t));
  std::vector<std::byte> buffer(16 + length + 4);
  const std::uint32_t tag = IO_REPARSE_TAG_MOUNT_POINT;
  const auto dataLength = static_cast<std::uint16_t>(8 + length + 4);
  const auto printOffset = static_cast<std::uint16_t>(length + 2);
  std::memcpy(buffer.data(), &tag, sizeof(tag));
  std::memcpy(buffer.data() + 4, &dataLength, sizeof(dataLength));
  std::memcpy(buffer.data() + 10, &length, sizeof(length));
  std::memcpy(buffer.data() + 12, &printOffset, sizeof(printOffset));
  std::memcpy(buffer.data() + 16, name.data(), length);
  DWORD written = 0;
  const bool changed = ::DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT, buffer.data(),
                                         static_cast<DWORD>(buffer.size()), nullptr, 0, &written, nullptr) != 0;
  ::CloseHandle(handle);
  if (!changed) throw std::runtime_error("cannot create test junction");
  swapSucceeded = true;
}

struct JunctionHookReset {
  ~JunctionHookReset() {
    fo4::papyrus::setSnapshotBeforeOpenHook(nullptr);
    fo4::papyrus::setSnapshotAfterDirectoryOpenHook(nullptr);
  }
};

}

TEST(PapyrusSecureSnapshot, WindowsAcceptsOrdinaryCaseInsensitiveRootSpelling) {
  ScratchRoot scratch("secure-snapshot-case");
  scratch.write("base/Quest.psc", "Scriptname Quest\n");
  HeldRoot root(scratch.path() / "BASE");
  ASSERT_TRUE(root.valid());
  const auto files = snapshot(root, 100, 128, 256);
  ASSERT_EQ(files.size(), 1u);
  EXPECT_EQ(files[0].relativePath, std::filesystem::path("Quest.psc"));
  EXPECT_EQ(files[0].text, "Scriptname Quest\n");
}

TEST(PapyrusSecureSnapshot, WindowsAuthorizedRootRemainsTiedToItsOpenedDirectory) {
  for (const bool replaceAncestor : {false, true}) {
    for (const bool junction : {false, true}) {
      ScratchRoot scratch("secure-snapshot-tied");
      const auto ancestor = scratch.path() / "parent";
      const auto imports = ancestor / "imports";
      const auto replaced = replaceAncestor ? ancestor : imports;
      const auto moved = scratch.path() / "held";
      const auto outside = scratch.path() / "outside";
      std::filesystem::create_directories(imports);
      std::filesystem::create_directories(outside);
      scratch.write("parent/imports/Original.psc", "Scriptname Original\n");
      scratch.write("outside/Outside.psc", "Scriptname Outside\n");
      const HANDLE handle = ::CreateFileW(imports.c_str(), FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
                                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                          OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
                                          nullptr);
      ASSERT_NE(handle, INVALID_HANDLE_VALUE);
      if (replaceAncestor) {
        EXPECT_THROW(std::filesystem::rename(replaced, moved), std::filesystem::filesystem_error);
      } else {
        std::filesystem::rename(replaced, moved);
        std::filesystem::create_directories(replaced);
        if (junction) {
          swapDirectory = replaced;
          swapTarget = outside;
          swapSucceeded = false;
          convertDirectoryToJunction();
          ASSERT_TRUE(swapSucceeded);
        } else {
          scratch.write("parent/imports/Outside.psc", "Scriptname Outside\n");
        }
      }
      const auto files =
          fo4::papyrus::snapshotPapyrusSources(reinterpret_cast<std::intptr_t>(handle), 100, 128, 256);
      ::CloseHandle(handle);
      ASSERT_EQ(files.size(), 1u);
      EXPECT_EQ(files[0].relativePath, std::filesystem::path("Original.psc"));
      EXPECT_EQ(files[0].text, "Scriptname Original\n");
      if (junction && !replaceAncestor) { EXPECT_NE(::RemoveDirectoryW(replaced.c_str()), 0); }
    }
  }
}

TEST(PapyrusSecureSnapshot, WindowsRefusesAJunctionIntroducedBeforeChildOpen) {
  ScratchRoot scratch("secure-snapshot-junction-before");
  const auto imports = scratch.path() / "imports";
  swapSucceeded = false;
  swapDirectory = imports / "swap";
  swapTarget = scratch.path() / "outside";
  std::filesystem::create_directories(swapDirectory);
  std::filesystem::create_directories(swapTarget);
  scratch.write("outside/Outside.psc", "Scriptname Outside\n");
  HeldRoot root(imports);
  ASSERT_TRUE(root.valid());
  JunctionHookReset reset;
  fo4::papyrus::setSnapshotBeforeOpenHook(convertDirectoryToJunction);
  EXPECT_ANY_THROW((void)snapshot(root, 100, 128, 256));
  EXPECT_TRUE(swapSucceeded);
  fo4::papyrus::setSnapshotBeforeOpenHook(nullptr);
  EXPECT_NE(::RemoveDirectoryW(swapDirectory.c_str()), 0);
}

TEST(PapyrusSecureSnapshot, WindowsDoesNotFollowAJunctionIntroducedAfterChildOpen) {
  ScratchRoot scratch("secure-snapshot-junction-after");
  const auto imports = scratch.path() / "imports";
  swapSucceeded = false;
  swapDirectory = imports / "swap";
  swapTarget = scratch.path() / "outside";
  std::filesystem::create_directories(swapDirectory);
  std::filesystem::create_directories(swapTarget);
  scratch.write("outside/Outside.psc", "Scriptname Outside\n");
  HeldRoot root(imports);
  ASSERT_TRUE(root.valid());
  JunctionHookReset reset;
  fo4::papyrus::setSnapshotAfterDirectoryOpenHook(convertDirectoryToJunction);
  try {
    EXPECT_TRUE(snapshot(root, 100, 128, 256).empty());
  } catch (const std::exception&) {
  }
  fo4::papyrus::setSnapshotAfterDirectoryOpenHook(nullptr);
  EXPECT_TRUE(swapSucceeded);
  EXPECT_NE(::RemoveDirectoryW(swapDirectory.c_str()), 0);
}

#endif

#endif
