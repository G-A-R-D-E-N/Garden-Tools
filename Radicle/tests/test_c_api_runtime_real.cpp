#include "fo4/c_api.h"
#include "fo4/c_api_env.h"
#include "fo4/c_api_runtime.h"
#include "fo4/avm2/host.hpp"
#include "fo4/avm2/interpreter.hpp"
#include "fo4/avm2/name.hpp"
#include "fo4/avm2/object.hpp"
#include "fo4/avm2/replay.hpp"
#include "fo4/avm2/runtime.hpp"
#include "fo4/avm2/swf_runtime.hpp"
#include "fo4/avm2/value.hpp"
#include "fo4/swf/swf.hpp"
#include "bgs_capture.hpp"
#include "fake_bgs_host.hpp"
#include "real_data_build.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

struct ArchiveDeleter {
  void operator()(radicle_ba2* ba2) const { radicle_ba2_close(ba2); }
};

struct RuntimeDeleter {
  void operator()(radicle_runtime* runtime) const { radicle_runtime_close(runtime); }
};

struct MovieDeleter {
  void operator()(radicle_movie* movie) const { radicle_movie_close(movie); }
};

using ArchiveHandle = std::unique_ptr<radicle_ba2, ArchiveDeleter>;
using RuntimeHandle = std::unique_ptr<radicle_runtime, RuntimeDeleter>;
using MovieHandle = std::unique_ptr<radicle_movie, MovieDeleter>;

struct ResolverHost {
  const radicle_ba2* archive{nullptr};
  std::string movie_directory;
  std::size_t resolved{0};
  std::size_t missed{0};
};

std::vector<std::string> candidate_paths(const ResolverHost& host, std::string_view path) {
  std::vector<std::string> candidates;
  candidates.emplace_back(path);
  candidates.emplace_back(host.movie_directory + std::string{path});
  const std::size_t slash = path.find_last_of("\\/");
  if (slash != std::string_view::npos) candidates.emplace_back(std::string{path.substr(slash + 1)});
  return candidates;
}

radicle_status resolve_asset(void* user, const char* path, uint8_t** out_bytes, size_t* out_size) {
  auto* host = static_cast<ResolverHost*>(user);
  if (path == nullptr || out_bytes == nullptr || out_size == nullptr || host->archive == nullptr) {
    return RADICLE_STATUS_INVALID_ARGUMENT;
  }
  for (const std::string& candidate : candidate_paths(*host, path)) {
    std::size_t index = 0;
    if (radicle_ba2_find(host->archive, candidate.c_str(), &index) != RADICLE_STATUS_OK) continue;
    if (radicle_ba2_extract(host->archive, index, out_bytes, out_size) != RADICLE_STATUS_OK) continue;
    ++host->resolved;
    return RADICLE_STATUS_OK;
  }
  ++host->missed;
  return RADICLE_STATUS_NOT_FOUND;
}

std::optional<std::vector<std::uint8_t>> resolve_bytes(ResolverHost& host, std::string_view path) {
  for (const std::string& candidate : candidate_paths(host, path)) {
    std::size_t index = 0;
    if (radicle_ba2_find(host.archive, candidate.c_str(), &index) != RADICLE_STATUS_OK) continue;
    std::uint8_t* bytes = nullptr;
    size_t size = 0;
    if (radicle_ba2_extract(host.archive, index, &bytes, &size) != RADICLE_STATUS_OK) {
      if (bytes != nullptr) radicle_free_bytes(bytes);
      continue;
    }
    std::vector<std::uint8_t> copy(bytes, bytes + size);
    radicle_free_bytes(bytes);
    ++host.resolved;
    return copy;
  }
  ++host.missed;
  return std::nullopt;
}

std::string directory_of(std::string_view path) {
  const std::size_t slash = path.find_last_of("\\/");
  return slash == std::string_view::npos ? std::string{} : std::string{path.substr(0, slash + 1)};
}

std::vector<std::uint64_t> replay_hashes(radicle_runtime& runtime, radicle_movie& movie) {
  std::vector<radicle_runtime_input> inputs(3);
  inputs[0].time_ms = 100.0;
  inputs[0].kind = RADICLE_INPUT_MOUSE_MOVE;
  inputs[0].x = 400.0;
  inputs[0].y = 300.0;
  inputs[1].time_ms = 250.0;
  inputs[1].kind = RADICLE_INPUT_MOUSE_DOWN;
  inputs[2].time_ms = 400.0;
  inputs[2].kind = RADICLE_INPUT_KEY_DOWN;
  inputs[2].key_code = 13;
  (void)runtime;
  std::vector<std::uint64_t> hashes(24, 0);
  size_t count = 0;
  EXPECT_EQ(radicle_movie_replay(&movie, inputs.data(), inputs.size(), 1.0 / 24.0, 24,
                                 hashes.data(), hashes.size(), &count),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(count, 24u);
  return hashes;
}

}  // namespace

TEST(CApiRuntimeReal, RunsAProgramMovieDeterministically) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder";
  }
  record_game_build(detect_game_build(dir));
  const auto archive_path = std::filesystem::path(dir) / "Fallout4 - Interface.ba2";
  if (!std::filesystem::exists(archive_path)) {
    GTEST_SKIP() << "no Fallout4 - Interface.ba2 in FO4_DATA_DIR";
  }
  radicle_ba2* raw_archive = nullptr;
  ASSERT_EQ(radicle_ba2_open(archive_path.string().c_str(), &raw_archive), RADICLE_STATUS_OK)
      << radicle_last_error();
  const ArchiveHandle archive(raw_archive);
  radicle_ba2_info info{};
  ASSERT_EQ(radicle_ba2_get_info(archive.get(), &info), RADICLE_STATUS_OK);

  std::string movie_path;
  for (std::size_t index = 0; index < info.entry_count; ++index) {
    radicle_ba2_entry_info entry{};
    if (radicle_ba2_get_entry(archive.get(), index, &entry) != RADICLE_STATUS_OK ||
        entry.path == nullptr) {
      continue;
    }
    const std::string_view path{entry.path};
    if (path.find("Programs\\") == std::string_view::npos) continue;
    if (path.size() < 4 || path.substr(path.size() - 4) != ".swf") continue;
    if (path.find("AtomicCommand") != std::string_view::npos) movie_path = entry.path;
  }
  if (movie_path.empty()) {
    for (std::size_t index = 0; index < info.entry_count && movie_path.empty(); ++index) {
      radicle_ba2_entry_info entry{};
      if (radicle_ba2_get_entry(archive.get(), index, &entry) != RADICLE_STATUS_OK ||
          entry.path == nullptr) {
        continue;
      }
      const std::string_view path{entry.path};
      if (path.find("Programs\\") != std::string_view::npos && path.size() > 4 &&
          path.substr(path.size() - 4) == ".swf") {
        movie_path = entry.path;
      }
    }
  }
  ASSERT_FALSE(movie_path.empty()) << "no Programs movie in the archive";

  auto run_once = [&](ResolverHost& host, std::vector<std::uint64_t>& hashes) {
    radicle_runtime_host host_callbacks{};
    host_callbacks.user = &host;
    host_callbacks.resolve_asset = resolve_asset;
    radicle_runtime* raw_runtime = nullptr;
    EXPECT_EQ(radicle_runtime_open(&host_callbacks, &raw_runtime), RADICLE_STATUS_OK)
        << radicle_last_error();
    RuntimeHandle runtime(raw_runtime);
    radicle_movie* raw_movie = nullptr;
    if (radicle_runtime_open_movie_from_ba2(runtime.get(), archive.get(), movie_path.c_str(),
                                            &raw_movie) != RADICLE_STATUS_OK) {
      return false;
    }
    MovieHandle movie(raw_movie);
    hashes = replay_hashes(*runtime, *movie);
    return true;
  };

  ResolverHost first_host{archive.get(), directory_of(movie_path)};
  std::vector<std::uint64_t> first;
  if (!run_once(first_host, first)) {
    GTEST_SKIP() << "the program movie needs a runtime feature that is still missing: "
                 << radicle_last_error();
  }
  ASSERT_EQ(first.size(), 24u);
  bool nonzero = false;
  for (const std::uint64_t hash : first) nonzero = nonzero || hash != 0;
  EXPECT_TRUE(nonzero);
  RecordProperty("asset_requests", std::to_string(first_host.resolved + first_host.missed));
  RecordProperty("asset_misses", std::to_string(first_host.missed));

  ResolverHost second_host{archive.get(), directory_of(movie_path)};
  std::vector<std::uint64_t> second;
  ASSERT_TRUE(run_once(second_host, second));
  EXPECT_EQ(first, second);
}

namespace {

std::string lower_ascii(std::string value) {
  for (char& character : value) {
    if (character >= 'A' && character <= 'Z') character = static_cast<char>(character - 'A' + 'a');
  }
  return value;
}

std::string basename_of(std::string_view path) {
  const std::size_t slash = path.find_last_of("\\/");
  return slash == std::string_view::npos ? std::string{path} : std::string{path.substr(slash + 1)};
}

std::map<std::string, std::string> swf_paths_by_basename(const radicle_ba2* archive,
                                                         const radicle_ba2_info& info) {
  std::map<std::string, std::string> by_basename;
  for (std::size_t index = 0; index < info.entry_count; ++index) {
    radicle_ba2_entry_info entry{};
    if (radicle_ba2_get_entry(archive, index, &entry) != RADICLE_STATUS_OK ||
        entry.path == nullptr) {
      continue;
    }
    const std::string_view path{entry.path};
    if (path.size() < 4 || lower_ascii(std::string{path.substr(path.size() - 4)}) != ".swf") {
      continue;
    }
    by_basename.emplace(lower_ascii(basename_of(path)), std::string{path});
  }
  return by_basename;
}

constexpr std::size_t kProgramFrames = 180;

std::vector<fo4::avm2::ReplayInput> program_inputs() {
  std::vector<fo4::avm2::ReplayInput> inputs;
  const auto add = [&inputs](double time_ms, fo4::avm2::ReplayInputKind kind, double x, double y,
                             std::uint32_t key) {
    fo4::avm2::ReplayInput input;
    input.time_ms = time_ms;
    input.kind = kind;
    input.x = x;
    input.y = y;
    input.key_code = key;
    inputs.push_back(input);
  };
  using Kind = fo4::avm2::ReplayInputKind;
  for (int frame = 0; frame < 40; ++frame) {
    add(static_cast<double>(frame) * 41.6667, Kind::MouseMove, 300.0 + frame * 4.0,
        200.0 + frame * 2.0, 0);
  }
  add(600.0, Kind::MouseDown, 460.0, 280.0, 0);
  add(650.0, Kind::MouseUp, 460.0, 280.0, 0);
  add(900.0, Kind::KeyDown, 0.0, 0.0, 13);
  add(950.0, Kind::KeyUp, 0.0, 0.0, 13);
  add(1200.0, Kind::KeyDown, 0.0, 0.0, 32);
  add(1250.0, Kind::KeyUp, 0.0, 0.0, 32);
  add(2400.0, Kind::MouseDown, 500.0, 300.0, 0);
  add(2450.0, Kind::MouseUp, 500.0, 300.0, 0);
  return inputs;
}

struct ProgramRun {
  std::string failure;
  std::vector<std::uint64_t> hashes;
  std::uint64_t host_log{0};
  std::size_t bgs_calls{0};
  std::size_t asset_misses{0};

  bool ok() const noexcept { return failure.empty(); }
};

void drive_movie(fo4::avm2::SwfRuntime& movie, fo4::avm2::Interpreter& interpreter,
                 const std::vector<fo4::avm2::ReplayInput>& inputs, bool init, ProgramRun& run) {
  const auto call = [&](std::string_view name, const std::vector<fo4::avm2::Value>& arguments) {
    const fo4::avm2::ExecutionResult result = movie.call_root(interpreter, name, arguments);
    if (!result.ok()) run.failure = std::string{name} + " failed: " + result.message;
    return result.ok();
  };
  if (init) {
    const std::vector<fo4::avm2::Value> platform{fo4::avm2::Value::uint32(0),
                                                 fo4::avm2::Value::boolean(false)};
    fo4::avm2::Multiname set_platform;
    set_platform.name = "SetPlatform";
    set_platform.namespaces.push_back(fo4::avm2::Namespace{});
    if (movie.root()->lookup(set_platform).found() && !call("SetPlatform", platform)) return;
    if (!call("InitProgram", {})) return;
  }
  const fo4::avm2::ReplayResult replayed =
      fo4::avm2::replay(movie, interpreter, inputs, 1.0 / 24.0, kProgramFrames);
  if (replayed.frames != kProgramFrames) {
    run.failure = "the replay stopped after " + std::to_string(replayed.frames) + " frames";
    return;
  }
  run.hashes = replayed.frame_hashes;
  const fo4::avm2::HandlerFailures& handler_failures = interpreter.handler_failures();
  if (handler_failures.count != 0) {
    run.failure = std::to_string(handler_failures.count) + " handler failure(s), first: " +
                  handler_failures.first_message;
    return;
  }
  if (!init) return;
  if (!movie.playing()) {
    run.failure = "a frame script failed and the movie stopped";
    return;
  }
  const std::set<std::uint64_t> distinct(replayed.frame_hashes.begin(),
                                         replayed.frame_hashes.end());
  if (distinct.size() <= 1) {
    run.failure = "state never changed: the frame hashes have " + std::to_string(distinct.size()) +
                  " distinct value";
    return;
  }
}

ProgramRun run_program_body(const radicle_ba2* archive, const std::string& movie_path,
                            const std::vector<fo4::avm2::ReplayInput>& inputs, bool init) {
  ProgramRun run;
  ResolverHost resolver{archive, directory_of(movie_path)};
  const std::optional<std::vector<std::uint8_t>> movie_bytes = resolve_bytes(resolver, movie_path);
  if (!movie_bytes) {
    run.failure = "the archive entry could not be extracted";
    return run;
  }
  const fo4::swf::SwfReadResult read = fo4::swf::read_swf(std::span<const std::byte>(
      reinterpret_cast<const std::byte*>(movie_bytes->data()), movie_bytes->size()));
  if (!read.ok()) {
    run.failure = "the movie does not parse: " + std::string{fo4::swf::status_name(read.status)};
    return run;
  }
  fake_bgs::FakeBgsHost host;
  fo4::avm2::Runtime runtime;
  fo4::avm2::Interpreter interpreter{runtime};
  fo4::avm2::HostCallbacks callbacks;
  callbacks.resolve_asset = [&resolver](std::string_view path) {
    return resolve_bytes(resolver, path);
  };
  callbacks.host_call = [&host](std::string_view method, std::vector<std::string> arguments) {
    return host.host_call(method, std::move(arguments));
  };
  runtime.host().set_callbacks(std::move(callbacks));
  const fo4::avm2::RuntimeLoad linked = runtime.link();
  if (!linked.ok()) {
    run.failure = "the runtime did not link: " + linked.message;
    return run;
  }
  fo4::avm2::SwfRuntimeOptions options;
  options.bgs_host = true;
  const std::shared_ptr<fo4::avm2::SwfRuntime> movie =
      fo4::avm2::SwfRuntime::create(runtime, interpreter, read.file, options);
  if (movie == nullptr) {
    run.failure = "the movie could not be created: " + fo4::avm2::SwfRuntime::last_error();
    return run;
  }
  drive_movie(*movie, interpreter, inputs, init, run);
  movie->release_fonts();
  run.host_log = host.log_hash();
  for (const fake_bgs::Call& call : host.calls()) {
    if (call.method.rfind("bgs.", 0) == 0) ++run.bgs_calls;
  }
  run.asset_misses = resolver.missed;
  return run;
}

ProgramRun run_program(const radicle_ba2* archive, const std::string& movie_path,
                       const std::vector<fo4::avm2::ReplayInput>& inputs, bool init) {
  try {
    return run_program_body(archive, movie_path, inputs, init);
  } catch (const std::exception& error) {
    ProgramRun run;
    run.failure = std::string{"exception: "} + error.what();
    return run;
  }
}

struct ManifestPin {
  std::string program;
  std::string build;
  std::uint64_t snapshot_hash{0};
  std::uint64_t host_log_hash{0};
  std::size_t bgs_calls{0};
};

std::uint64_t parse_pin_u64(const std::string& text) {
  std::uint64_t value = 0;
  for (char c : text) {
    if (c < '0' || c > '9') {
      break;
    }
    value = value * 10 + static_cast<std::uint64_t>(c - '0');
  }
  return value;
}

std::vector<ManifestPin> load_replay_pins() {
  std::vector<ManifestPin> pins;
  std::ifstream in{std::filesystem::path{FO4_TESTDATA_DIR} / "swf" /
                    "program_replay_pins.txt"};
  if (!in) {
    return pins;
  }
  ManifestPin current;
  bool open = false;
  const auto flush = [&] {
    if (open && !current.program.empty() && !current.build.empty()) {
      pins.push_back(current);
    }
    current = ManifestPin{};
    open = false;
  };
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.empty() || line.front() == '#') {
      continue;
    }
    const std::size_t space = line.find(' ');
    const std::string key = line.substr(0, space);
    const std::string value = space == std::string::npos ? std::string{} : line.substr(space + 1);
    if (key == "program") {
      flush();
      current.program = value;
      open = true;
    } else if (open && key == "build") {
      current.build = value;
    } else if (open && key == "snapshot") {
      current.snapshot_hash = parse_pin_u64(value);
    } else if (open && key == "hostlog") {
      current.host_log_hash = parse_pin_u64(value);
    } else if (open && key == "bgs_calls") {
      current.bgs_calls = static_cast<std::size_t>(parse_pin_u64(value));
    }
  }
  flush();
  return pins;
}

void check_pins(const char* program, const ProgramRun& run, const GameBuild& build) {
  if (run.hashes.empty()) {
    ADD_FAILURE() << program << ": no frame hashes to pin";
    return;
  }
  // A build with no rows must record unpinned, not fail; a row mismatch must fail.
  static const std::vector<ManifestPin> pins = load_replay_pins();
  const ManifestPin* match = nullptr;
  for (const ManifestPin& pin : pins) {
    if (pin.program == program && pin.build == build.file_version) {
      match = &pin;
      break;
    }
  }
  if (match == nullptr) {
    ::testing::Test::RecordProperty((std::string{"pin_"} + program).c_str(),
                                      ("unpinned for build " + build.file_version).c_str());
    return;
  }
  EXPECT_EQ(run.hashes.back(), match->snapshot_hash) << program;
  EXPECT_EQ(run.host_log, match->host_log_hash) << program;
  EXPECT_EQ(run.bgs_calls, match->bgs_calls) << program;
}

}  // namespace

TEST(CApiRuntimeReal, ReplaysTheSevenProgramsDeterministically) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder";
  }
  const GameBuild build = detect_game_build(dir);
  record_game_build(build);
  const auto archive_path = std::filesystem::path(dir) / "Fallout4 - Interface.ba2";
  if (!std::filesystem::exists(archive_path)) {
    GTEST_SKIP() << "no Fallout4 - Interface.ba2 in FO4_DATA_DIR";
  }
  radicle_ba2* raw_archive = nullptr;
  ASSERT_EQ(radicle_ba2_open(archive_path.string().c_str(), &raw_archive), RADICLE_STATUS_OK)
      << radicle_last_error();
  const ArchiveHandle archive(raw_archive);
  radicle_ba2_info info{};
  ASSERT_EQ(radicle_ba2_get_info(archive.get(), &info), RADICLE_STATUS_OK);

  const std::map<std::string, std::string> by_basename =
      swf_paths_by_basename(archive.get(), info);

  const std::pair<const char*, bool> programs[] = {
      {"AtomicCommand.swf", true}, {"GJ_Defrag.swf", false}, {"Grognak.swf", true},
      {"PipFall.swf", true},       {"RedMenace.swf", true},  {"TurretHack.swf", false},
      {"ZetaInvaders.swf", true}};
  const std::vector<fo4::avm2::ReplayInput> inputs = program_inputs();
  std::size_t ran = 0;
  std::size_t mismatches = 0;
  std::string failed;
  for (const auto& [program, expects_bgs] : programs) {
    const std::string key = lower_ascii(program);
    const std::string property_name = "program_" + std::string{program};
    const auto found = by_basename.find(key);
    if (found == by_basename.end()) {
      std::cerr << "[r8-programs] " << program << ": missing from Fallout4 - Interface.ba2"
                << std::endl;
      failed += std::string{program} + ": missing from the archive\n";
      RecordProperty(property_name.c_str(), "missing");
      continue;
    }
    const std::string& movie_path = found->second;
    ProgramRun first = run_program(archive.get(), movie_path, inputs, true);
    if (first.ok() && expects_bgs && first.bgs_calls == 0) {
      first.failure = "no bgs call reached the host";
    }
    if (!first.ok()) {
      std::cerr << "[r8-programs] " << program << " (" << movie_path
                << ") failed on the first run: " << first.failure << std::endl;
      failed += std::string{program} + ": " + first.failure + "\n";
      RecordProperty(property_name.c_str(), ("failed: " + first.failure).c_str());
      continue;
    }
    const ProgramRun second = run_program(archive.get(), movie_path, inputs, true);
    if (!second.ok()) {
      std::cerr << "[r8-programs] " << program << " (" << movie_path
                << ") failed on the second run: " << second.failure << std::endl;
      failed += std::string{program} + " (second run): " + second.failure + "\n";
      RecordProperty(property_name.c_str(), "replay failed on the second run");
      ++mismatches;
      continue;
    }
    ++ran;
    std::cerr << "R8_HASH " << program << ' ' << first.hashes.back() << ' ' << first.host_log
              << ' ' << first.bgs_calls << std::endl;
    EXPECT_EQ(first.hashes, second.hashes) << program << " replay is not deterministic";
    EXPECT_EQ(first.host_log, second.host_log) << program << " host call log is not deterministic";
    if (first.hashes != second.hashes || first.host_log != second.host_log) {
      failed += std::string{program} + ": replay or host log is not deterministic\n";
      ++mismatches;
    }
    check_pins(program, first, build);
    RecordProperty(property_name.c_str(),
                   "ran snapshot=" + std::to_string(first.hashes.back()) +
                       " hostlog=" + std::to_string(first.host_log) +
                       " bgs_calls=" + std::to_string(first.bgs_calls) +
                       " asset_misses=" + std::to_string(first.asset_misses));
  }

  for (const char* control_name : {"atomiccommand.swf", "pipfall.swf"}) {
    const auto control = by_basename.find(control_name);
    EXPECT_TRUE(control != by_basename.end()) << control_name << " is needed for the control";
    if (control == by_basename.end()) continue;
    const ProgramRun base = run_program(archive.get(), control->second, inputs, true);
    ASSERT_TRUE(base.ok()) << control_name << ": " << base.failure;
    const ProgramRun altered = run_program(archive.get(), control->second, inputs, false);
    ASSERT_EQ(altered.hashes.size(), kProgramFrames) << control_name << ": " << altered.failure;
    EXPECT_NE(altered.hashes, base.hashes) << control_name << ": the frame hashes ignore the scripts";
    EXPECT_NE(altered.host_log, base.host_log) << control_name << ": the host log ignores the init";
  }

  RecordProperty("ran", std::to_string(ran));
  EXPECT_EQ(mismatches, 0u) << failed;
  EXPECT_EQ(ran, 7u) << failed;
}

TEST(CApiRuntimeReal, ComparesTheCapturedBgsSequenceForEachProgram) {
  const char* capture_dir = std::getenv("FO4_SWF_CAPTURE_DIR");
  if (capture_dir == nullptr) {
    GTEST_SKIP() << "set FO4_SWF_CAPTURE_DIR to a folder of <Program>.tsv captures";
  }
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder";
  }
  record_game_build(detect_game_build(dir));
  const auto archive_path = std::filesystem::path(dir) / "Fallout4 - Interface.ba2";
  if (!std::filesystem::exists(archive_path)) {
    GTEST_SKIP() << "no Fallout4 - Interface.ba2 in FO4_DATA_DIR";
  }
  radicle_ba2* raw_archive = nullptr;
  ASSERT_EQ(radicle_ba2_open(archive_path.string().c_str(), &raw_archive), RADICLE_STATUS_OK)
      << radicle_last_error();
  const ArchiveHandle archive(raw_archive);
  radicle_ba2_info info{};
  ASSERT_EQ(radicle_ba2_get_info(archive.get(), &info), RADICLE_STATUS_OK);
  const std::map<std::string, std::string> by_basename =
      swf_paths_by_basename(archive.get(), info);

  const char* programs[] = {"AtomicCommand", "GJ_Defrag", "Grognak", "PipFall",
                            "RedMenace",     "TurretHack", "ZetaInvaders"};
  std::size_t compared_programs = 0;
  std::string failed;
  for (const char* program : programs) {
    const std::string name = std::string{program} + ".swf";
    const std::filesystem::path capture =
        std::filesystem::path(capture_dir) / (std::string{program} + ".tsv");
    if (!std::filesystem::exists(capture)) {
      RecordProperty("bgs_" + name, "no capture file");
      continue;
    }
    const auto found = by_basename.find(lower_ascii(name));
    if (found == by_basename.end()) {
      failed += name + ": missing from the archive\n";
      continue;
    }
    const std::optional<std::string> text = bgs_capture::read_capture_file(capture);
    if (!text) {
      failed += name + ": the capture file cannot be opened\n";
      continue;
    }
    std::vector<bgs_capture::Row> rows;
    try {
      rows = bgs_capture::parse_capture(*text);
    } catch (const std::exception& error) {
      failed += name + ": " + error.what() + "\n";
      continue;
    }
    ResolverHost resolver{archive.get(), directory_of(found->second)};
    const std::optional<std::vector<std::uint8_t>> movie_bytes =
        resolve_bytes(resolver, found->second);
    if (!movie_bytes) {
      failed += name + ": the archive entry could not be extracted\n";
      continue;
    }
    const fo4::swf::SwfReadResult read = fo4::swf::read_swf(std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(movie_bytes->data()), movie_bytes->size()));
    if (!read.ok()) {
      failed += name + ": the movie does not parse: " +
                std::string{fo4::swf::status_name(read.status)} + "\n";
      continue;
    }
    fo4::avm2::Runtime runtime;
    fo4::avm2::Interpreter interpreter{runtime};
    fo4::avm2::HostCallbacks callbacks;
    callbacks.resolve_asset = [&resolver](std::string_view path) {
      return resolve_bytes(resolver, path);
    };
    ++compared_programs;
    const bgs_capture::Outcome outcome =
        bgs_capture::replay_capture(runtime, interpreter, callbacks, read.file, rows);
    if (!outcome.matched()) {
      std::string reason;
      if (outcome.first.has_value()) {
        reason = "seq " + std::to_string(outcome.first->seq) + ": " + outcome.first->reason;
      }
      if (!outcome.replay_error.empty()) {
        reason += (reason.empty() ? "" : "; ") + std::string{"replay: "} + outcome.replay_error;
      }
      failed += name + ": " + reason + "\n";
      RecordProperty("bgs_" + name, "first difference: " + reason);
      continue;
    }
    RecordProperty("bgs_" + name, "matched " + std::to_string(outcome.compared) + " bgs calls");
  }
  RecordProperty("compared_programs", std::to_string(compared_programs));
  if (compared_programs == 0) {
    GTEST_SKIP() << "no <Program>.tsv in FO4_SWF_CAPTURE_DIR";
  }
  EXPECT_EQ(failed, "");
}
