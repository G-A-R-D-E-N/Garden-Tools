#pragma once

#include <filesystem>
#include <map>
#include <string>

#include <nlohmann/json.hpp>

#include <fo4/material/material_file.hpp>
#include <fo4ide/core/Result.hpp>

namespace fo4ide::material {

class MaterialService {
public:
    Result<fo4::material::MaterialFile> load(const std::filesystem::path& path) const;
    Result<nlohmann::json> inspect(const std::filesystem::path& path) const;
    Result<std::string> setFields(const std::filesystem::path& path,
                                  const std::map<std::string, std::string, std::less<>>& fields,
                                  const std::filesystem::path& output = {}) const;
};

}
