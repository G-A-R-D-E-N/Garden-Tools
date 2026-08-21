#include <fo4ide/archive/ArchiveService.hpp>
#include <fo4ide/archive/RadicleArchiveBackend.hpp>

#include <memory>
#include <utility>

namespace fo4ide::archive {

NullArchiveBackend::NullArchiveBackend()
    : backend_(std::make_unique<RadicleArchiveBackend>()) {}

NullArchiveBackend::~NullArchiveBackend() = default;
NullArchiveBackend::NullArchiveBackend(NullArchiveBackend&&) noexcept = default;
NullArchiveBackend& NullArchiveBackend::operator=(NullArchiveBackend&&) noexcept = default;

bool NullArchiveBackend::available() const noexcept {
    return backend_ != nullptr && backend_->available();
}

Result<nlohmann::json> NullArchiveBackend::invoke(
    std::string_view operation,
    const nlohmann::json& arguments) {
    if (backend_ == nullptr) {
        return std::unexpected(Error{
            "ArchiveBackendUnavailable",
            "Radicle archive backend is not initialized"
        });
    }
    return backend_->invoke(operation, arguments);
}

}
