#include <fo4ide/hkx/HkxService.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <fo4/hkx/convert_packfile.hpp>
#include <fo4/io/sha256.hpp>

namespace fo4ide::hkx {
namespace {

using Json = nlohmann::json;

Json diagnosticsJson(const std::vector<fo4::hkx::HkxConversionDiagnostic>& items) {
    auto out = Json::array();
    for (const auto& item : items) {
        const char* level = item.level == fo4::hkx::HkxDiagnosticLevel::Error     ? "error"
                            : item.level == fo4::hkx::HkxDiagnosticLevel::Warning ? "warning"
                                                                                  : "info";
        out.push_back({{"level", level}, {"object", item.object_id}, {"member", item.member}, {"message", item.message}});
    }
    return out;
}

Json changesJson(const std::vector<fo4::hkx::HkxFieldChange>& items) {
    auto out = Json::array();
    for (const auto& item : items) {
        out.push_back({{"object", item.object},
                       {"class", item.class_name},
                       {"member", item.member},
                       {"before", item.before},
                       {"after", item.after}});
    }
    return out;
}

Json conversionJson(const fo4::hkx::HkxPackfileConversion& result) {
    const auto& report = result.report;
    return {{"ok", result.status == fo4::hkx::HkxPackfileConvertStatus::Ok},
            {"status", std::string(fo4::hkx::packfile_convert_status_name(result.status))},
            {"reason", result.reason},
            {"source_version", result.source_version},
            {"target_version", result.target_version},
            {"same_version", result.same_version},
            {"evidence", result.evidence},
            {"objects", result.object_count},
            {"exact_objects", report.exact_objects},
            {"patched_objects", report.patched_objects},
            {"defaulted_fields", report.defaulted_fields},
            {"dropped_fields", report.dropped_fields},
            {"dropped_references", report.dropped_references},
            {"enum_mapped_fields", report.enum_mapped_fields},
            {"unsupported_enum_values", report.unsupported_enum_values},
            {"unsupported_objects", report.unsupported_objects},
            {"changed_total", result.changed_total},
            {"changes", changesJson(result.changes)},
            {"opaque_classes", result.opaque_classes},
            {"diagnostics", diagnosticsJson(result.diagnostics)},
            {"source_pointer_size", result.source_pointer_size},
            {"target_pointer_size", result.target_pointer_size},
            {"relaid_out", result.relaid_out},
            {"derived_table_consistency_checked", result.derived_table_consistency_checked},
            {"narrow_layout_measured", result.narrow_layout_measured},
            {"round_trip_checked", result.round_trip_checked},
            {"fields_compared", result.fields_compared},
            {"relayout_blocks", result.relayout_blocks},
            {"relayout_classes", result.relayout_classes},
            {"blocked_class", result.block.class_name},
            {"blocked_member", result.block.member},
            {"bytes", result.bytes.size()}};
}

}  // namespace

// Invariant: an empty outPath is a preview that writes nothing; a write needs a distinct outPath and a conversion that ended Ok.
Result<nlohmann::json> HkxService::convertAsset(const std::filesystem::path& path, std::string_view targetVersion,
                                                const std::filesystem::path& outPath,
                                                std::string_view expectedSource, bool overwrite, int pointerSize) const {
    if (path.empty()) return std::unexpected(Error{"InvalidPath", "HKX path is empty"});
    if (pointerSize != 0 && pointerSize != 4 && pointerSize != 8) {
        return std::unexpected(Error{"InvalidPointerSize", "pointer_size must be 4 or 8"});
    }
    if (!std::filesystem::is_regular_file(path)) {
        return std::unexpected(Error{"FileNotFound", "HKX not found: " + path.string()});
    }
    if (!outPath.empty()) {
        if (auto allowed = checkSaveTarget(outPath, "A converted packfile"); !allowed) return std::unexpected(allowed.error());
        std::error_code same;
        if (std::filesystem::equivalent(path, outPath, same)) {
            return std::unexpected(Error{"HkxConvertInPlace", "Not saved: out_path is the source file; convert to a different path"});
        }
    }

    auto read = readHkxBytes(path);
    if (!read) return std::unexpected(read.error());
    const std::vector<std::byte>& source = *read;
    const auto digest = fo4::io::sha256_hex(source);
    if (!expectedSource.empty() && digest != expectedSource) {
        return std::unexpected(Error{"HkxChanged", "Not saved: " + path.string() +
                                                       " changed on disk after it was loaded -- reload it before converting"});
    }

    std::string target(targetVersion);
    if (target.empty()) {
        if (const auto probed = fo4::hkx::probe_havok_version(source)) target = probed->version;
    }
    if (pointerSize != 0) {
        const auto probed = fo4::hkx::probe_havok_version(source);
        if (probed && !target.empty() && target != probed->version) {
            return std::unexpected(Error{"InvalidTarget", "pointer_size never changes the Havok version; leave target_version empty or equal to " +
                                                              probed->version});
        }
    }
    const auto result = pointerSize != 0 ? fo4::hkx::convert_packfile_pointer_size(source, static_cast<std::uint8_t>(pointerSize))
                                         : fo4::hkx::convert_packfile(source, target);

    Json reply = conversionJson(result);
    reply["source"] = digest;
    reply["saved"] = false;
    if (outPath.empty()) return reply;

    if (result.status != fo4::hkx::HkxPackfileConvertStatus::Ok) {
        return std::unexpected(Error{"HkxConvertRefused",
                                     "Not saved: " + std::string(fo4::hkx::packfile_convert_status_name(result.status)) + ": " +
                                         result.reason});
    }
    if (auto published = publishHkxBytes(outPath, result.bytes, overwrite, "HkxSaveFailed"); !published) {
        return std::unexpected(published.error());
    }
    reply["saved"] = true;
    reply["out"] = outPath.string();
    return reply;
}

}  // namespace fo4ide::hkx
