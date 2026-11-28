#include "bgs_capture.hpp"
#include "bgs_test_movies.hpp"

#include "fo4/avm2/host.hpp"
#include "fo4/avm2/interpreter.hpp"
#include "fo4/avm2/runtime.hpp"
#include "fo4/swf/swf.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using bgs_test::capture_root_file;
using bgs_test::frame_failure_file;
using bgs_test::movie_of;
using fo4::avm2::HostCallbacks;
using fo4::avm2::Interpreter;
using fo4::avm2::Runtime;

std::string capture_line(int seq, int frame, const char* direction, const char* method,
                         const char* args, const char* answer) {
  return std::to_string(seq) + '\t' + std::to_string(frame) + '\t' + direction + '\t' + method +
         '\t' + args + '\t' + answer + '\n';
}

constexpr const char* kIntro = R"([{"type":"string","value":"intro"}])";

std::vector<std::string> capture_lines() {
  return {
      capture_line(1, 0, "to_swf", "InitProgram", "[]", "null"),
      capture_line(2, 0, "to_host", "registerSound", kIntro, "null"),
      capture_line(3, 0, "to_host", "playSound", kIntro, "null"),
      capture_line(4, 1, "to_swf", "SetHighscore", R"([{"type":"int","value":42}])", "null"),
      capture_line(5, 1, "to_host", "setHighscore", R"([{"type":"string","value":"42"}])", "null"),
      capture_line(6, 2, "to_swf", "Lookup", R"([{"type":"string","value":"OnStatus"}])", "null"),
      capture_line(7, 2, "to_host", "getTextReplaceID",
                   R"([{"type":"string","value":"OnStatus"}])", R"({"type":"string","value":"10"})"),
      capture_line(8, 2, "to_host", "notifyScripts", R"([{"type":"string","value":"10"}])", "null"),
      capture_line(9, 3, "to_swf", "Pause", R"([{"type":"boolean","value":true}])", "null"),
      capture_line(10, 3, "to_host", "playLeftAnim", "[]", "null"),
  };
}

bgs_capture::Outcome replay_text(const std::vector<std::byte>& bytes, const std::string& text) {
  const std::vector<bgs_capture::Row> rows = bgs_capture::parse_capture(text);
  const fo4::swf::SwfReadResult read = fo4::swf::read_swf(bytes);
  EXPECT_TRUE(read.ok()) << fo4::swf::status_name(read.status);
  Runtime runtime;
  Interpreter interpreter{runtime};
  return bgs_capture::replay_capture(runtime, interpreter, HostCallbacks{}, read.file, rows);
}

bgs_capture::Outcome replay_lines(const std::vector<std::string>& lines,
                                  bool references_random = false) {
  std::string text;
  for (const std::string& line : lines) {
    text += line;
  }
  return replay_text(movie_of(capture_root_file(references_random)), text);
}

// Catches a replay that drops a captured answer: notifyScripts must receive the replayed id.
TEST(BgsCapture, CapturedCallSequenceReplaysWithItsAnswersAndMatches) {
  const bgs_capture::Outcome outcome = replay_lines(capture_lines());
  EXPECT_EQ(outcome.replay_error, "");
  ASSERT_FALSE(outcome.first.has_value()) << outcome.first->seq << " " << outcome.first->reason;
  EXPECT_EQ(outcome.compared, 6u);
}

// Catches a comparator that accepts an SWF call the capture lacks: it must report that call.
TEST(BgsCapture, ComparisonReportsAnUncapturedCallInTheMiddle) {
  std::vector<std::string> lines = capture_lines();
  lines.erase(lines.begin() + 2);
  const bgs_capture::Outcome outcome = replay_lines(lines);
  ASSERT_TRUE(outcome.first.has_value());
  EXPECT_EQ(outcome.first->seq, 5);
  EXPECT_NE(outcome.first->reason.find("setHighscore"), std::string::npos) << outcome.first->reason;
  EXPECT_EQ(outcome.first->reason.find("RNG"), std::string::npos) << outcome.first->reason;
}

// Catches a comparator that ignores a captured call the SWF never makes: it must report it.
TEST(BgsCapture, ComparisonReportsADroppedCallTheSwfNeverMakes) {
  std::vector<std::string> lines = capture_lines();
  lines.push_back(capture_line(11, 3, "to_host", "notifyScripts",
                               R"([{"type":"string","value":"x"}])", "null"));
  const bgs_capture::Outcome outcome = replay_lines(lines);
  ASSERT_TRUE(outcome.first.has_value());
  EXPECT_EQ(outcome.first->seq, 11);
  EXPECT_NE(outcome.first->reason.find("no further calls"), std::string::npos)
      << outcome.first->reason;
}

// Catches a comparator that accepts calls out of order: the first out-of-order seq must be reported.
TEST(BgsCapture, ComparisonReportsAReorderedCapturedCallAtItsSeq) {
  std::vector<std::string> lines = capture_lines();
  lines[2] =
      capture_line(3, 0, "to_host", "setHighscore", R"([{"type":"string","value":"42"}])", "null");
  lines[4] = capture_line(5, 1, "to_host", "playSound", kIntro, "null");
  const bgs_capture::Outcome outcome = replay_lines(lines);
  ASSERT_TRUE(outcome.first.has_value());
  EXPECT_EQ(outcome.first->seq, 3);
}

// Catches a comparator that ignores arguments: a changed argument must be reported at its seq.
TEST(BgsCapture, ComparisonReportsAWrongCapturedArgumentAtItsSeq) {
  std::vector<std::string> lines = capture_lines();
  lines[2] =
      capture_line(3, 0, "to_host", "playSound", R"([{"type":"string","value":"outro"}])", "null");
  const bgs_capture::Outcome outcome = replay_lines(lines);
  ASSERT_TRUE(outcome.first.has_value());
  EXPECT_EQ(outcome.first->seq, 3);
  EXPECT_NE(outcome.first->reason.find("outro"), std::string::npos) << outcome.first->reason;
}

// Catches a comparator that ignores method names: a swap with equal arguments must be reported.
TEST(BgsCapture, ComparisonReportsAMethodSwapThatKeepsTheArguments) {
  std::vector<std::string> lines = capture_lines();
  lines[1] = capture_line(2, 0, "to_host", "playSound", kIntro, "null");
  lines[2] = capture_line(3, 0, "to_host", "registerSound", kIntro, "null");
  const bgs_capture::Outcome outcome = replay_lines(lines);
  ASSERT_TRUE(outcome.first.has_value());
  EXPECT_EQ(outcome.first->seq, 2);
  EXPECT_NE(outcome.first->reason.find("registerSound"), std::string::npos)
      << outcome.first->reason;
}

// Catches a replay that loses the first difference when a later game call fails: seq 3 must stay first.
TEST(BgsCapture, ComparisonKeepsTheFirstDifferenceWhenALaterGameCallFails) {
  std::vector<std::string> lines = capture_lines();
  lines[2] =
      capture_line(3, 0, "to_host", "playSound", R"([{"type":"string","value":"outro"}])", "null");
  lines.push_back(capture_line(12, 2, "to_swf", "SetPlatform",
                               R"([{"type":"uint","value":0},{"type":"boolean","value":false}])",
                               "null"));
  const bgs_capture::Outcome outcome = replay_lines(lines);
  ASSERT_TRUE(outcome.first.has_value());
  EXPECT_EQ(outcome.first->seq, 3);
  EXPECT_EQ(outcome.compared, 6u);
}

// Catches a missing root member that aborts the comparison: the failure must be reported at its seq.
TEST(BgsCapture, ComparisonReportsAGameCallTheRootCannotRun) {
  std::vector<std::string> lines = capture_lines();
  lines.insert(lines.begin(),
               capture_line(0, 0, "to_swf", "SetPlatform",
                            R"([{"type":"uint","value":0},{"type":"boolean","value":false}])",
                            "null"));
  const bgs_capture::Outcome outcome = replay_lines(lines);
  ASSERT_TRUE(outcome.first.has_value());
  EXPECT_EQ(outcome.first->seq, 0);
  EXPECT_NE(outcome.first->reason.find("SetPlatform"), std::string::npos) << outcome.first->reason;
  EXPECT_EQ(outcome.compared, 6u);
}

// Catches a capture with no to_host rows that would pass with nothing compared: it must be rejected.
TEST(BgsCapture, ReplayRejectsACaptureWithoutSwfToHostRows) {
  const bgs_capture::Outcome header_only = replay_text(
      movie_of(capture_root_file(false)), "seq\tframe\tdirection\tmethod\targs\treturn\n");
  EXPECT_FALSE(header_only.matched());
  EXPECT_NE(header_only.replay_error.find("no to_host rows"), std::string::npos)
      << header_only.replay_error;
  const bgs_capture::Outcome game_only = replay_text(
      movie_of(capture_root_file(false)), capture_line(1, 0, "to_swf", "InitProgram", "[]", "null"));
  EXPECT_FALSE(game_only.matched());
  EXPECT_NE(game_only.replay_error.find("no to_host rows"), std::string::npos)
      << game_only.replay_error;
}

// Catches a missing capture file read as an empty capture: the read must report failure.
TEST(BgsCapture, ReadCaptureFileRefusesAMissingFile) {
  EXPECT_FALSE(bgs_capture::read_capture_file("zz-no-such-capture-file.tsv").has_value());
}

// Catches a frame script that stops the movie silently: the script's error must reach the outcome.
TEST(BgsCapture, ReplayReportsTheScriptErrorWhenTheMovieStopsInAdvance) {
  const bgs_capture::Outcome outcome =
      replay_text(movie_of(frame_failure_file(), 2),
                  capture_line(1, 0, "to_host", "registerSound", kIntro, "null"));
  EXPECT_FALSE(outcome.matched());
  EXPECT_NE(outcome.replay_error.find("frame script error"), std::string::npos)
      << outcome.replay_error;
  EXPECT_NE(outcome.replay_error.find("noSuchFrameFunction"), std::string::npos)
      << outcome.replay_error;
}

// Catches a BOM glued to the header: the first row must still parse.
TEST(BgsCapture, ParserStripsAByteOrderMarkBeforeTheHeader) {
  const std::string text =
      std::string("\xEF\xBB\xBF") + "seq\tframe\tdirection\tmethod\targs\treturn\n" +
      capture_line(1, 0, "to_swf", "InitProgram", "[]", "null") +
      capture_line(2, 0, "to_host", "registerSound", kIntro, "null");
  const std::vector<bgs_capture::Row> rows = bgs_capture::parse_capture(text);
  ASSERT_EQ(rows.size(), 2u);
  EXPECT_EQ(rows[0].method, "InitProgram");
  EXPECT_EQ(rows[1].method, "registerSound");
}

// Catches an unknown value type that fails without its line or name: both must be in the error.
TEST(BgsCapture, ParserNamesTheLineAndTheTypeOfAnUnknownValueType) {
  for (const char* type : {"stringW", "Object", "Array"}) {
    const std::string args = std::string{"[{\"type\":\""} + type + "\",\"value\":\"x\"}]";
    const std::string text = capture_line(1, 0, "to_swf", "InitProgram", "[]", "null") +
                             capture_line(2, 0, "to_host", "playSound", args.c_str(), "null");
    try {
      bgs_capture::parse_capture(text);
      ADD_FAILURE() << type << " was accepted";
    } catch (const std::runtime_error& error) {
      const std::string message = error.what();
      EXPECT_NE(message.find("capture line 2"), std::string::npos) << message;
      EXPECT_NE(message.find(type), std::string::npos) << message;
    }
  }
}

// Catches an RNG-driven mismatch reported as plain: the RNG note must be present.
TEST(BgsCapture, ComparisonNamesPossibleRngDivergenceWhenTheMovieReferencesRandom) {
  std::vector<std::string> lines = capture_lines();
  lines[2] =
      capture_line(3, 0, "to_host", "playSound", R"([{"type":"string","value":"outro"}])", "null");
  const bgs_capture::Outcome outcome = replay_lines(lines, true);
  ASSERT_TRUE(outcome.first.has_value());
  EXPECT_EQ(outcome.first->seq, 3);
  EXPECT_NE(outcome.first->reason.find("RNG-driven"), std::string::npos)
      << outcome.first->reason;
}

}  // namespace
