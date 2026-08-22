#pragma once

#include <fo4ide/archive/ArchiveService.hpp>

namespace fo4ide::archive {

class RadicleArchiveBackend final : public IArchiveBackend {
public:
    bool available() const noexcept override { return true; }
    Result<nlohmann::json> invoke(std::string_view operation,
                                  const nlohmann::json& arguments) override;
};

}
