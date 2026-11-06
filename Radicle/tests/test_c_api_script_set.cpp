#include "fo4/c_api_env.h"
#include "fo4/c_api_script_set.h"

#include "fo4/archive/ba2.hpp"
#include "fo4/archive/ba2_hash.hpp"
#include "fo4/pex/pex.hpp"
#include "fo4/pex_analysis/opcodes.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using fo4::archive::Ba2GeneralArchive;
using fo4::archive::Ba2GeneralEntry;

std::vector<std::byte> bytes_of(std::string_view text) {
  std::vector<std::byte> out;
  for (const char ch : text) out.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  return out;
}

Ba2GeneralEntry general(std::string path, std::string_view payload) {
  Ba2GeneralEntry entry;
  const auto hashes = fo4::archive::compute_ba2_hashes(path);
  entry.name_hash = hashes.name;
  entry.extension_hash = hashes.extension;
  entry.directory_hash = hashes.directory;
  entry.path = std::move(path);
  entry.chunks.push_back(fo4::archive::make_stored_ba2_chunk(bytes_of(payload)));
  return entry;
}

class CApiScriptSetTest : public ::testing::Test {
protected:
  void SetUp() override {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    root_ = std::filesystem::temp_directory_path() / ("radicle_c_api_script_set_" + std::to_string(id));
    std::filesystem::create_directories(root_);
  }

  void TearDown() override {
    std::error_code ec;
    std::filesystem::remove_all(root_, ec);
  }

  std::filesystem::path write_loose(const std::filesystem::path& dir, std::string_view relative,
                                    std::string_view payload) {
    const auto path = dir / std::filesystem::path(std::string(relative));
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(payload.data(), static_cast<std::streamsize>(payload.size()));
    return path;
  }

  std::filesystem::path write_archive(std::string_view name, std::vector<Ba2GeneralEntry> entries) {
    Ba2GeneralArchive archive;
    archive.has_string_table = true;
    archive.entries = std::move(entries);
    const auto path = root_ / std::string(name);
    fo4::archive::write_ba2_general_file(archive, path);
    return path;
  }

  radicle_assets* open(const std::vector<std::string>& roots, const std::vector<std::string>& archives) {
    std::vector<const char*> root_paths;
    for (const auto& root : roots) root_paths.push_back(root.c_str());
    std::vector<const char*> archive_paths;
    for (const auto& archive : archives) archive_paths.push_back(archive.c_str());
    radicle_assets* assets = nullptr;
    EXPECT_EQ(radicle_assets_open_roots(root_paths.data(), root_paths.size(), archive_paths.data(),
                                        archive_paths.size(), &assets),
              RADICLE_STATUS_OK);
    return assets;
  }

  radicle_script_set* open_set(const std::vector<std::string>& roots, const std::vector<std::string>& archives,
                               radicle_archive_policy policy = RADICLE_ARCHIVE_POLICY_FAIL) {
    std::vector<const char*> root_paths;
    for (const auto& root : roots) root_paths.push_back(root.c_str());
    std::vector<const char*> archive_paths;
    for (const auto& archive : archives) archive_paths.push_back(archive.c_str());
    radicle_script_set* set = nullptr;
    EXPECT_EQ(radicle_script_set_open(root_paths.data(), root_paths.size(), archive_paths.data(),
                                      archive_paths.size(), policy, &set),
              RADICLE_STATUS_OK);
    return set;
  }

  int32_t chain_valid(radicle_script_set* set, const char* name) {
    int32_t valid = -1;
    EXPECT_EQ(radicle_script_set_parent_chain_valid(set, name, &valid), RADICLE_STATUS_OK);
    return valid;
  }

  std::filesystem::path root_;
};

using fo4::pex::PexFile;
using fo4::pex::PexFunction;
using fo4::pex::PexInstruction;
using fo4::pex::PexObject;
using fo4::pex::PexState;
using fo4::pex::PexValue;
using fo4::pex::PexValueType;
using fo4::pex_analysis::PexOp;

PexValue ident(std::string name) {
  PexValue value;
  value.type = PexValueType::Identifier;
  value.str = std::move(name);
  return value;
}

PexInstruction call_of(PexOp op, std::vector<std::string> names) {
  PexInstruction instruction;
  instruction.opCode = static_cast<std::uint8_t>(op);
  for (auto& name : names) instruction.args.push_back(ident(std::move(name)));
  return instruction;
}

PexFunction function_of(std::string name, bool native, bool global, std::vector<PexInstruction> body = {}) {
  PexFunction function;
  function.name = std::move(name);
  function.isNative = native;
  function.isGlobal = global;
  function.instructions = std::move(body);
  return function;
}

std::string pex_text(std::string object_name, std::string parent, std::vector<PexFunction> functions = {}) {
  PexFile pex;
  pex.majorVersion = 3;
  pex.minorVersion = 9;
  pex.gameId = 2;
  PexObject object;
  object.name = std::move(object_name);
  object.parentClassName = std::move(parent);
  PexState state;
  state.functions = std::move(functions);
  object.states.push_back(std::move(state));
  pex.objects.push_back(std::move(object));
  pex.rebuildStringTable();
  const auto bytes = pex.toBytes();
  return std::string(bytes.begin(), bytes.end());
}

std::string c_strings_text(const char* text) { return text == nullptr ? std::string("<null>") : std::string(text); }

std::string text_of(uint8_t* bytes, size_t size) {
  return std::string(reinterpret_cast<const char*>(bytes), size);
}

// A pointer that a rejected call must overwrite; it is never dereferenced.
template <typename T>
T* sentinel() {
  return reinterpret_cast<T*>(static_cast<std::uintptr_t>(1));
}

}  // namespace

TEST_F(CApiScriptSetTest, ResolvesReadsAndCountsReplacedPayloads) {
  const auto data = root_ / "Data";
  const auto loose_path = write_loose(data, "Scripts/Actors/Foo.pex", "loose");
  const auto archive = write_archive("Mod - Main.ba2", {general("Scripts\\Actors\\Foo.pex", "archived"),
                                                        general("Scripts\\Bar.pex", "bar")});
  radicle_assets* assets = open({data.string()}, {archive.string()});
  ASSERT_NE(assets, nullptr);
  EXPECT_EQ(radicle_assets_root_count(assets), 1u);

  radicle_script_set* set = nullptr;
  ASSERT_EQ(radicle_script_set_resolve(assets, nullptr, 0, &set), RADICLE_STATUS_OK);
  ASSERT_NE(set, nullptr);
  ASSERT_EQ(radicle_script_set_count(set), 2u);

  radicle_script_set_winner winner{};
  ASSERT_EQ(radicle_script_set_get(set, 0, &winner), RADICLE_STATUS_OK);
  EXPECT_EQ(std::string(winner.name), "actors/foo");
  EXPECT_EQ(std::string(winner.winner.name), "actors/foo");
  EXPECT_EQ(std::string(winner.winner.path), "scripts/actors/foo.pex");
  EXPECT_EQ(winner.winner.origin, RADICLE_ASSET_LOOSE);
  EXPECT_TRUE(std::filesystem::equivalent(std::filesystem::path(winner.winner.container), loose_path));
  EXPECT_EQ(winner.winner.size, 5u);
  EXPECT_EQ(winner.replaced, 1u);

  uint8_t* bytes = nullptr;
  size_t size = 0;
  ASSERT_EQ(radicle_script_set_read(set, 0, &bytes, &size), RADICLE_STATUS_OK);
  EXPECT_EQ(text_of(bytes, size), "loose");
  radicle_free_bytes(bytes);

  ASSERT_EQ(radicle_script_set_get(set, 1, &winner), RADICLE_STATUS_OK);
  EXPECT_EQ(std::string(winner.name), "bar");
  EXPECT_EQ(winner.winner.origin, RADICLE_ASSET_ARCHIVE);
  EXPECT_EQ(winner.replaced, 0u);
  ASSERT_EQ(radicle_script_set_read(set, 1, &bytes, &size), RADICLE_STATUS_OK);
  EXPECT_EQ(text_of(bytes, size), "bar");
  radicle_free_bytes(bytes);

  radicle_script_set_destroy(set);
  radicle_assets_destroy(assets);
}

TEST_F(CApiScriptSetTest, ExplicitLoadIndexMovesTheWinner) {
  const auto high = root_ / "ModHigh";
  const auto low = root_ / "ModLow";
  const auto high_path = write_loose(high, "Scripts/foo.pex", "high");
  const auto low_path = write_loose(low, "Scripts/foo.pex", "low");
  radicle_assets* assets = open({high.string(), low.string()}, {});
  ASSERT_NE(assets, nullptr);
  ASSERT_EQ(radicle_assets_root_count(assets), 2u);

  radicle_script_set* set = nullptr;
  ASSERT_EQ(radicle_script_set_resolve(assets, nullptr, 0, &set), RADICLE_STATUS_OK);
  radicle_script_set_winner winner{};
  ASSERT_EQ(radicle_script_set_get(set, 0, &winner), RADICLE_STATUS_OK);
  EXPECT_TRUE(std::filesystem::equivalent(std::filesystem::path(winner.winner.container), high_path));
  radicle_script_set_destroy(set);

  const int32_t indexes[] = {2, 7};
  ASSERT_EQ(radicle_script_set_resolve(assets, indexes, 2, &set), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_script_set_count(set), 1u);
  ASSERT_EQ(radicle_script_set_get(set, 0, &winner), RADICLE_STATUS_OK);
  EXPECT_TRUE(std::filesystem::equivalent(std::filesystem::path(winner.winner.container), low_path));
  EXPECT_EQ(winner.winner.load_index, 7);
  EXPECT_EQ(winner.replaced, 1u);

  uint8_t* bytes = nullptr;
  size_t size = 0;
  ASSERT_EQ(radicle_script_set_read(set, 0, &bytes, &size), RADICLE_STATUS_OK);
  EXPECT_EQ(text_of(bytes, size), "low");
  radicle_free_bytes(bytes);

  radicle_script_set_destroy(set);
  radicle_assets_destroy(assets);
}

TEST_F(CApiScriptSetTest, RejectsBadArguments) {
  const auto data = root_ / "Data";
  write_loose(data, "Scripts/foo.pex", "foo");
  radicle_assets* assets = open({data.string()}, {});
  ASSERT_NE(assets, nullptr);

  // Invariant: every out-param keeps a sentinel until the call proves the arguments.
  // Invariant: a rejected call must clear the caller's previous handle so the sentinel stays visible here.
  radicle_script_set* out_set = sentinel<radicle_script_set>();
  EXPECT_EQ(radicle_script_set_resolve(nullptr, nullptr, 0, &out_set), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(out_set, nullptr);
  out_set = sentinel<radicle_script_set>();
  EXPECT_EQ(radicle_script_set_resolve(assets, nullptr, 2, &out_set), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(out_set, nullptr);
  // Invariant: the assets handle has one root, so two indexes must be refused.
  const int32_t two[] = {1, 2};
  out_set = sentinel<radicle_script_set>();
  EXPECT_EQ(radicle_script_set_resolve(assets, two, 2, &out_set), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(out_set, nullptr);
  EXPECT_EQ(radicle_script_set_resolve(assets, nullptr, 0, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);

  radicle_script_set* set = nullptr;
  ASSERT_EQ(radicle_script_set_resolve(assets, nullptr, 0, &set), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_script_set_count(set), 1u);
  radicle_script_set_winner winner{};
  winner.name = "stale";
  winner.winner.load_index = 9;
  winner.replaced = 7u;
  EXPECT_EQ(radicle_script_set_get(set, 1, &winner), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(winner.name, static_cast<const char*>(nullptr));
  EXPECT_EQ(winner.winner.load_index, 0);
  EXPECT_EQ(winner.replaced, 0u);
  EXPECT_EQ(radicle_script_set_get(set, 0, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  uint8_t* bytes = sentinel<uint8_t>();
  size_t size = 99u;
  EXPECT_EQ(radicle_script_set_read(set, 1, &bytes, &size), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(bytes, nullptr);
  EXPECT_EQ(size, 0u);
  bytes = sentinel<uint8_t>();
  size = 99u;
  EXPECT_EQ(radicle_script_set_read(nullptr, 0, &bytes, &size), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(bytes, nullptr);
  EXPECT_EQ(size, 0u);
  winner.name = "stale";
  winner.winner.load_index = 9;
  winner.replaced = 7u;
  EXPECT_EQ(radicle_script_set_get(nullptr, 0, &winner), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(winner.name, static_cast<const char*>(nullptr));
  EXPECT_EQ(winner.winner.load_index, 0);
  EXPECT_EQ(winner.replaced, 0u);
  EXPECT_EQ(radicle_script_set_count(nullptr), 0u);
  radicle_script_set_destroy(nullptr);
  radicle_script_set_destroy(set);

  radicle_script_set* empty = nullptr;
  radicle_assets* none = open({}, {});
  ASSERT_NE(none, nullptr);
  ASSERT_EQ(radicle_script_set_resolve(none, nullptr, 0, &empty), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_script_set_count(empty), 0u);
  radicle_script_set_destroy(empty);
  radicle_assets_destroy(none);
  radicle_assets_destroy(assets);
}

TEST_F(CApiScriptSetTest, OpenOwnsItsAssetsAndResolvesLooseOverArchive) {
  const auto data = root_ / "Data";
  const auto loose_path = write_loose(data, "Scripts/Actors/Foo.pex", "loose");
  const auto archive = write_archive("Mod - Main.ba2", {general("Scripts\\Actors\\Foo.pex", "archived"),
                                                        general("Scripts\\Bar.pex", "bar")});
  radicle_script_set* set = open_set({data.string()}, {archive.string()});
  ASSERT_NE(set, nullptr);
  ASSERT_EQ(radicle_script_set_count(set), 2u);
  EXPECT_EQ(radicle_script_set_failure_count(set), 0u);

  radicle_script_set_winner winner{};
  ASSERT_EQ(radicle_script_set_get(set, 0, &winner), RADICLE_STATUS_OK);
  EXPECT_EQ(std::string(winner.name), "actors/foo");
  EXPECT_EQ(winner.winner.origin, RADICLE_ASSET_LOOSE);
  EXPECT_TRUE(std::filesystem::equivalent(std::filesystem::path(winner.winner.container), loose_path));
  EXPECT_EQ(winner.replaced, 1u);
  uint8_t* bytes = nullptr;
  size_t size = 0;
  ASSERT_EQ(radicle_script_set_read(set, 1, &bytes, &size), RADICLE_STATUS_OK);
  EXPECT_EQ(text_of(bytes, size), "bar");
  radicle_free_bytes(bytes);
  radicle_script_set_destroy(set);
}

TEST_F(CApiScriptSetTest, ArchivePolicyFailAbortsAndSkipReportsTheUnreadableArchive) {
  const auto data = root_ / "Data";
  const auto first = write_archive("A - Main.ba2", {general("Scripts\\One.pex", "one")});
  const auto broken = write_loose(root_, "B - Broken.ba2", "this is not a ba2");
  const auto last = write_archive("C - Main.ba2", {general("Scripts\\Two.pex", "two")});
  const std::vector<std::string> archives{first.string(), broken.string(), last.string()};
  std::vector<const char*> archive_paths;
  for (const auto& archive : archives) archive_paths.push_back(archive.c_str());
  const char* root_path = nullptr;

  radicle_script_set* set = sentinel<radicle_script_set>();
  EXPECT_NE(radicle_script_set_open(&root_path, 0, archive_paths.data(), archive_paths.size(),
                                    RADICLE_ARCHIVE_POLICY_FAIL, &set),
            RADICLE_STATUS_OK);
  EXPECT_EQ(set, nullptr);

  set = open_set({data.string()}, archives, RADICLE_ARCHIVE_POLICY_SKIP);
  ASSERT_NE(set, nullptr);
  ASSERT_EQ(radicle_script_set_count(set), 2u);
  ASSERT_EQ(radicle_script_set_failure_count(set), 1u);
  radicle_script_set_failure_info failure{};
  ASSERT_EQ(radicle_script_set_failure(set, 0, &failure), RADICLE_STATUS_OK);
  EXPECT_EQ(failure.archive_index, 1u);
  EXPECT_EQ(std::filesystem::path(failure.path), broken);
  EXPECT_FALSE(c_strings_text(failure.message).empty());
  EXPECT_EQ(radicle_script_set_failure(set, 1, &failure), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(failure.path, static_cast<const char*>(nullptr));

  radicle_script_set_winner winner{};
  ASSERT_EQ(radicle_script_set_get(set, 1, &winner), RADICLE_STATUS_OK);
  EXPECT_EQ(std::string(winner.name), "two");
  uint8_t* bytes = nullptr;
  size_t size = 0;
  ASSERT_EQ(radicle_script_set_read(set, 1, &bytes, &size), RADICLE_STATUS_OK);
  EXPECT_EQ(text_of(bytes, size), "two");
  radicle_free_bytes(bytes);
  radicle_script_set_destroy(set);

  radicle_script_set* rejected = sentinel<radicle_script_set>();
  EXPECT_EQ(radicle_script_set_open(&root_path, 0, archive_paths.data(), archive_paths.size(),
                                    static_cast<radicle_archive_policy>(7), &rejected),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(rejected, nullptr);
}

TEST_F(CApiScriptSetTest, OpenDropsDuplicateRoots) {
  const auto data = root_ / "Data";
  write_loose(data, "Scripts/Foo.pex", "loose");
  radicle_script_set* set = open_set({data.string(), data.string()}, {});
  ASSERT_NE(set, nullptr);
  ASSERT_EQ(radicle_script_set_count(set), 1u);
  radicle_script_set_winner winner{};
  ASSERT_EQ(radicle_script_set_get(set, 0, &winner), RADICLE_STATUS_OK);
  EXPECT_EQ(winner.replaced, 0u);
  radicle_script_set_destroy(set);
}

TEST_F(CApiScriptSetTest, ParentChainValidityMatchesTheManagedWalk) {
  const auto data = root_ / "Data";
  write_loose(data, "Scripts/Base.pex", pex_text("Base", ""));
  write_loose(data, "Scripts/Child.pex", pex_text("Child", "Base"));
  write_loose(data, "Scripts/Ns/Inner.pex", pex_text("Ns:Inner", "child"));
  write_loose(data, "Scripts/Orphan.pex", pex_text("Orphan", "Nope"));
  write_loose(data, "Scripts/CycA.pex", pex_text("CycA", "CycB"));
  write_loose(data, "Scripts/CycB.pex", pex_text("CycB", "CycA"));
  write_loose(data, "Scripts/SelfLoop.pex", pex_text("SelfLoop", "selfloop"));
  write_loose(data, "Scripts/Junk.pex", "not a pex");
  write_loose(data, "Scripts/OnJunk.pex", pex_text("OnJunk", "Junk"));
  constexpr int kDepth = 300;
  for (int i = 0; i < kDepth; ++i) {
    const std::string name = "Deep" + std::to_string(i);
    write_loose(data, "Scripts/" + name + ".pex",
                pex_text(name, i + 1 < kDepth ? "Deep" + std::to_string(i + 1) : ""));
  }
  radicle_script_set* set = open_set({data.string()}, {});
  ASSERT_NE(set, nullptr);

  EXPECT_EQ(chain_valid(set, "Base"), 1);
  EXPECT_EQ(chain_valid(set, "cHiLd"), 1);
  EXPECT_EQ(chain_valid(set, "NS:inner"), 1);
  EXPECT_EQ(chain_valid(set, "Deep0"), 1);
  EXPECT_EQ(chain_valid(set, "Orphan"), 0);
  EXPECT_EQ(chain_valid(set, "CycA"), 0);
  EXPECT_EQ(chain_valid(set, "SelfLoop"), 0);
  EXPECT_EQ(chain_valid(set, "Junk"), 0);
  EXPECT_EQ(chain_valid(set, "OnJunk"), 0);
  EXPECT_EQ(chain_valid(set, "Missing"), 0);

  int32_t valid = 5;
  EXPECT_EQ(radicle_script_set_parent_chain_valid(nullptr, "Base", &valid), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(valid, 0);
  EXPECT_EQ(radicle_script_set_parent_chain_valid(set, nullptr, &valid), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_script_set_parent_chain_valid(set, "Base", nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  radicle_script_set_destroy(set);
}

TEST_F(CApiScriptSetTest, CensusFindsNativeCallSitesThroughTheParentChain) {
  const auto data = root_ / "Data";
  write_loose(data, "Scripts/Base.pex", pex_text("Base", "", {function_of("Native", true, false)}));
  write_loose(data, "Scripts/Util.pex", pex_text("Util", "", {function_of("Ping", true, true)}));
  write_loose(data, "Scripts/Child.pex",
              pex_text("Child", "Base",
                       {function_of("Helper", false, false),
                        function_of("Run", false, false,
                                    {call_of(PexOp::CallMethod, {"Native", "self", "::none"}),
                                     call_of(PexOp::CallStatic, {"Util", "Ping", "::none"}),
                                     call_of(PexOp::CallMethod, {"Missing", "self", "::none"}),
                                     call_of(PexOp::CallMethod, {"Helper", "self", "::none"})})}));
  write_loose(data, "Scripts/Broken.pex", "not a pex");
  radicle_script_set* set = open_set({data.string()}, {});
  ASSERT_NE(set, nullptr);

  radicle_script_census* census = nullptr;
  ASSERT_EQ(radicle_script_set_census(set, &census), RADICLE_STATUS_OK);
  ASSERT_NE(census, nullptr);
  radicle_script_census_totals totals{};
  ASSERT_EQ(radicle_script_census_totals_get(census, &totals), RADICLE_STATUS_OK);
  EXPECT_EQ(totals.object_count, 3u);
  EXPECT_EQ(totals.unreadable_count, 1u);
  EXPECT_EQ(totals.site_count, 2u);
  EXPECT_EQ(totals.script_call_count, 1u);
  EXPECT_EQ(totals.unresolved_count, 1u);

  radicle_script_census_site site{};
  ASSERT_EQ(radicle_script_census_site_get(census, 0, &site), RADICLE_STATUS_OK);
  EXPECT_EQ(std::string(site.script), "Child");
  EXPECT_EQ(std::string(site.function), "Run");
  EXPECT_EQ(site.instruction, 0u);
  EXPECT_EQ(site.is_static, 0);
  EXPECT_EQ(std::string(site.target), "Child");
  EXPECT_EQ(std::string(site.called_function), "Native");
  ASSERT_EQ(radicle_script_census_site_get(census, 1, &site), RADICLE_STATUS_OK);
  EXPECT_EQ(site.instruction, 1u);
  EXPECT_EQ(site.is_static, 1);
  EXPECT_EQ(std::string(site.target), "Util");
  EXPECT_EQ(std::string(site.called_function), "Ping");
  EXPECT_EQ(radicle_script_census_site_get(census, 2, &site), RADICLE_STATUS_INVALID_ARGUMENT);

  const char* text = nullptr;
  ASSERT_EQ(radicle_script_census_unresolved(census, 0, &text), RADICLE_STATUS_OK);
  EXPECT_NE(c_strings_text(text).find("Missing"), std::string::npos);
  ASSERT_EQ(radicle_script_census_unreadable(census, 0, &text), RADICLE_STATUS_OK);
  EXPECT_EQ(c_strings_text(text), "broken");
  EXPECT_EQ(radicle_script_census_unresolved(census, 1, &text), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_script_census_totals_get(nullptr, &totals), RADICLE_STATUS_INVALID_ARGUMENT);

  radicle_script_census_destroy(census);
  radicle_script_census_destroy(nullptr);
  radicle_script_set_destroy(set);
}
