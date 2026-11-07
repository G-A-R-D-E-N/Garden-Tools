#include "env_bench/env_bench.hpp"

#include "fo4/archive/assets.hpp"
#include "fo4/environment/environment.hpp"
#include "fo4/environment/strings.hpp"

#include <chrono>
#include <string>
#include <variant>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <psapi.h>
#else
#include <fstream>
#include <sys/resource.h>
#include <unistd.h>
#endif

namespace radicle_bench {
namespace {

struct Memory {
  std::uint64_t current_kb{0};
  std::uint64_t peak_kb{0};
};

Memory sample_memory() {
  Memory memory;
#ifdef _WIN32
  PROCESS_MEMORY_COUNTERS counters{};
  if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) {
    memory.current_kb = counters.WorkingSetSize / 1024;
    memory.peak_kb = counters.PeakWorkingSetSize / 1024;
  }
#else
  rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) == 0) memory.peak_kb = static_cast<std::uint64_t>(usage.ru_maxrss);
  std::ifstream statm("/proc/self/statm");
  std::uint64_t size_pages = 0;
  std::uint64_t resident_pages = 0;
  if (statm >> size_pages >> resident_pages) {
    memory.current_kb = resident_pages * static_cast<std::uint64_t>(sysconf(_SC_PAGESIZE)) / 1024;
  }
#endif
  return memory;
}

void tally(const fo4::Group& group, EnvBenchResult& result) {
  ++result.group_count;
  result.node_bytes += group.children.capacity() * sizeof(fo4::GroupChild);
  for (const auto& child : group.children) {
    if (const auto* nested = std::get_if<fo4::Group>(&child)) {
      tally(*nested, result);
      continue;
    }
    const auto& record = std::get<fo4::Record>(child);
    result.payload_bytes += record.decompressed_payload.capacity();
    result.node_bytes += record.subrecords.capacity() * sizeof(fo4::Subrecord);
    for (const auto& subrecord : record.subrecords) {
      ++result.subrecord_count;
      result.subrecord_bytes += subrecord.data.capacity();
    }
  }
}

template <typename F>
double time_ms(F&& body) {
  const auto start = std::chrono::steady_clock::now();
  body();
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

}  // namespace

std::vector<std::filesystem::path> default_masters(const std::filesystem::path& data_dir) {
  std::vector<std::filesystem::path> paths;
  for (const char* name : {"Fallout4.esm", "DLCRobot.esm", "DLCworkshop01.esm", "DLCCoast.esm",
                           "DLCworkshop02.esm", "DLCworkshop03.esm", "DLCNukaWorld.esm"}) {
    auto path = data_dir / name;
    if (std::filesystem::exists(path)) paths.push_back(std::move(path));
  }
  return paths;
}

EnvBenchResult run_env_bench(const std::filesystem::path& data_dir,
                             const std::vector<std::filesystem::path>& plugins) {
  EnvBenchResult result;
  for (const auto& path : plugins) result.plugin_bytes += std::filesystem::file_size(path);

  result.rss_before_load_kb = sample_memory().current_kb;
  fo4::env::Environment env;
  result.load_ms = time_ms([&] { env = fo4::env::Environment::load(plugins); });
  const Memory after = sample_memory();
  result.rss_after_load_kb = after.current_kb;
  result.peak_rss_after_load_kb = after.peak_kb;
  result.plugin_count = env.plugins().size();
  for (std::size_t plugin = 0; plugin < result.plugin_count; ++plugin) {
    const auto& loaded = env.plugin(plugin);
    if (loaded.original_data) result.retained_file_bytes += loaded.original_data->size();
    for (const auto& group : loaded.groups) tally(group, result);
  }

  fo4::archive::AssetResolver assets;
  result.archives_ms = time_ms([&] {
    const std::vector<std::string> vanilla{"Fallout4.esm"};
    const auto archives = fo4::archive::archive_load_order(data_dir, {}, vanilla);
    assets = fo4::archive::AssetResolver::open({}, archives);
  });
  result.archive_count = assets.archives().size();

  result.strings_ms = time_ms([&] {
    static_cast<void>(fo4::env::StringTables::load(env, data_dir / "Strings", "en", &assets));
  });

  result.walk_ms = time_ms([&] {
    for (std::size_t plugin = 0; plugin < result.plugin_count; ++plugin) {
      for (const auto* ref : env.plugin_records(plugin)) {
        if (ref->record != nullptr) ++result.record_count;
      }
    }
  });
  return result;
}

void print(std::ostream& out, const EnvBenchResult& result) {
  out << "plugins:              " << result.plugin_count << "\n"
      << "plugin bytes:         " << result.plugin_bytes << "\n"
      << "records:              " << result.record_count << "\n"
      << "archives:             " << result.archive_count << "\n"
      << "rss before load (KB): " << result.rss_before_load_kb << "\n"
      << "rss after load (KB):  " << result.rss_after_load_kb << "\n"
      << "peak rss (KB):        " << result.peak_rss_after_load_kb << "\n"
      << "Environment::load ms: " << result.load_ms << "\n"
      << "archives open ms:     " << result.archives_ms << "\n"
      << "StringTables::load ms: " << result.strings_ms << "\n"
      << "record walk ms:       " << result.walk_ms << "\n"
      << "retained file bytes:  " << result.retained_file_bytes << "\n"
      << "payload bytes:        " << result.payload_bytes << "\n"
      << "subrecord bytes:      " << result.subrecord_bytes << "\n"
      << "subrecords:           " << result.subrecord_count << "\n"
      << "groups:               " << result.group_count << "\n"
      << "node bytes:           " << result.node_bytes << "\n";
}

}  // namespace radicle_bench
