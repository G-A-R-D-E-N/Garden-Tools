#include "fo4/papyrus/decompiler.hpp"
#include "fo4/pex/pex.hpp"

#include "papyrus_corpus_roots.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace helper = fo4::papyrus::test;

namespace {

std::vector<std::filesystem::path> collectLoosePex(
    const std::vector<std::filesystem::path>& roots) {
  std::vector<std::filesystem::path> found;
  for (const auto& base : roots) {
    std::error_code error;
    if (!std::filesystem::is_directory(base, error)) continue;
    for (std::filesystem::recursive_directory_iterator it(base, error), end; it != end; it.increment(error)) {
      if (error) break;
      if (!it->is_regular_file(error)) continue;
      auto extension = it->path().extension().string();
      std::transform(extension.begin(), extension.end(), extension.begin(),
                     [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
      if (extension == ".pex") found.push_back(it->path());
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

std::size_t countOccurrences(const std::string& text, const std::string& needle) {
  std::size_t count = 0;
  for (auto at = text.find(needle); at != std::string::npos; at = text.find(needle, at + needle.size())) {
    ++count;
  }
  return count;
}

}  // namespace

TEST(PapyrusDecompilerReal, EveryCorpusPexDecompilesToSourceAndListingWithoutThrowing) {
  auto roots = helper::rootsFromEnv("FO4RE_PEX_CORPUS");
  if (roots.empty()) roots = helper::rootsFromEnv("FO4_PEX_CORPUS");
  if (roots.empty()) {
    GTEST_SKIP() << "set FO4RE_PEX_CORPUS to one or more directories of .pex files";
  }
  const auto files = collectLoosePex(roots);
  if (files.empty()) {
    GTEST_SKIP() << "no .pex files under the FO4RE_PEX_CORPUS roots";
  }

  std::size_t decompiled = 0;
  std::size_t fallbacks = 0;
  std::size_t unhandled = 0;
  std::vector<std::string> failures;
  for (const auto& file : files) {
    try {
      const auto pex = fo4::pex::PexFile::fromBytes(readBytes(file));
      const std::string source = fo4::papyrus::Decompiler::toSource(pex);
      const std::string listing = fo4::papyrus::Decompiler::disassemble(pex);
      if (source.rfind("ScriptName ", 0) != 0 || listing.rfind("; Disassembly of ", 0) != 0) {
        failures.push_back(file.string() + ": missing ScriptName or disassembly header");
        continue;
      }
      fallbacks += countOccurrences(source, "[decompile failed: ");
      unhandled += countOccurrences(source, "[unhandled ");
      ++decompiled;
    } catch (const std::exception& error) {
      failures.push_back(file.string() + ": " + error.what());
    }
  }

  std::cout << "[decompiler] decompiled " << decompiled << " of " << files.size() << " .pex files; "
            << fallbacks << " decompile-failed fallbacks, " << unhandled << " unhandled instructions\n";
  for (const auto& failure : failures) ADD_FAILURE() << failure;
  EXPECT_TRUE(failures.empty());
}
