#include <fo4ide/logging/Logger.hpp>

#include <mutex>
#include <unordered_map>

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace fo4ide::logging {
namespace {

std::mutex gMutex;
std::shared_ptr<spdlog::sinks::rotating_file_sink_mt> gSink;
std::unordered_map<std::string, std::shared_ptr<spdlog::logger>> gLoggers;
spdlog::level::level_enum gLevel = spdlog::level::info;

}

Result<void> initialize(const Config& config) {
    try {
        std::scoped_lock lock(gMutex);
        std::filesystem::create_directories(config.directory);
        const auto path = config.directory / config.filename;
        gSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            path.string(), config.maxBytes, config.maxFiles);
        gLevel = config.level;
        gLoggers.clear();
        return {};
    } catch (const std::exception& ex) {
        return std::unexpected(Error{"LoggingInitFailed", ex.what()});
    }
}

void shutdown() {
    std::scoped_lock lock(gMutex);
    for (auto& [name, logger] : gLoggers) {
        static_cast<void>(name);
        logger->flush();
    }
    gLoggers.clear();
    gSink.reset();
    spdlog::drop_all();
}

std::shared_ptr<spdlog::logger> get(std::string_view category) {
    std::scoped_lock lock(gMutex);
    const std::string key(category);
    if (const auto it = gLoggers.find(key); it != gLoggers.end()) return it->second;

    std::shared_ptr<spdlog::logger> logger;
    if (gSink) {
        logger = std::make_shared<spdlog::logger>(key, gSink);
    } else {
        logger = spdlog::stderr_color_mt(key);
    }
    logger->set_level(gLevel);
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%l] %v");
    gLoggers.emplace(key, logger);
    return logger;
}

}
