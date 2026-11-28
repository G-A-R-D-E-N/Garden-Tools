#include <fo4/c_api.h>
#include <fo4/c_api_papyrus.h>
#include <fo4/c_api_papyrus_runtime.h>
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
#include <utility>
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

const std::string kDebugOnlyCall =
    "Scriptname Probe\n"
    "Int Property Count Auto\n"
    "Function Go(int amount)\n"
    "    int total = Count + amount\n"
    "    Debug.Trace(\"probe\")\n"
    "    If total > 3\n"
    "        Count = total\n"
    "    EndIf\n"
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

namespace {

radicle_status catalog_parse(const std::string& source, radicle_event_catalog** out) {
  return radicle_papyrus_event_catalog_parse(reinterpret_cast<const uint8_t*>(source.data()), source.size(), out);
}

const char* const kEventSource =
    "ScriptName Actor extends ObjectReference\n"
    "Event OnDeath(Actor akKiller)\n"
    "EndEvent\n"
    "Event OnLoad()\n"
    "EndEvent\n"
    "CustomEvent Ping\n"
    "Function Helper()\n"
    "EndFunction\n"
    "State Dead\n"
    "  Event OnBeginState()\n"
    "  EndEvent\n"
    "EndState\n";

}  // namespace

TEST(CApiPapyrusEventCatalogTest, ListsScriptLevelEventsThenStateEventsInSourceOrder) {
  radicle_event_catalog* catalog = nullptr;
  ASSERT_EQ(catalog_parse(kEventSource, &catalog), RADICLE_STATUS_OK) << radicle_last_error();
  ASSERT_NE(catalog, nullptr);
  ASSERT_EQ(radicle_papyrus_event_catalog_count(catalog), 4u);

  struct Expected {
    const char* name;
    const char* state;
    int32_t is_custom;
  };
  const Expected expected[] = {{"OnDeath", "", 0}, {"OnLoad", "", 0}, {"Ping", "", 1}, {"OnBeginState", "Dead", 0}};
  for (size_t index = 0; index < 4; ++index) {
    radicle_event_info info{};
    ASSERT_EQ(radicle_papyrus_event_catalog_get(catalog, index, &info), RADICLE_STATUS_OK);
    EXPECT_STREQ(info.name, expected[index].name);
    EXPECT_STREQ(info.state, expected[index].state);
    EXPECT_EQ(info.is_custom, expected[index].is_custom);
    EXPECT_STREQ(info.declaring_script, "Actor");
    EXPECT_EQ(info.owner_kind, RADICLE_EVENT_OWNER_ACTOR);
  }
  radicle_event_info first{};
  ASSERT_EQ(radicle_papyrus_event_catalog_get(catalog, 0, &first), RADICLE_STATUS_OK);
  EXPECT_EQ(first.param_count, 1u);
  radicle_event_param param{};
  ASSERT_EQ(radicle_papyrus_event_catalog_param(catalog, 0, 0, &param), RADICLE_STATUS_OK);
  EXPECT_STREQ(param.name, "akKiller");
  EXPECT_STREQ(param.type, "Actor");
  EXPECT_EQ(param.is_array, 0);
  radicle_papyrus_event_catalog_destroy(catalog);
}

TEST(CApiPapyrusEventCatalogTest, ReportsParameterNamesTypesAndArrays) {
  const std::string source = "ScriptName ObjectReference extends Form\n"
                             "Event OnPackageEnd(Form[] akItems, int aiCount)\n"
                             "EndEvent\n";
  radicle_event_catalog* catalog = nullptr;
  ASSERT_EQ(catalog_parse(source, &catalog), RADICLE_STATUS_OK) << radicle_last_error();
  ASSERT_EQ(radicle_papyrus_event_catalog_count(catalog), 1u);
  radicle_event_info info{};
  ASSERT_EQ(radicle_papyrus_event_catalog_get(catalog, 0, &info), RADICLE_STATUS_OK);
  EXPECT_EQ(info.owner_kind, RADICLE_EVENT_OWNER_OBJECT_REFERENCE);
  EXPECT_EQ(info.param_count, 2u);

  radicle_event_param param{};
  ASSERT_EQ(radicle_papyrus_event_catalog_param(catalog, 0, 0, &param), RADICLE_STATUS_OK);
  EXPECT_STREQ(param.name, "akItems");
  EXPECT_STREQ(param.type, "Form");
  EXPECT_EQ(param.is_array, 1);
  ASSERT_EQ(radicle_papyrus_event_catalog_param(catalog, 0, 1, &param), RADICLE_STATUS_OK);
  EXPECT_STREQ(param.name, "aiCount");
  EXPECT_EQ(param.is_array, 0);

  param = radicle_event_param{};
  EXPECT_EQ(radicle_papyrus_event_catalog_param(catalog, 0, 2, &param), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(param.name, nullptr);
  EXPECT_EQ(radicle_papyrus_event_catalog_param(catalog, 1, 0, &param), RADICLE_STATUS_INVALID_ARGUMENT);
  radicle_papyrus_event_catalog_destroy(catalog);
}

TEST(CApiPapyrusEventCatalogTest, OwnerKindFollowsTheDeclaringScriptNameAlone) {
  const std::pair<const char*, radicle_event_owner_kind> cases[] = {
      {"Actor", RADICLE_EVENT_OWNER_ACTOR},
      {"ObjectReference", RADICLE_EVENT_OWNER_OBJECT_REFERENCE},
      {"Quest", RADICLE_EVENT_OWNER_QUEST},
      {"Alias", RADICLE_EVENT_OWNER_ALIAS},
      {"ReferenceAlias", RADICLE_EVENT_OWNER_ALIAS},
      {"MyQuestScript", RADICLE_EVENT_OWNER_OTHER},
  };
  for (const auto& [name, kind] : cases) {
    const std::string source = std::string("ScriptName ") + name + " extends Form\nEvent OnInit()\nEndEvent\n";
    radicle_event_catalog* catalog = nullptr;
    ASSERT_EQ(catalog_parse(source, &catalog), RADICLE_STATUS_OK) << name << ": " << radicle_last_error();
    radicle_event_info info{};
    ASSERT_EQ(radicle_papyrus_event_catalog_get(catalog, 0, &info), RADICLE_STATUS_OK);
    EXPECT_EQ(info.owner_kind, kind) << name;
    radicle_papyrus_event_catalog_destroy(catalog);
  }
}

TEST(CApiPapyrusEventCatalogTest, ParseErrorReturnsNoHandleAndRefusalsClearOutputs) {
  radicle_event_catalog* catalog = reinterpret_cast<radicle_event_catalog*>(static_cast<std::uintptr_t>(1));
  EXPECT_EQ(catalog_parse("ScriptName Broken\nEvent OnX(\n", &catalog), RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(catalog, nullptr);
  EXPECT_NE(std::string(radicle_last_error()).find(':'), std::string::npos);

  EXPECT_EQ(radicle_papyrus_event_catalog_parse(nullptr, 0, &catalog), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(catalog_parse(kEventSource, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_papyrus_event_catalog_parse(reinterpret_cast<const uint8_t*>(kEventSource), 0, &catalog),
            RADICLE_STATUS_INVALID_ARGUMENT);

  ASSERT_EQ(catalog_parse(kEventSource, &catalog), RADICLE_STATUS_OK);
  radicle_event_info info{};
  info.name = "stale";
  EXPECT_EQ(radicle_papyrus_event_catalog_get(catalog, 4, &info), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(info.name, nullptr);
  EXPECT_EQ(radicle_papyrus_event_catalog_get(nullptr, 0, &info), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_papyrus_event_catalog_count(nullptr), 0u);
  radicle_papyrus_event_catalog_destroy(nullptr);
  radicle_papyrus_event_catalog_destroy(catalog);
}

TEST(CApiPapyrusSourceTest, CompilesOwnScriptStructAccessThatTheInterpreterRuns) {
  const std::string source =
      "Scriptname StructProbe\n"
      "Struct Point\n"
      "    Int X = 1\n"
      "    String Label = \"a\"\n"
      "EndStruct\n"
      "Int Function Bump(Int start)\n"
      "    Point p = new Point\n"
      "    p.X = start + 4\n"
      "    p.X += 3\n"
      "    Return p.X\n"
      "EndFunction\n"
      "String Function Name(String text)\n"
      "    Point p = new Point\n"
      "    p.Label = text + \"!\"\n"
      "    Return p.Label\n"
      "EndFunction\n";
  radicle_pex* pex = nullptr;
  std::vector<std::string> rows;
  ASSERT_EQ(compile_source(source, base_stubs().string(), &pex, rows), RADICLE_STATUS_OK);
  EXPECT_TRUE(rows.empty()) << (rows.empty() ? "" : rows[0]);
  ASSERT_NE(pex, nullptr);

  radicle_papyrus_runtime* runtime = nullptr;
  ASSERT_EQ(radicle_papyrus_runtime_create(nullptr, &runtime), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_papyrus_runtime_add_pex(runtime, pex), RADICLE_STATUS_OK);
  uint64_t instance = 0;
  ASSERT_EQ(radicle_papyrus_runtime_attach(runtime, "StructProbe", RADICLE_PAPYRUS_OWNER_FORM, "000830:StructProbe.esp",
                                           "", &instance),
            RADICLE_STATUS_OK);
  radicle_papyrus_status status = RADICLE_PAPYRUS_STATUS_INTERNAL_ERROR;
  ASSERT_EQ(radicle_papyrus_runtime_initialize(runtime, instance, &status), RADICLE_STATUS_OK);
  ASSERT_EQ(status, RADICLE_PAPYRUS_STATUS_OK);

  radicle_papyrus_value* start = nullptr;
  ASSERT_EQ(radicle_papyrus_value_int(10, &start), RADICLE_STATUS_OK);
  const radicle_papyrus_value* int_args[] = {start};
  uint64_t ticket = 0;
  ASSERT_EQ(radicle_papyrus_runtime_call_function(runtime, instance, "Bump", int_args, 1, &ticket, &status),
            RADICLE_STATUS_OK);
  EXPECT_EQ(status, RADICLE_PAPYRUS_STATUS_OK);
  ASSERT_NE(ticket, 0u);
  radicle_papyrus_step_result step{};
  ASSERT_EQ(radicle_papyrus_runtime_step(runtime, 512, &step), RADICLE_STATUS_OK);
  radicle_papyrus_call_info info{};
  info.struct_size = sizeof(info);
  radicle_papyrus_value* result = nullptr;
  ASSERT_EQ(radicle_papyrus_runtime_call_result(runtime, ticket, 1, &info, &result), RADICLE_STATUS_OK);
  EXPECT_EQ(info.state, RADICLE_PAPYRUS_CALL_DONE);
  EXPECT_EQ(info.status, RADICLE_PAPYRUS_STATUS_OK);
  ASSERT_NE(result, nullptr);
  EXPECT_EQ(radicle_papyrus_value_int_of(result), 17);
  radicle_papyrus_value_destroy(result);
  radicle_papyrus_value_destroy(start);

  radicle_papyrus_value* text = nullptr;
  ASSERT_EQ(radicle_papyrus_value_string("hi", 2, &text), RADICLE_STATUS_OK);
  const radicle_papyrus_value* text_args[] = {text};
  ticket = 0;
  ASSERT_EQ(radicle_papyrus_runtime_call_function(runtime, instance, "Name", text_args, 1, &ticket, &status),
            RADICLE_STATUS_OK);
  EXPECT_EQ(status, RADICLE_PAPYRUS_STATUS_OK);
  ASSERT_NE(ticket, 0u);
  ASSERT_EQ(radicle_papyrus_runtime_step(runtime, 512, &step), RADICLE_STATUS_OK);
  result = nullptr;
  ASSERT_EQ(radicle_papyrus_runtime_call_result(runtime, ticket, 1, &info, &result), RADICLE_STATUS_OK);
  EXPECT_EQ(info.state, RADICLE_PAPYRUS_CALL_DONE);
  EXPECT_EQ(info.status, RADICLE_PAPYRUS_STATUS_OK);
  ASSERT_NE(result, nullptr);
  EXPECT_EQ(radicle_papyrus_value_kind_of(result), RADICLE_PAPYRUS_VALUE_STRING);
  EXPECT_STREQ(radicle_papyrus_value_text(result), "hi!");
  radicle_papyrus_value_destroy(result);
  radicle_papyrus_value_destroy(text);
  radicle_papyrus_runtime_destroy(runtime);
  radicle_pex_destroy(pex);
}

TEST(CApiPapyrusSourceTest, StillRefusesAnUnknownMemberOfAnOwnScriptStruct) {
  const std::string source =
      "Scriptname StructProbe\n"
      "Struct Point\n"
      "    Int X = 1\n"
      "EndStruct\n"
      "Int Function Bad()\n"
      "    Point p = new Point\n"
      "    Return p.Missing\n"
      "EndFunction\n";
  radicle_pex* pex = nullptr;
  std::vector<std::string> rows;
  ASSERT_EQ(compile_source(source, base_stubs().string(), &pex, rows), RADICLE_STATUS_OK);
  EXPECT_EQ(pex, nullptr);
  ASSERT_FALSE(rows.empty());
  EXPECT_EQ(rows[0].rfind("error\t", 0), 0u);
}

TEST(CApiPapyrusSourceTest, ReportsAParseFailureAsARow) {
  std::vector<std::string> rows;
  ASSERT_EQ(summarize_source("Scriptname B\nInt a = 1 # 2\n", rows), RADICLE_STATUS_OK);
  ASSERT_EQ(rows.size(), 1u);
  EXPECT_EQ(rows[0].rfind("error\t2\t", 0), 0u) << rows[0];

  /* Must keep a message that quotes a decoded tab on one row with three separators. */
  std::vector<std::string> quoted;
  ASSERT_EQ(summarize_source("Scriptname B\nState \"a\tb\"\nEndState\n", quoted), RADICLE_STATUS_OK);
  ASSERT_EQ(quoted.size(), 1u);
  EXPECT_EQ(std::count(quoted[0].begin(), quoted[0].end(), '\t'), 3);
  EXPECT_EQ(quoted[0].find('\n'), std::string::npos);
  EXPECT_EQ(quoted[0].find('\r'), std::string::npos);
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
  const auto without_debug = compiled_bytes(kDebugOnlyCall, base_stubs().string(), 0, 1);
  EXPECT_EQ(without_debug, reference_bytes(kDebugOnlyCall, false, false));
  const auto with_debug = compiled_bytes(kDebugOnlyCall, base_stubs().string(), 0, 0);
  EXPECT_EQ(with_debug, reference_bytes(kDebugOnlyCall, false, true));
  EXPECT_NE(without_debug, with_debug);
  const auto release = compiled_bytes(kDebugOnlyCall, base_stubs().string(), 1, 0);
  EXPECT_EQ(release, reference_bytes(kDebugOnlyCall, true, true));
  EXPECT_NE(release, with_debug);
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
  for (const auto& row : rows) EXPECT_NE(row.rfind("error\t", 0), 0u) << row;
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
  ASSERT_EQ(compile_source("Scriptname Probe\nInt a = 1 @ 2\n", base_stubs().string(), &pex, rows),
            RADICLE_STATUS_OK);
  EXPECT_EQ(pex, nullptr);
  EXPECT_NE(std::find(rows.begin(), rows.end(), "error\tPAP0004\t2\t11\tUnexpected character '@'."), rows.end());
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

TEST(CApiPapyrusCompileTest, ParameterSpelledLikeItsFunctionSharesOneStringTableEntry) {
  const std::string source =
      "Scriptname Probe\n"
      "Function G(Int g)\n"
      "EndFunction\n";
  radicle_pex* pex = nullptr;
  std::vector<std::string> rows;
  ASSERT_EQ(compile_source(source, base_stubs().string(), &pex, rows), RADICLE_STATUS_OK);
  ASSERT_NE(pex, nullptr);

  radicle_pex_object_code code{};
  ASSERT_EQ(radicle_pex_object_code_get(pex, 0, &code), RADICLE_STATUS_OK) << radicle_last_error();
  ASSERT_EQ(code.function_count, 1u);
  ASSERT_EQ(code.functions[0].parameter_count, 1u);
  EXPECT_EQ(code.typed_names[code.functions[0].first_parameter].name, code.functions[0].name);
  radicle_pex_free_object_code(&code);

  const auto table = fo4::pex::PexFile::fromBytes(write_pex(pex)).stringTable;
  EXPECT_EQ(std::count(table.begin(), table.end(), "G"), 1);
  EXPECT_EQ(std::count(table.begin(), table.end(), "g"), 0);
  radicle_pex_destroy(pex);
}

static fo4::pex::PexFile compile_to_pex_file(const std::string& source) {
  radicle_pex* pex = nullptr;
  std::vector<std::string> rows;
  EXPECT_EQ(compile_source(source, base_stubs().string(), &pex, rows), RADICLE_STATUS_OK);
  EXPECT_NE(pex, nullptr);
  auto file = fo4::pex::PexFile::fromBytes(write_pex(pex));
  radicle_pex_destroy(pex);
  return file;
}

static int count_identifier_operands(const fo4::pex::PexFunction& fn, const std::string& name) {
  int found = 0;
  for (const auto& instruction : fn.instructions) {
    for (const auto& arg : instruction.args) {
      if (arg.type == fo4::pex::PexValueType::Identifier && arg.str == name) ++found;
    }
  }
  return found;
}

TEST(CApiPapyrusCompileTest, ParameterNamedIIsSpelledAsKeywordAlias) {
  const auto file = compile_to_pex_file(
      "Scriptname Probe\n"
      "Function F(Int i)\n"
      "EndFunction\n");
  EXPECT_EQ(std::count(file.stringTable.begin(), file.stringTable.end(), "I"), 1);
  EXPECT_EQ(std::count(file.stringTable.begin(), file.stringTable.end(), "i"), 0);
  const auto& fn = file.objects.at(0).states.at(0).functions.at(0);
  ASSERT_EQ(fn.params.size(), 1u);
  EXPECT_EQ(fn.params[0].name, "I");
}

TEST(CApiPapyrusCompileTest, LocalNamedSIsSpelledAsKeywordAliasInOperands) {
  const auto file = compile_to_pex_file(
      "Scriptname Probe\n"
      "Function F()\n"
      "    String s = \"x\"\n"
      "    Debug.Trace(s)\n"
      "EndFunction\n");
  EXPECT_EQ(std::count(file.stringTable.begin(), file.stringTable.end(), "S"), 1);
  EXPECT_EQ(std::count(file.stringTable.begin(), file.stringTable.end(), "s"), 0);
  const auto& fn = file.objects.at(0).states.at(0).functions.at(0);
  EXPECT_EQ(std::count_if(fn.locals.begin(), fn.locals.end(), [](const auto& l) { return l.name == "S"; }), 1);
  EXPECT_EQ(count_identifier_operands(fn, "S"), 2);
  EXPECT_EQ(count_identifier_operands(fn, "s"), 0);
}

TEST(CApiPapyrusCompileTest, OtherSingleLetterParameterKeepsItsSpelling) {
  const auto file = compile_to_pex_file(
      "Scriptname Probe\n"
      "Function F(Bool b)\n"
      "EndFunction\n");
  EXPECT_EQ(std::count(file.stringTable.begin(), file.stringTable.end(), "b"), 1);
  EXPECT_EQ(std::count(file.stringTable.begin(), file.stringTable.end(), "B"), 0);
}

TEST(CApiPapyrusCompileTest, StringLiteralSIsNotAliased) {
  const auto file = compile_to_pex_file(
      "Scriptname Probe\n"
      "Function F()\n"
      "    Debug.Trace(\"s\")\n"
      "EndFunction\n");
  EXPECT_EQ(std::count(file.stringTable.begin(), file.stringTable.end(), "s"), 1);
  EXPECT_EQ(std::count(file.stringTable.begin(), file.stringTable.end(), "S"), 0);
}
