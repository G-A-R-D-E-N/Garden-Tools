#include "fo4/seq/seq.hpp"

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

std::vector<std::filesystem::path> collect(const std::vector<std::string>& roots) {
  std::vector<std::filesystem::path> found;
  for (const auto& root : roots) {
    std::error_code error;
    const std::filesystem::path base(root);
    if (!std::filesystem::is_directory(base, error)) continue;
    for (std::filesystem::recursive_directory_iterator it(base, error), end; it != end; it.increment(error)) {
      if (error) break;
      if (!it->is_regular_file(error)) continue;
      if (lowerExtension(it->path()) == ".seq") found.push_back(it->path());
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

}

TEST(SeqCorpus, EveryLooseSeqRoundTripsByteIdentically) {
  const char* roots = std::getenv("FO4_SEQ_CORPUS");
  if (roots == nullptr) {
    GTEST_SKIP() << "set FO4_SEQ_CORPUS to one or more directories of .seq files";
  }
  const auto files = collect(splitRoots(roots));
  if (files.empty()) GTEST_SKIP() << "no .seq files under " << roots;

  std::size_t found = 0;
  std::size_t identical = 0;
  std::size_t unreadable = 0;
  std::size_t mismatched = 0;
  std::size_t entries = 0;
  std::size_t empty = 0;
  std::map<std::size_t, std::size_t> sizes;

  for (const auto& file : files) {
    found++;
    std::vector<std::uint8_t> input;
    try {
      input = readBytes(file);
    } catch (const std::exception& error) {
      unreadable++;
      std::cout << "seq corpus: unreadable " << file.string() << ": " << error.what() << std::endl;
      continue;
    }
    sizes[input.size()]++;
    if (input.empty()) empty++;

    try {
      const fo4::seq::SeqFile parsed = fo4::seq::SeqFile::fromBytes(input);
      entries += parsed.entries.size();
      if (parsed.toBytes() == input) {
        identical++;
      } else {
        mismatched++;
        std::cout << "seq corpus: round trip differs for " << file.string() << std::endl;
      }
    } catch (const std::exception& error) {
      unreadable++;
      std::cout << "seq corpus: unreadable " << file.string() << ": " << error.what() << std::endl;
    }
  }

  std::cout << "seq corpus: found=" << found << " identical=" << identical << " entries=" << entries
            << " empty=" << empty << " unreadable=" << unreadable << " mismatched=" << mismatched
            << " sizes=";
  for (const auto& entry : sizes) std::cout << entry.first << "x" << entry.second << " ";
  std::cout << std::endl;

  EXPECT_EQ(identical + unreadable, found) << "every found file must be read or counted";
  EXPECT_EQ(unreadable, 0u) << "files that failed to read";
  EXPECT_EQ(mismatched, 0u) << "files that did not round trip byte-identically";
  EXPECT_EQ(identical, found) << "byte-identical round trips";
}
