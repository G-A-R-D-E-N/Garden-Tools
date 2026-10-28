#pragma once

#include <filesystem>
#include <string>

#include <fo4ide/core/Result.hpp>

namespace fo4ide::assets {

Result<std::string> renderGeometry(const std::filesystem::path& nifPath);
Result<std::string> renderInspect(const std::filesystem::path& nifPath);

}
