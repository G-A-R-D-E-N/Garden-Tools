#include <fo4ide/archive/ArchiveService.hpp>

namespace fo4ide::archive {

Result<nlohmann::json> NullArchiveBackend::invoke(
    std::string_view,
    const nlohmann::json&) {
    return std::unexpected(Error{
        "ArchiveBackendUnavailable",
        "No archive backend is connected"
    });
}

}
