#include "fo4/archive/ba2.hpp"
#include "fo4/pex/pex.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <system_error>
#include <vector>

namespace {

#ifdef _WIN32
constexpr char kRootSeparator = ';';
#else
constexpr char kRootSeparator = ':';
#endif

std::vector<std::string> splitRoots(const std::string& value) {
  std::vector<std::string> roots;
  std::string current;
  for (const char ch : value) {
    if (ch == kRootSeparator) {
      if (!current.empty()) roots.push_back(current);
      current.clear();
      continue;
    }
    current.push_back(ch);
  }
  if (!current.empty()) roots.push_back(current);
  return roots;
}

std::string lowerExtension(const std::filesystem::path& path) {
  std::string extension = path.extension().string();
  for (char& ch : extension) {
    if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
  }
  return extension;
}

/* Collapse the variable parts of a message so distinct causes group together,
   the same way the plugin corpus harness does. */
std::string normalize(std::string message) {
  std::string out;
  bool in_number = false;
  for (const char ch : message) {
    const bool digit = ch >= '0' && ch <= '9';
    const bool hex_body = in_number && ((ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F') || ch == 'x');
    if (digit || hex_body) {
      if (!in_number) {
        out += '#';
        in_number = true;
      }
      continue;
    }
    in_number = false;
    out += ch;
  }
  return out;
}

struct Tally {
  std::size_t found{0};
  std::size_t read{0};
  std::size_t identical{0};
  std::size_t unreadable{0};
  std::size_t mismatched{0};
  std::map<std::string, std::size_t> causes;
  std::map<std::string, std::string> firstExample;

  void note(const std::string& message, const std::string& where) {
    const auto key = normalize(message);
    if (!causes.count(key)) firstExample[key] = where + ": " + message;
    causes[key]++;
  }
};

std::vector<std::filesystem::path> collectLoosePex(const std::vector<std::string>& roots) {
  std::vector<std::filesystem::path> found;
  for (const auto& root : roots) {
    std::error_code error;
    const std::filesystem::path base(root);
    if (!std::filesystem::is_directory(base, error)) continue;
    for (std::filesystem::recursive_directory_iterator it(base, error), end; it != end; it.increment(error)) {
      if (error) break;
      if (!it->is_regular_file(error)) continue;
      if (lowerExtension(it->path()) == ".pex") found.push_back(it->path());
    }
  }
  std::sort(found.begin(), found.end());
  return found;
}

std::vector<std::uint8_t> readBytes(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) throw std::runtime_error("cannot open " + path.string());
  const std::vector<char> raw((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
  return std::vector<std::uint8_t>(raw.begin(), raw.end());
}

std::vector<std::uint8_t> unpack(const std::vector<std::byte>& raw) {
  std::vector<std::uint8_t> bytes;
  bytes.reserve(raw.size());
  for (const std::byte byte : raw) bytes.push_back(std::to_integer<std::uint8_t>(byte));
  return bytes;
}

void roundTrip(const std::vector<std::uint8_t>& bytes, const std::string& where, Tally& tally) {
  tally.found++;
  std::vector<std::uint8_t> rewritten;
  try {
    rewritten = fo4::pex::PexFile::fromBytes(bytes).toBytes();
  } catch (const std::exception& error) {
    tally.unreadable++;
    tally.note(error.what(), where);
    return;
  }
  tally.read++;
  if (rewritten == bytes) {
    tally.identical++;
    return;
  }
  tally.mismatched++;
  std::size_t offset = 0;
  while (offset < rewritten.size() && offset < bytes.size() && rewritten[offset] == bytes[offset]) ++offset;
  tally.note("round trip differs at offset " + std::to_string(offset) + " of " +
                 std::to_string(bytes.size()) + " bytes",
             where);
}

void report(const Tally& tally) {
  std::cout << "pex corpus: found=" << tally.found << " read=" << tally.read
            << " identical=" << tally.identical << " unreadable=" << tally.unreadable
            << " mismatched=" << tally.mismatched << std::endl;
  for (const auto& entry : tally.firstExample) {
    std::cout << "  class(" << tally.causes.at(entry.first) << "): " << entry.second << std::endl;
  }
}

void expectClean(const Tally& tally) {
  EXPECT_EQ(tally.read + tally.unreadable, tally.found) << "every found file must be read or counted";
  EXPECT_EQ(tally.unreadable, 0u) << "files that failed to read";
  EXPECT_EQ(tally.mismatched, 0u) << "files that did not round trip byte-identically";
  EXPECT_EQ(tally.identical, tally.found) << "byte-identical round trips";
}

}

TEST(PexCorpus, EveryLoosePexRoundTripsByteIdentically) {
  const char* roots = std::getenv("FO4_PEX_CORPUS");
  if (roots == nullptr) {
    GTEST_SKIP() << "set FO4_PEX_CORPUS to one or more directories of .pex files";
  }
  const auto files = collectLoosePex(splitRoots(roots));
  if (files.empty()) GTEST_SKIP() << "no .pex files under " << roots;

  Tally tally;
  for (const auto& file : files) {
    try {
      roundTrip(readBytes(file), file.string(), tally);
    } catch (const std::exception& error) {
      tally.found++;
      tally.unreadable++;
      tally.note(error.what(), file.string());
    }
  }
  report(tally);
  expectClean(tally);
}

TEST(PexCorpus, EveryArchivedPexRoundTripsByteIdentically) {
  const char* archives = std::getenv("FO4_PEX_ARCHIVES");
  if (archives == nullptr) {
    GTEST_SKIP() << "set FO4_PEX_ARCHIVES to .ba2 files holding Scripts/*.pex";
  }

  Tally tally;
  for (const auto& root : splitRoots(archives)) {
    const std::filesystem::path path(root);
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error)) continue;

    std::vector<fo4::archive::Ba2GeneralEntry> entries;
    try {
      entries = fo4::archive::read_ba2_general_file(path).entries;
    } catch (const std::exception& read_error) {
      tally.note(read_error.what(), path.filename().string());
      continue;
    }

    for (const auto& entry : entries) {
      if (lowerExtension(entry.path) != ".pex") continue;
      const std::string where = path.filename().string() + "!" + entry.path;
      try {
        roundTrip(unpack(fo4::archive::extract_ba2_general_entry(entry)), where, tally);
      } catch (const std::exception& error) {
        tally.found++;
        tally.unreadable++;
        tally.note(error.what(), where);
      }
    }
  }

  report(tally);
  ASSERT_GT(tally.found, 0u) << "no .pex entries found in " << archives;
  expectClean(tally);
}
