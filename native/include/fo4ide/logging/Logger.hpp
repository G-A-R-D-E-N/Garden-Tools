#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

#include <spdlog/logger.h>
#include <spdlog/spdlog.h>

#include <fo4ide/core/Result.hpp>

namespace fo4ide::logging {

struct Config {
    std::filesystem::path directory;
    std::string filename{"FO4IDE.log"};
    std::size_t maxBytes{5 * 1024 * 1024};
    std::size_t maxFiles{3};
    spdlog::level::level_enum level{spdlog::level::info};
};

Result<void> initialize(const Config& config);
void shutdown();
std::shared_ptr<spdlog::logger> get(std::string_view category);

}
