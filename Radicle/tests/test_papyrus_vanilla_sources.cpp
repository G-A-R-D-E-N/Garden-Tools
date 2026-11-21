#include "papyrus_scratch_root.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <fo4/papyrus/script_index.hpp>
#include <fo4/papyrus/vanilla_script_sources.hpp>

namespace {

using Bytes = std::vector<std::uint8_t>;
using fo4::papyrus::test::ScratchRoot;

void put16(Bytes& out, std::uint16_t value) {
  out.push_back(static_cast<std::uint8_t>(value & 0xFFu));
  out.push_back(static_cast<std::uint8_t>(value >> 8));
}

void put32(Bytes& out, std::uint32_t value) {
  put16(out, static_cast<std::uint16_t>(value & 0xFFFFu));
  put16(out, static_cast<std::uint16_t>(value >> 16));
}

void putText(Bytes& out, const std::string& text) { out.insert(out.end(), text.begin(), text.end()); }

struct ZipEntry {
  std::string name;
  std::uint16_t method;
  Bytes data;
  std::uint32_t uncompressedSize;
};

Bytes buildZip(const std::vector<ZipEntry>& entries) {
  Bytes out;
  std::vector<std::uint32_t> offsets;
  for (const auto& entry : entries) {
    offsets.push_back(static_cast<std::uint32_t>(out.size()));
    put32(out, 0x04034b50u);
    put16(out, 20);
    put16(out, 0);
    put16(out, entry.method);
    put32(out, 0);
    put32(out, 0);
    put32(out, static_cast<std::uint32_t>(entry.data.size()));
    put32(out, entry.uncompressedSize);
    put16(out, static_cast<std::uint16_t>(entry.name.size()));
    put16(out, 0);
    putText(out, entry.name);
    out.insert(out.end(), entry.data.begin(), entry.data.end());
  }
  const auto centralStart = static_cast<std::uint32_t>(out.size());
  for (std::size_t i = 0; i < entries.size(); ++i) {
    const auto& entry = entries[i];
    put32(out, 0x02014b50u);
    put16(out, 20);
    put16(out, 20);
    put16(out, 0);
    put16(out, entry.method);
    put32(out, 0);
    put32(out, 0);
    put32(out, static_cast<std::uint32_t>(entry.data.size()));
    put32(out, entry.uncompressedSize);
    put16(out, static_cast<std::uint16_t>(entry.name.size()));
    put16(out, 0);
    put16(out, 0);
    put16(out, 0);
    put16(out, 0);
    put32(out, 0);
    put32(out, offsets[i]);
    putText(out, entry.name);
  }
  const auto centralSize = static_cast<std::uint32_t>(out.size()) - centralStart;
  put32(out, 0x06054b50u);
  put16(out, 0);
  put16(out, 0);
  put16(out, static_cast<std::uint16_t>(entries.size()));
  put16(out, static_cast<std::uint16_t>(entries.size()));
  put32(out, centralSize);
  put32(out, centralStart);
  put16(out, 0);
  return out;
}

std::filesystem::path writeBytes(const ScratchRoot& scratch, const std::string& relative, const Bytes& bytes) {
  const auto path = scratch.path() / relative;
  std::filesystem::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary);
  out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  return path;
}

Bytes storedZip(const std::string& entryName, const std::string& text) {
  return buildZip({{entryName, 0, Bytes(text.begin(), text.end()), static_cast<std::uint32_t>(text.size())}});
}

}

TEST(PapyrusVanillaSources, ReadsStoredAndDeflatedPscEntriesOnly) {
  ScratchRoot scratch("vanilla-zip");
  const Bytes deflatedA{0x4B, 0x04, 0x00};
  const Bytes stored{'S', 'c', 'r', 'i', 'p', 't'};
  const auto zip = buildZip({
      {"Fragments/QF_One.psc", 0, stored, static_cast<std::uint32_t>(stored.size())},
      {"two.psc", 8, deflatedA, 1},
      {"readme.txt", 0, stored, static_cast<std::uint32_t>(stored.size())},
      {"Folder/", 0, {}, 0},
  });
  const auto path = writeBytes(scratch, "stored-and-deflated.zip", zip);

  const auto sources = fo4::papyrus::readPscZip(path);

  ASSERT_EQ(sources.size(), 2u);
  EXPECT_EQ(sources[0].relativePath.generic_string(), "Fragments/QF_One.psc");
  EXPECT_EQ(sources[0].text, "Script");
  EXPECT_EQ(sources[1].relativePath.generic_string(), "two.psc");
  EXPECT_EQ(sources[1].text, "a");
}

TEST(PapyrusVanillaSources, RefusesUnsafeZipEntryNames) {
  ScratchRoot scratch("vanilla-unsafe");
  const Bytes body{'x'};
  for (const char* name : {"../escape.psc", "/abs.psc", "C:/drive.psc", "back\\slash.psc", "a/./b.psc"}) {
    const auto path = writeBytes(scratch, "unsafe.zip", buildZip({{name, 0, body, 1}}));
    EXPECT_THROW(fo4::papyrus::readPscZip(path), std::runtime_error) << name;
  }
}

TEST(PapyrusVanillaSources, RefusesTruncatedAndMismatchedZips) {
  ScratchRoot scratch("vanilla-truncated");
  const Bytes body{'x'};
  auto zip = buildZip({{"a.psc", 0, body, 1}});
  zip.resize(zip.size() / 2);
  EXPECT_THROW(fo4::papyrus::readPscZip(writeBytes(scratch, "truncated.zip", zip)), std::runtime_error);

  const auto sizeMismatch = buildZip({{"a.psc", 0, body, 2}});
  EXPECT_THROW(fo4::papyrus::readPscZip(writeBytes(scratch, "mismatch.zip", sizeMismatch)),
               std::runtime_error);
}

TEST(PapyrusVanillaSources, DirectoryReadIsRelativeAndSorted) {
  ScratchRoot scratch("vanilla-dir");
  scratch.write("Zeta.psc", "Scriptname Zeta\n");
  scratch.write("Sub/Alpha.psc", "Scriptname Alpha\n");
  scratch.write("notes.txt", "ignored\n");

  const auto sources = fo4::papyrus::readPscDirectory(scratch.path());

  ASSERT_EQ(sources.size(), 2u);
  EXPECT_EQ(sources[0].relativePath.generic_string(), "Sub/Alpha.psc");
  EXPECT_EQ(sources[1].relativePath.generic_string(), "Zeta.psc");
  EXPECT_TRUE(fo4::papyrus::readPscDirectory(scratch.path() / "Missing").empty());
}

TEST(PapyrusVanillaSources, ImportRootPrefersExtractedSourcesOverTheZip) {
  ScratchRoot data("vanilla-root");
  const auto folder = std::filesystem::path("Scripts") / "Source" / "Base";
  writeBytes(data, (folder / "Base.zip").string(), storedZip("FromZip.psc", "Scriptname FromZip\n"));
  EXPECT_TRUE(fo4::papyrus::loadVanillaImportRoot(data.path(), "Base").has_value());
  EXPECT_EQ(fo4::papyrus::loadVanillaImportRoot(data.path(), "Base")->sources.front().relativePath,
            std::filesystem::path("FromZip.psc"));

  data.write((folder / "Extracted.psc").string(), "Scriptname Extracted\n");
  const auto root = fo4::papyrus::loadVanillaImportRoot(data.path(), "Base");
  ASSERT_TRUE(root.has_value());
  ASSERT_EQ(root->sources.size(), 1u);
  EXPECT_EQ(root->sources[0].relativePath, std::filesystem::path("Extracted.psc"));
  EXPECT_EQ(root->root, data.path() / folder);
}

TEST(PapyrusVanillaSources, ImportRootRefusesMoreThanOneZipAndBadFolderNames) {
  ScratchRoot data("vanilla-zips");
  const auto folder = std::filesystem::path("Scripts") / "Source" / "DLC01";
  writeBytes(data, (folder / "A.zip").string(), storedZip("A.psc", "Scriptname A\n"));
  writeBytes(data, (folder / "B.zip").string(), storedZip("B.psc", "Scriptname B\n"));
  EXPECT_THROW(fo4::papyrus::loadVanillaImportRoot(data.path(), "DLC01"), std::runtime_error);
  EXPECT_THROW(fo4::papyrus::loadVanillaImportRoot(data.path(), "../Base"), std::runtime_error);
  EXPECT_THROW(fo4::papyrus::loadVanillaImportRoot(data.path(), ""), std::runtime_error);
  EXPECT_FALSE(fo4::papyrus::loadVanillaImportRoot(data.path(), "DLC02").has_value());
}

TEST(PapyrusVanillaSources, AddVanillaImportsIndexesExtractedAndZippedFoldersAsSnapshots) {
  ScratchRoot data("vanilla-imports");
  const auto source = std::filesystem::path("Scripts") / "Source";
  data.write((source / "Base" / "Quest.psc").string(), "Scriptname Quest\n");
  writeBytes(data, (source / "DLC01" / "Dlc.zip").string(),
             storedZip("DLC01/Fragments/QF_Dlc.psc", "Scriptname DLC01:Fragments:QF_Dlc Extends Quest\n"));

  fo4::papyrus::ScriptIndex index;
  const auto added = fo4::papyrus::addVanillaImports(index, data.path(), fo4::papyrus::kVanillaImportFolders);

  EXPECT_EQ(added, 2u);
  EXPECT_TRUE(index.snapshotOnly());
  EXPECT_NE(index.resolve("Quest"), nullptr);
  EXPECT_NE(index.resolve("QF_Dlc"), nullptr);
  const auto* dlc = index.resolve("DLC01:Fragments:QF_Dlc");
  ASSERT_NE(dlc, nullptr);
  const auto chain = index.baseChain(dlc);
  ASSERT_EQ(chain.size(), 2u);
  EXPECT_EQ(chain[1], index.resolve("Quest"));
}

TEST(PapyrusVanillaSources, AddVanillaImportsWithNoSourceFoldersAddsNothing) {
  ScratchRoot data("vanilla-empty");
  fo4::papyrus::ScriptIndex index;
  EXPECT_EQ(fo4::papyrus::addVanillaImports(index, data.path(), fo4::papyrus::kVanillaImportFolders), 0u);
  EXPECT_FALSE(index.snapshotOnly());
  EXPECT_EQ(index.count(), 0u);
}
