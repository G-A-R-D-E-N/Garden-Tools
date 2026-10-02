#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include <cstdint>
#include <exception>
#include <string>

#include <fo4/plugin/visit.hpp>

namespace fo4ide::plugin {
namespace {

// source:Core/wbDefinitionsFO4.pas (dev-4.1.6) record flag bit 11 'Initially Disabled'; must match RadiclePluginMaintenance.cpp.
constexpr std::uint32_t kCopyInitiallyDisabled = 0x00000800u;

}  // namespace

Result<nlohmann::json> RadiclePluginBackend::copyAsDisabledOverride(const nlohmann::json& arguments) {
    auto copied = copyAsOverride(arguments);
    if (!copied || !copied->is_string()) return copied;
    const auto& text = copied->get_ref<const std::string&>();
    if (text.starts_with("LOSES:") || text.starts_with("EXISTS:")) return copied;

    const auto key = fo4::FormKey::parse(arguments.at(1).get<std::string>());
    const auto targetName = arguments.at(2).get<std::string>();

    std::scoped_lock lock(mutex_);
    auto* target = findPlugin(targetName);
    if (target == nullptr) {
        return std::unexpected(Error{"TargetPluginMissing", "Target plugin is not loaded: " + targetName});
    }
    try {
        auto mutated = target->plugin;
        bool found = false;
        fo4::visit_records(mutated, [&](fo4::Record& record) {
            if (record.header.signature == fo4::sig_tes4 || record.form_key != key) return;
            record.header.flags = fo4::RecordFlags{record.header.flags.value() | kCopyInitiallyDisabled};
            record.original_range.clear();
            record.decompressed_payload.clear();
            record.header.data_size = 0;
            record.uncompressed_size = 0;
            found = true;
        });
        if (!found) {
            return std::unexpected(Error{"RecordNotFound", "Copied override not found in " + targetName});
        }
        mutated.original_range.clear();
        stagePluginMutationLocked(*target, std::move(mutated));
        return nlohmann::json(copied->get<std::string>() + " Marked it Initially Disabled.");
    } catch (const std::exception& error) {
        return std::unexpected(Error{"CopyAsDisabledOverrideFailed", error.what()});
    }
}

}  // namespace fo4ide::plugin
