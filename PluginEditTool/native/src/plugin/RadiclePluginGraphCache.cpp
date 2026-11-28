#include "RadiclePluginSupport.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <exception>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif
#include <malloc.h>

namespace fo4ide::plugin {

RadiclePluginBackend::RadiclePluginBackend() {
    if (const char* configured = std::getenv("FO4IDE_GRAPH_IDLE_SECONDS"); configured != nullptr) {
        try {
            const auto seconds = std::stoll(configured);
            if (seconds >= 2) graphIdleLimit_ = std::chrono::seconds(seconds);
        } catch (const std::exception&) {
        }
    }
    graphReaper_ = std::jthread([this](std::stop_token stop_token) {
        reapIdleConflictGraph(stop_token);
    });
}

RadiclePluginBackend::~RadiclePluginBackend() = default;

#ifdef FO4IDE_TEST_HOOKS
std::weak_ptr<const fo4::ConflictGraph> RadiclePluginBackend::cachedGraphForTest() const {
    std::scoped_lock lock(graphMutex_);
    return graphStorage_;
}
#endif

void RadiclePluginBackend::reapIdleConflictGraph(std::stop_token stop_token) {
    std::mutex waitMutex;
    std::condition_variable_any waitCv;
    while (!stop_token.stop_requested()) {
        {
            std::unique_lock lock(waitMutex);
            waitCv.wait_for(lock, stop_token, std::chrono::seconds(10), [] { return false; });
        }
        if (stop_token.stop_requested()) return;
        {
            std::scoped_lock peekLock(graphMutex_);
            if (!graphStorage_ || std::chrono::steady_clock::now() - graphLastUse_ < graphIdleLimit_) continue;
        }
        bool dropped = false;
        {
            std::unique_lock<std::shared_mutex> stateLock(stateMutex_);
            std::scoped_lock graphLock(graphMutex_);
            if (graphStorage_ &&
                std::chrono::steady_clock::now() - graphLastUse_ >= graphIdleLimit_) {
                if (graphStorage_->references_built()) {
                    graphStorage_->release_references();
                    graphLastUse_ = std::chrono::steady_clock::now();
                } else {
                    graphStorage_.reset();
                    graphDirty_ = true;
                }
                dropped = true;
            }
        }
        if (dropped) {
#ifdef _WIN32
            _heapmin();
            HeapCompact(GetProcessHeap(), 0);
#else
            malloc_trim(0);
#endif
        }
    }
}

void RadiclePluginBackend::dropConflictGraphLocked() const {
    std::scoped_lock lock(graphMutex_);
    graphStorage_.reset();
    graphDirty_ = true;
}

}
