#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <fo4ide/core/Result.hpp>

namespace fo4ide::assets {

Result<std::string> renderGeometry(const std::filesystem::path& nifPath);
Result<std::string> renderInspect(const std::filesystem::path& nifPath);
Result<std::string> renderTree(const std::filesystem::path& nifPath);
Result<std::string> renderVerify(const std::filesystem::path& nifPath);

}
