#include "fo4/abc/abc.hpp"
#include "fo4/archive/ba2.hpp"
#include "fo4/avm2/interpreter.hpp"
#include "fo4/avm2/name.hpp"
#include "fo4/avm2/runtime.hpp"
#include "fo4/avm2/value.hpp"
#include "fo4/swf/swf.hpp"
#include "real_data_build.hpp"
#include "real_data_movies.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace abc = fo4::abc;
namespace avm2 = fo4::avm2;

bool is_programs_movie(const std::string& path) {
  if (lower_extension(path) != ".swf") {
    return false;
  }
  std::string lowered = path;
  for (char& ch : lowered) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  return lowered.find("programs\\") != std::string::npos;
}

bool starts_with(const std::string& text, std::string_view prefix) {
  return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

struct ProbeTotals {
  std::size_t movies{0};
  std::size_t linked{0};
  std::size_t link_failures{0};
  std::size_t scripts{0};
  std::size_t ran_ok{0};
  std::size_t unimplemented_opcodes{0};
  std::size_t other_failures{0};
  std::map<std::string, std::size_t> other_messages;
  std::vector<std::string> unimplemented_examples;
};

/* The probe must reach past unbound natives; an unimplemented opcode must still fail it. */
void probe_movie(const std::string& path, const std::vector<std::byte>& bytes, ProbeTotals& totals) {
  const fo4::swf::SwfReadResult read = fo4::swf::read_swf(bytes);
  if (!read.ok()) {
    ++totals.other_failures;
    totals.other_messages["movie read: " + std::string{fo4::swf::status_name(read.status)}] += 1;
    return;
  }
  const std::vector<fo4::swf::SwfAbcBlock> blocks = fo4::swf::abc_blocks(read.file);
  std::deque<abc::AbcFile> files;
  for (const fo4::swf::SwfAbcBlock& block : blocks) {
    auto& file = files.emplace_back();
    if (abc::AbcFile::parse(reinterpret_cast<const std::uint8_t*>(block.payload.data()),
                            block.payload.size(), file) != abc::AbcStatus::Ok) {
      files.pop_back();
    }
  }
  avm2::Runtime runtime;
  for (const abc::AbcFile& file : files) {
    runtime.add_file(file);
  }
  const avm2::RuntimeLoad link = runtime.link();
  if (!link.ok()) {
    ++totals.link_failures;
    totals.other_messages["link: " + std::string{avm2::runtime_status_name(link.status)} + " " +
                          link.message] += 1;
    return;
  }
  ++totals.linked;
  avm2::InterpreterLimits limits;
  limits.lenient_builtins = true;
  limits.max_instructions = 50000;
  avm2::Interpreter interpreter(runtime, limits);
  for (std::size_t index = 0; index < runtime.scripts().size(); ++index) {
    ++totals.scripts;
    const avm2::ExecutionResult result = interpreter.run_script(index);
    if (result.ok()) {
      ++totals.ran_ok;
      continue;
    }
    if (starts_with(result.message, "opcode ")) {
      ++totals.unimplemented_opcodes;
      if (totals.unimplemented_examples.size() < 16) {
        totals.unimplemented_examples.push_back(path + " script " + std::to_string(index) + ": " +
                                                result.message);
      }
      continue;
    }
    ++totals.other_failures;
    totals.other_messages[result.message.empty()
                              ? std::string{avm2::interpreter_status_name(result.status)}
                              : result.message] += 1;
  }
}

}  // namespace

TEST(Avm2ScriptsReal, RunsEveryProgramsScriptInitializerWithoutUnimplementedOpcode) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder";
  }
  const std::filesystem::path data_dir(dir);
  const GameBuild build = detect_game_build(data_dir);
  record_game_build(build);
  const auto archive_path = data_dir / "Fallout4 - Interface.ba2";
  if (!std::filesystem::exists(archive_path)) {
    GTEST_SKIP() << "no Fallout4 - Interface.ba2 in FO4_DATA_DIR";
  }
  const fo4::archive::Ba2GeneralArchive archive = fo4::archive::read_ba2_general_file(archive_path);

  ProbeTotals totals;
  for (const fo4::archive::Ba2GeneralEntry& entry : archive.entries) {
    if (!is_programs_movie(entry.path)) {
      continue;
    }
    ++totals.movies;
    probe_movie(entry.path, fo4::archive::extract_ba2_general_entry(entry), totals);
  }

  RecordProperty("program_movies", std::to_string(totals.movies));
  RecordProperty("linked_movies", std::to_string(totals.linked));
  RecordProperty("link_failures", std::to_string(totals.link_failures));
  RecordProperty("scripts", std::to_string(totals.scripts));
  RecordProperty("scripts_ran_ok", std::to_string(totals.ran_ok));
  RecordProperty("unimplemented_opcodes", std::to_string(totals.unimplemented_opcodes));
  RecordProperty("other_failures", std::to_string(totals.other_failures));
  std::string other_summary;
  for (const auto& [message, count] : totals.other_messages) {
    other_summary += message + " x" + std::to_string(count) + "; ";
  }
  RecordProperty("other_failure_messages", other_summary.substr(0, 2000));
  std::string unimplemented_summary;
  for (const std::string& example : totals.unimplemented_examples) {
    unimplemented_summary += example + "; ";
  }
  RecordProperty("unimplemented_examples", unimplemented_summary.substr(0, 2000));

  ASSERT_GT(totals.movies, 0u) << "no Programs movies in " << archive_path.string();
  EXPECT_GT(totals.linked, 0u) << other_summary;
  EXPECT_GT(totals.scripts, 0u);
  EXPECT_EQ(totals.unimplemented_opcodes, 0u) << unimplemented_summary;
}
