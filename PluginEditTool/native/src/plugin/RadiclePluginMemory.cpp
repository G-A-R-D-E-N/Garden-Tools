#include "RadiclePluginSupport.hpp"

#include <cstdint>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <malloc.h>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")
#else
#include <malloc.h>
#endif

namespace fo4ide::plugin {
namespace {

std::uint64_t workingSetBytes() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS counters{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) {
        return static_cast<std::uint64_t>(counters.WorkingSetSize);
    }
    return 0;
#else
    return 0;
#endif
}

}  // namespace

Result<nlohmann::json> RadiclePluginBackend::releaseMemory(const nlohmann::json& arguments) {
    bool dropGraph = false;
    if (arguments.is_array() && !arguments.empty() && arguments[0].is_boolean()) {
        dropGraph = arguments[0].get<bool>();
    }
    std::string dropped;
    if (dropGraph) {
        StateReleased released;
        std::unique_lock<std::shared_mutex> exclusive(stateMutex_);
        dropConflictGraphLocked();
        dropped = " Dropped the cached conflict graph; it rebuilds on the next conflict or reference query.";
    }
    const auto before = workingSetBytes();
#ifdef _WIN32
    // Constraint: only free pages and page the working set out; plugin data must stay loaded.
    _heapmin();
    HeapCompact(GetProcessHeap(), 0);
    SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1));
#else
    malloc_trim(0);
#endif
    const auto after = workingSetBytes();
    return nlohmann::json(
        "Trimmed the process working set: " + std::to_string(before / 1048576) + " MB -> " +
        std::to_string(after / 1048576) +
        " MB. Plugin data stays loaded and pages back in on the next access." + dropped);
}

}  // namespace fo4ide::plugin
