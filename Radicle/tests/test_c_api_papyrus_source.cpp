#include <fo4/c_api.h>
#include <fo4/c_api_papyrus_source.h>
#include <fo4/c_api_papyrus_compiler.h>
#include <fo4/c_api_pex.h>
#include <fo4/papyrus/compile_script.hpp>
#include <fo4/papyrus/parser.hpp>
#include <fo4/papyrus/script_index.hpp>
#include <fo4/pex/pex.hpp>

#include <gtest/gtest.h>

#include "papyrus_pex_comparer.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace {

radicle_status summarize_source(const std::string& source, std::vector<std::string>& rows) {
  char** raw = nullptr;
  size_t count = 0;
  const radicle_status status = radicle_papyrus_source_summary(
      reinterpret_cast<const uint8_t*>(source.data()), source.size(), &raw, &count);
  rows.clear();
  for (size_t index = 0; index < count; ++index) rows.emplace_back(raw[index]);
  radicle_free_string_array(raw, count);
  return status;
}

std::filesystem::path base_stubs() { return std::filesystem::path(FO4_TESTDATA_DIR) / "graph" / "BaseStubs"; }

const std::string kProbe =
    "Scriptname Probe\n"
    "Int Property Count Auto\n"
    "Function Go(int amount)\n"
    "    int total = Count + amount\n"
    "    If total > 3\n"
    "        Count = total\n"
    "    EndIf\n"
    "EndFunction\n";

const std::string kTrace =
    "Scriptname Probe\n"
    "Function Go()\n"
    "    Debug.Trace(\"probe\")\n"
    "EndFunction\n";

radicle_status compile_source(const std::string& source, const std::string& imports, radicle_pex** pex,
                              std::vector<std::string>& rows, int32_t release = 0, int32_t no_debug_info = 0) {
  char** raw = nullptr;
  size_t count = 0;
  const radicle_papyrus_compile_options options{release, no_debug_info, imports.c_str()};
  const radicle_status status = radicle_papyrus_compile_source(
      reinterpret_cast<const uint8_t*>(source.data()), source.size(), "Probe.psc", options, pex, &raw, &count);
  rows.clear();
  for (size_t index = 0; index < count; ++index) rows.emplace_back(raw[index]);
  radicle_free_string_array(raw, count);
  return status;
}

radicle_status decompile_text(const radicle_pex* pex, int32_t assembly, std::string& text) {
  uint8_t* raw = nullptr;
  size_t size = 0;
  const radicle_status status = radicle_papyrus_decompile(pex, assembly, &raw, &size);
  text = raw ? std::string(reinterpret_cast<const char*>(raw), size) : std::string();
  radicle_free_bytes(raw);
  return status;
}

std::vector<uint8_t> write_pex(const radicle_pex* pex) {
  uint8_t* raw = nullptr;
  size_t size = 0;
  EXPECT_EQ(radicle_pex_write_bytes(pex, &raw, &size), RADICLE_STATUS_OK);
  std::vector<uint8_t> bytes(raw, raw + size);
  radicle_free_bytes(raw);
  return bytes;
}

std::vector<uint8_t> compiled_bytes(const std::string& source, const std::string& imports, int32_t release,
                                    int32_t no_debug_info) {
  radicle_pex* pex = nullptr;
  std::vector<std::string> rows;
  EXPECT_EQ(compile_source(source, imports, &pex, rows, release, no_debug_info), RADICLE_STATUS_OK);
  EXPECT_NE(pex, nullptr);
  if (!pex) return {};
  auto bytes = write_pex(pex);
  radicle_pex_destroy(pex);
  return bytes;
}

std::vector<uint8_t> reference_bytes(const std::string& source, bool release, bool debug_info) {
  fo4::papyrus::ScriptIndex index;
  index.addRoot(base_stubs());
  const auto parsed = fo4::papyrus::Parser::parse(source, "Probe.psc");
  fo4::papyrus::PapyrusCompileOptions options;
  options.emitDebugInfo = debug_info;
  options.emitDebugOnlyCode = !release;
  options.emitBetaOnlyCode = !release;
  const auto result = fo4::papyrus::compileScript(index, &parsed, "Probe.psc", options);
  EXPECT_NE(result.pex, nullptr);
  if (!result.pex) return {};
  return result.pex->toBytes();
}

}  // namespace

TEST(CApiPapyrusSourceTest, ReportsTheFieldsAnEditorNeeds) {
  const std::string source =
      "Scriptname Summary:Probe extends ObjectReference\n"
      "Import Utility\n"
      "Int Property Count Auto\n"
      "Int Property Total AutoReadOnly\n"
      "Event Actor.OnPlayerLoadGame(Actor akSender)\n"
      "EndEvent\n"
      "Function Helper()\n"
      "EndFunction\n"
      "State Busy\n"
      "  Function Tick()\n"
      "  EndFunction\n"
      "EndState\n";
  std::vector<std::string> rows;
  ASSERT_EQ(summarize_source(source, rows), RADICLE_STATUS_OK);
  const std::vector<std::string> expected = {
      "script\tSummary:Probe\tObjectReference\t",
      "import\tUtility",
      "property\tCount\t1",
      "property\tTotal\t1",
      "state\tBusy",
      "function\tActor.OnPlayerLoadGame\t\t0\t1",
      "function\tHelper\t\t0\t0",
      "function\tTick\tBusy\t0\t0",
  };
  EXPECT_EQ(rows, expected);
}

TEST(CApiPapyrusSourceTest, ReportsAParseFailureAsARow) {
  std::vector<std::string> rows;
  ASSERT_EQ(summarize_source("Scriptname B\nInt a = 1 # 2\n", rows), RADICLE_STATUS_OK);
  ASSERT_EQ(rows.size(), 1u);
  EXPECT_EQ(rows[0], "error\t2\t11\tunexpected character '#'");

  /* Must keep a message that quotes a decoded tab on one row with three separators. */
  std::vector<std::string> quoted;
  ASSERT_EQ(summarize_source("Scriptname B\nState \"a\tb\"\nEndState\n", quoted), RADICLE_STATUS_OK);
  ASSERT_EQ(quoted.size(), 1u);
  EXPECT_EQ(std::count(quoted[0].begin(), quoted[0].end(), '\t'), 3);
  EXPECT_EQ(quoted[0].find('\n'), std::string::npos);
  EXPECT_EQ(quoted[0].find('\r'), std::string::npos);
  EXPECT_NE(quoted[0].find("expected identifier but found \"a b\""), std::string::npos);
}

TEST(CApiPapyrusSourceTest, RefusesMissingEmptyAndOversizedSources) {
  char** raw = reinterpret_cast<char**>(1);
  size_t count = 9;
  EXPECT_EQ(radicle_papyrus_source_summary(nullptr, 4, &raw, &count), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_STREQ(radicle_last_error(), "source is required");
  EXPECT_EQ(raw, nullptr);
  EXPECT_EQ(count, 0u);

  const std::string empty;
  EXPECT_EQ(radicle_papyrus_source_summary(reinterpret_cast<const uint8_t*>(empty.data()), 0, &raw, &count),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_STREQ(radicle_last_error(), "source must not be empty");
  EXPECT_EQ(raw, nullptr);
  EXPECT_EQ(count, 0u);

  const std::string one = "Scriptname B\n";
  EXPECT_EQ(radicle_papyrus_source_summary(reinterpret_cast<const uint8_t*>(one.data()),
                                           RADICLE_PAPYRUS_SOURCE_MAX_BYTES + 1, &raw, &count),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_STREQ(radicle_last_error(), "source size 4194305 is above the 4194304-byte cap");
  EXPECT_EQ(raw, nullptr);
  EXPECT_EQ(count, 0u);

  EXPECT_EQ(radicle_papyrus_source_summary(reinterpret_cast<const uint8_t*>(one.data()), one.size(), nullptr, &count),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_STREQ(radicle_last_error(), "out_rows and out_count are required");
}

TEST(CApiPapyrusCompileTest, CompileSourceMatchesCompileScriptForTheSameInput) {
  EXPECT_EQ(compiled_bytes(kTrace, base_stubs().string(), 0, 0), reference_bytes(kTrace, false, true));
}

TEST(CApiPapyrusCompileTest, ImportRootsDecideWhetherAnImportedScriptResolves) {
  radicle_pex* pex = nullptr;
  std::vector<std::string> rows;
  ASSERT_EQ(compile_source(kTrace, "", &pex, rows), RADICLE_STATUS_OK);
  EXPECT_EQ(pex, nullptr);
  ASSERT_FALSE(rows.empty());
  EXPECT_EQ(rows.front().substr(0, 6), "error\t");
}

TEST(CApiPapyrusCompileTest, DebugAndReleaseFlagsReachTheCodeGenerator) {
  const auto without_debug = compiled_bytes(kProbe, base_stubs().string(), 0, 1);
  EXPECT_EQ(without_debug, reference_bytes(kProbe, false, false));
  const auto with_debug = compiled_bytes(kProbe, base_stubs().string(), 0, 0);
  EXPECT_EQ(with_debug, reference_bytes(kProbe, false, true));
  EXPECT_NE(without_debug, with_debug);
  EXPECT_EQ(compiled_bytes(kProbe, base_stubs().string(), 1, 0), reference_bytes(kProbe, true, true));
}

TEST(CApiPapyrusCompileTest, DecompiledSourceRecompilesToTheSameFunctions) {
  radicle_pex* first = nullptr;
  std::vector<std::string> rows;
  ASSERT_EQ(compile_source(kProbe, base_stubs().string(), &first, rows), RADICLE_STATUS_OK);
  ASSERT_NE(first, nullptr);

  std::string source;
  ASSERT_EQ(decompile_text(first, 0, source), RADICLE_STATUS_OK);
  EXPECT_EQ(source.rfind("ScriptName Probe", 0), 0u);
  std::string listing;
  ASSERT_EQ(decompile_text(first, 1, listing), RADICLE_STATUS_OK);
  EXPECT_EQ(listing.rfind("; Disassembly of Probe.psc\n", 0), 0u);

  radicle_pex* second = nullptr;
  ASSERT_EQ(compile_source(source, base_stubs().string(), &second, rows), RADICLE_STATUS_OK);
  ASSERT_NE(second, nullptr);
  EXPECT_TRUE(rows.empty()) << rows.front();
  const auto original = fo4::pex::PexFile::fromBytes(write_pex(first));
  const auto recompiled = fo4::pex::PexFile::fromBytes(write_pex(second));
  radicle_pex_destroy(first);
  radicle_pex_destroy(second);
  ASSERT_EQ(original.objects.size(), 1u);
  ASSERT_EQ(recompiled.objects.size(), 1u);
  EXPECT_EQ(fo4::papyrus::test::firstDifference(original.objects[0], recompiled.objects[0]), "");
}

TEST(CApiPapyrusCompileTest, SourceErrorsReturnRowsAndNoHandle) {
  radicle_pex* pex = nullptr;
  std::vector<std::string> rows;
  ASSERT_EQ(compile_source("Scriptname Probe\nInt a = 1 # 2\n", base_stubs().string(), &pex, rows),
            RADICLE_STATUS_OK);
  EXPECT_EQ(pex, nullptr);
  EXPECT_NE(std::find(rows.begin(), rows.end(), "error\tPAP0004\t2\t11\tUnexpected character '#'."), rows.end());
}

TEST(CApiPapyrusCompileTest, RefusalsClearEveryOutputParameter) {
  const std::string one = "Scriptname Probe\n";
  radicle_pex* pex = nullptr;
  char** raw = nullptr;
  size_t count = 0;
  const radicle_papyrus_compile_options options{0, 0, nullptr};
  const auto arm = [&] {
    pex = reinterpret_cast<radicle_pex*>(1);
    raw = reinterpret_cast<char**>(1);
    count = 9;
  };
  const auto source = reinterpret_cast<const uint8_t*>(one.data());

  arm();
  EXPECT_EQ(radicle_papyrus_compile_source(nullptr, 4, "Probe.psc", options, &pex, &raw, &count),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_STREQ(radicle_last_error(), "source is required");
  EXPECT_EQ(pex, nullptr);
  EXPECT_EQ(raw, nullptr);
  EXPECT_EQ(count, 0u);

  arm();
  EXPECT_EQ(radicle_papyrus_compile_source(source, 0, "Probe.psc", options, &pex, &raw, &count),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_STREQ(radicle_last_error(), "source must not be empty");
  EXPECT_EQ(pex, nullptr);
  EXPECT_EQ(raw, nullptr);
  EXPECT_EQ(count, 0u);

  arm();
  EXPECT_EQ(radicle_papyrus_compile_source(source, RADICLE_PAPYRUS_SOURCE_MAX_BYTES + 1, "Probe.psc", options,
                                           &pex, &raw, &count),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_STREQ(radicle_last_error(), "source size 4194305 is above the 4194304-byte cap");
  EXPECT_EQ(pex, nullptr);
  EXPECT_EQ(raw, nullptr);
  EXPECT_EQ(count, 0u);

  arm();
  EXPECT_EQ(radicle_papyrus_compile_source(source, one.size(), nullptr, options, &pex, &raw, &count),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_STREQ(radicle_last_error(), "file_name is required");
  EXPECT_EQ(pex, nullptr);
  EXPECT_EQ(raw, nullptr);
  EXPECT_EQ(count, 0u);

  EXPECT_EQ(radicle_papyrus_compile_source(source, one.size(), "Probe.psc", options, nullptr, &raw, &count),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_STREQ(radicle_last_error(), "out_pex, diag_rows and diag_count are required");

  uint8_t* text = reinterpret_cast<uint8_t*>(1);
  size_t text_size = 9;
  EXPECT_EQ(radicle_papyrus_decompile(nullptr, 0, &text, &text_size), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_STREQ(radicle_last_error(), "pex, out_text and out_size are required");
  EXPECT_EQ(text, nullptr);
  EXPECT_EQ(text_size, 0u);
}
