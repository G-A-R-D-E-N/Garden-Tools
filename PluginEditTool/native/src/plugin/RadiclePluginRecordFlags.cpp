#include "RadiclePluginSupport.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <string>

#include <fo4/forms/record_flags.hpp>
#include <fo4/plugin/reader.hpp>

namespace fo4ide::plugin {

namespace {

constexpr std::uint32_t kVisibleWhenDistant = 0x00008000u;

}

Result<nlohmann::json> RadiclePluginBackend::setRecordFlags(const nlohmann::json& arguments) {
    const auto name = support::stringArgument(arguments, 0);
    if (name.empty()) return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    const auto key = support::formKeyArgument(support::stringArgument(arguments, 1));
    if (!key) return std::unexpected(key.error());

    const std::array<std::uint32_t, 4> bits{
        fo4::RecordFlags::persistent,
        kVisibleWhenDistant,
        fo4::RecordFlags::initially_disabled,
        fo4::RecordFlags::deleted,
    };
    for (std::size_t index = 0; index < bits.size(); ++index) {
        const auto value = support::argumentAt(arguments, index + 2);
        if (!value.is_null() && !value.is_boolean()) {
            return std::unexpected(Error{
                "InvalidRecordFlag",
                "Record flag arguments must be boolean or omitted"
            });
        }
    }

    OpLock lock(*this, OpLock::Mode::Write, {name});
    auto* loaded = findPlugin(name);
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + name});
    }
    if (const auto guard = writeGuard(*loaded)) return std::unexpected(*guard);

    try {
        const auto* record = fo4::find_record(loaded->plugin, *key);
        if (record == nullptr) {
            return std::unexpected(Error{
                "RecordMissing",
                "Record " + key->to_string() + " is not in " + name
            });
        }
        auto flags = record->header.flags.value();
        for (std::size_t index = 0; index < bits.size(); ++index) {
            const auto value = support::argumentAt(arguments, index + 2);
            if (!value.is_boolean()) continue;
            flags = value.get<bool>() ? (flags | bits[index]) : (flags & ~bits[index]);
        }

        auto mutated = loaded->plugin;
        switch (fo4::set_record_flags(mutated, *key, flags)) {
            case fo4::RecordFlagStatus::changed:
                stagePluginMutationLocked(*loaded, std::move(mutated));
                return nlohmann::json(
                    "Set the record flags of " + key->to_string() + " in " + name +
                    ". SavePlugin to persist it.");
            case fo4::RecordFlagStatus::unchanged:
                return nlohmann::json(
                    "Record flags of " + key->to_string() + " in " + name + " are already set.");
            case fo4::RecordFlagStatus::record_missing:
                return std::unexpected(Error{
                    "RecordMissing",
                    "Record " + key->to_string() + " is not in " + name
                });
            case fo4::RecordFlagStatus::header_record:
                return std::unexpected(Error{
                    "HeaderRecordFlags",
                    "The plugin header record flags cannot be set here: " + name
                });
            case fo4::RecordFlagStatus::deleted_bit:
                return std::unexpected(Error{
                    "RecordFlagsDeletedBit",
                    "The deleted flag of " + key->to_string() + " in " + name +
                        " only follows delete and undelete operations"
                });
            case fo4::RecordFlagStatus::target_cell_missing:
                return std::unexpected(Error{
                    "TargetCellMissing",
                    "The cell of " + key->to_string() + " in " + name +
                        " is missing, so the persistent flag cannot move the reference"
                });
        }
    } catch (const std::exception& error) {
        return std::unexpected(Error{"SetRecordFlagsFailed", error.what()});
    }
    return std::unexpected(Error{"SetRecordFlagsFailed", "Unhandled record flag status"});
}

}
