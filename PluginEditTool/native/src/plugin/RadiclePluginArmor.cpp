#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include <exception>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include <fo4/forms/armor.hpp>
#include <fo4/plugin/writer.hpp>

namespace fo4ide::plugin {
namespace {

std::string armorStringArgument(const nlohmann::json& args, std::size_t index) {
    if (!args.is_array() || index >= args.size() || !args[index].is_string()) return {};
    return args[index].get<std::string>();
}

}

Result<nlohmann::json> RadiclePluginBackend::setArmorField(
    const nlohmann::json& arguments) {
    const auto pluginName = armorStringArgument(arguments, 0);
    const auto formKeyText = armorStringArgument(arguments, 1);
    const auto field = armorStringArgument(arguments, 2);
    const auto value = armorStringArgument(arguments, 3);
    if (pluginName.empty()) {
        return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    }
    if (formKeyText.empty()) {
        return std::unexpected(Error{"FormKeyMissing", "Record FormKey is empty"});
    }
    if (value.find('\0') != std::string::npos) {
        return std::unexpected(Error{
            "InvalidFieldValue",
            "Armor model and icon fields cannot contain embedded NUL bytes"
        });
    }

    const bool armo = field.starts_with("ARMO\\");
    const bool arma = field.starts_with("ARMA\\");
    if (!armo && !arma) {
        return std::unexpected(Error{
            "FieldWriteUnsupported",
            "Field is not a supported ARMO or ARMA model path: " + field
        });
    }

    fo4::FormKey key;
    try {
        key = fo4::FormKey::parse(formKeyText);
    } catch (const std::exception& error) {
        return std::unexpected(Error{"InvalidFormKey", error.what()});
    }

    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) {
        return std::unexpected(Error{
            "PluginNotLoaded",
            "Plugin is not loaded: " + pluginName
        });
    }
    if (const auto error = writeGuard(*loaded)) return std::unexpected(*error);

    try {
        auto mutated = loaded->plugin;
        bool handled = false;
        bool found = false;

        if (armo) {
            std::optional<std::string_view> maleWorld;
            std::optional<std::string_view> maleIcon;
            std::optional<std::string_view> maleMessage;
            std::optional<std::string_view> femaleWorld;
            std::optional<std::string_view> femaleIcon;
            std::optional<std::string_view> femaleMessage;

            if (field == "ARMO\\MaleWorldModel") {
                maleWorld = value;
                handled = true;
            } else if (field == "ARMO\\MaleIcon") {
                maleIcon = value;
                handled = true;
            } else if (field == "ARMO\\MaleMessageIcon") {
                maleMessage = value;
                handled = true;
            } else if (field == "ARMO\\FemaleWorldModel") {
                femaleWorld = value;
                handled = true;
            } else if (field == "ARMO\\FemaleIcon") {
                femaleIcon = value;
                handled = true;
            } else if (field == "ARMO\\FemaleMessageIcon") {
                femaleMessage = value;
                handled = true;
            }
            if (handled) {
                found = fo4::forms::set_armor_model_fields(
                    mutated, key, maleWorld, maleIcon, maleMessage,
                    femaleWorld, femaleIcon, femaleMessage);
            }
        } else {
            std::optional<std::string_view> maleBiped;
            std::optional<std::string_view> femaleBiped;
            std::optional<std::string_view> maleFirst;
            std::optional<std::string_view> femaleFirst;

            if (field == "ARMA\\MaleBipedModel") {
                maleBiped = value;
                handled = true;
            } else if (field == "ARMA\\FemaleBipedModel") {
                femaleBiped = value;
                handled = true;
            } else if (field == "ARMA\\MaleFirstPersonModel") {
                maleFirst = value;
                handled = true;
            } else if (field == "ARMA\\FemaleFirstPersonModel") {
                femaleFirst = value;
                handled = true;
            }
            if (handled) {
                found = fo4::forms::set_armor_addon_model_fields(
                    mutated, key, maleBiped, femaleBiped, maleFirst, femaleFirst);
            }
        }

        if (!handled) {
            return std::unexpected(Error{
                "FieldWriteUnsupported",
                "Unsupported armor field path: " + field
            });
        }
        if (!found) {
            return std::unexpected(Error{
                "RecordNotFound",
                "Record is not present in " + pluginName + ": " + formKeyText
            });
        }

        stagePluginMutationLocked(*loaded, std::move(mutated));

        return nlohmann::json(
            "Updated " + field + " for " + formKeyText + " in " + pluginName + ".");
    } catch (const fo4::forms::ArmorError& error) {
        return std::unexpected(Error{"ArmorWriteFailed", error.what()});
    } catch (const std::exception& error) {
        return std::unexpected(Error{"FieldWriteFailed", error.what()});
    }
}

}  // namespace fo4ide::plugin
