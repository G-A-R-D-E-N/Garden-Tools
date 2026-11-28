#include <fo4ide/plugin/PluginValidation.hpp>

#include "RadiclePluginValidateInternal.hpp"

#include <cstdint>
#include <exception>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <fo4/plugin/reader.hpp>
#include <fo4/schema/vmad.hpp>


namespace fo4ide::plugin::detail {
namespace {


const fo4::Record* findByFormKey(const fo4::Plugin& plugin, const std::string& text) {
    fo4::FormKey key;
    try {
        key = fo4::FormKey::parse(text);
    } catch (const std::exception&) {
        return nullptr;
    }
    for (const auto* record : fo4::collect_records(plugin)) {
        if (record->header.signature == fo4::sig_tes4) continue;
        if (record->form_key == key) return record;
    }
    return nullptr;
}

const fo4::Record* findByEditorId(const fo4::Plugin& plugin, std::string_view editorId) {
    if (editorId.empty()) return nullptr;
    for (const auto* record : fo4::collect_records(plugin)) {
        if (record->header.signature == fo4::sig_tes4) continue;
        if (sameName(editorIdOf(*record), editorId)) return record;
    }
    return nullptr;
}

std::string stringField(const nlohmann::json& object, const char* name) {
    const auto found = object.find(name);
    if (found == object.end() || !found->is_string()) return {};
    return found->get<std::string>();
}

const fo4::Record* contractRecord(const fo4::Plugin& plugin, const std::string& formKey,
                                  const std::string& editorId, std::string_view type,
                                  std::string_view field, const nlohmann::json& entry, FindingSink& sink) {
    for (const auto* key : {"formKey", "editorId"}) {
        const auto found = entry.find(key);
        if (found != entry.end() && (!found->is_string() || found->get<std::string>().empty())) {
            sink.add("CONTRACT_RECORD_MISSING", Severity::error, formKey, editorId, std::string(type),
                     std::string(field), std::string("Contract identifier ") + key + " must be a non-empty string");
            return nullptr;
        }
    }
    const auto label = formKey.empty() ? editorId : formKey;
    const auto* record = formKey.empty() ? findByEditorId(plugin, editorId)
                                         : findByFormKey(plugin, formKey);
    if (record == nullptr) {
        sink.add("CONTRACT_RECORD_MISSING", Severity::error, formKey, editorId, std::string(type),
                 std::string(field), "Contract expects " + label + " but the plugin does not define it");
        return nullptr;
    }
    const auto actualEditorId = editorIdOf(*record);
    if (!editorId.empty() && !sameName(actualEditorId, editorId)) {
        sink.add("CONTRACT_EDITOR_ID_MISMATCH", Severity::error, record->form_key.to_string(),
                 actualEditorId, record->header.signature.to_string(), "EDID",
                 "Contract expects " + label + " to have editor ID " + editorId +
                     " but it is " + actualEditorId);
        return nullptr;
    }
    return record;
}

std::string globTypeName(std::uint8_t code) {
    switch (code) {
        case 's': return "short";
        case 'l': return "long";
        case 'f': return "float";
        case 'b': return "boolean";
        default: return "unknown";
    }
}

void checkRecords(const fo4::Plugin& plugin, const nlohmann::json& entries, FindingSink& sink) {
    for (const auto& entry : entries) {
        if (!entry.is_object()) continue;
        const auto formKeyText = stringField(entry, "formKey");
        const auto editorIdText = stringField(entry, "editorId");
        const auto typeText = stringField(entry, "type");
        const auto label = formKeyText.empty() ? editorIdText : formKeyText;

        const auto* record = contractRecord(plugin, formKeyText, editorIdText, typeText, "FORMID", entry, sink);
        if (record == nullptr) continue;

        const auto actualType = record->header.signature.to_string();
        if (!typeText.empty() && !sameName(actualType, typeText)) {
            sink.add("CONTRACT_RECORD_TYPE_MISMATCH", Severity::error,
                     record->form_key.to_string(), editorIdOf(*record), actualType, "SIGNATURE",
                     "Contract expects " + label + " to be " + typeText + " but it is " +
                         actualType);
        }
    }
}

void checkGlobals(const fo4::Plugin& plugin, const nlohmann::json& entries, FindingSink& sink) {
    for (const auto& entry : entries) {
        if (!entry.is_object()) continue;
        const auto formKeyText = stringField(entry, "formKey");
        const auto editorIdText = stringField(entry, "editorId");
        const auto expected = stringField(entry, "type");
        const auto label = formKeyText.empty() ? editorIdText : formKeyText;

        const auto* record = contractRecord(plugin, formKeyText, editorIdText, "GLOB", "FORMID", entry, sink);
        if (record == nullptr) continue;
        if (record->header.signature != fo4::Signature::from_string("GLOB")) {
            sink.add("CONTRACT_RECORD_TYPE_MISMATCH", Severity::error,
                     record->form_key.to_string(), editorIdOf(*record),
                     record->header.signature.to_string(), "SIGNATURE",
                     "Contract expects " + label + " to be GLOB but it is " +
                         record->header.signature.to_string());
            continue;
        }
        const auto* fnam = findSubrecord(*record, fo4::Signature::from_string("FNAM"));
        if (fnam == nullptr || fnam->data.empty()) {
            sink.add("CONTRACT_GLOB_TYPE_MISMATCH", Severity::error, record->form_key.to_string(),
                     editorIdOf(*record), "GLOB", "FNAM",
                     "Contract expects GLOB " + label + " to be " + expected +
                         " but it has no FNAM type byte");
            continue;
        }
        const auto actual = globTypeName(std::to_integer<std::uint8_t>(fnam->data.front()));
        if (!expected.empty() && !sameName(actual, expected)) {
            sink.add("CONTRACT_GLOB_TYPE_MISMATCH", Severity::error, record->form_key.to_string(),
                     editorIdOf(*record), "GLOB", "FNAM",
                     "Contract expects GLOB " + label + " to be " + expected + " but it is " +
                         actual);
        }
    }
}

std::optional<fo4::VmadScripts> readScripts(const fo4::Record& record) {
    const auto* vmad = findSubrecord(record, fo4::Signature::from_string("VMAD"));
    if (vmad == nullptr) return std::nullopt;
    try {
        return fo4::read_vmad_scripts(vmad->data);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

void checkVmad(const fo4::Plugin& plugin, const nlohmann::json& entries, FindingSink& sink) {
    for (const auto& entry : entries) {
        if (!entry.is_object()) continue;
        const auto recordText = stringField(entry, "record");
        const auto scriptName = stringField(entry, "script");
        const auto* record = findByFormKey(plugin, recordText);
        if (record == nullptr) {
            sink.add("CONTRACT_RECORD_MISSING", Severity::error, recordText, {}, {}, "VMAD",
                     "Contract expects a VMAD binding on " + recordText +
                         " but the plugin does not define that record");
            continue;
        }

        const auto scripts = readScripts(*record);
        const fo4::VmadScript* script = nullptr;
        if (scripts.has_value()) {
            for (const auto& candidate : scripts->scripts) {
                if (sameName(candidate.name, scriptName)) {
                    script = &candidate;
                    break;
                }
            }
        }
        if (script == nullptr) {
            sink.add("CONTRACT_VMAD_BINDING_MISSING", Severity::error,
                     record->form_key.to_string(), editorIdOf(*record),
                     record->header.signature.to_string(), "VMAD",
                     "Contract expects script " + scriptName + " on " +
                         record->form_key.to_string() + " but the VMAD does not attach it");
            continue;
        }

        const auto propertiesIt = entry.find("properties");
        if (propertiesIt == entry.end() || !propertiesIt->is_array()) continue;
        for (const auto& expected : *propertiesIt) {
            if (!expected.is_object()) continue;
            const auto name = stringField(expected, "name");
            const fo4::VmadProperty* property = nullptr;
            for (const auto& candidate : script->properties) {
                if (sameName(candidate.name, name)) {
                    property = &candidate;
                    break;
                }
            }
            if (property == nullptr) {
                sink.add("CONTRACT_VMAD_BINDING_MISSING", Severity::error,
                         record->form_key.to_string(), editorIdOf(*record),
                         record->header.signature.to_string(), name,
                         "Contract expects property " + name + " on script " + scriptName +
                             " but the VMAD does not declare it");
                continue;
            }
            const auto expectedForm = stringField(expected, "formKey");
            if (expectedForm.empty()) continue;
            std::string actualForm;
            bool bound = false;
            if (property->object.has_value() && !property->object->is_null()) {
                try {
                    const auto actual = fo4::resolve_plugin_form_id(
                        property->object.value(), fo4::plugin_form_id_context(plugin));
                    actualForm = actual.to_string();
                    // Compared as FormKeys, so "0x000801:Mod.esp" and "801:mod.ESP" name the same form.
                    bound = actual == fo4::FormKey::parse(expectedForm);
                } catch (const std::exception&) {
                    bound = false;
                }
            }
            if (!bound) {
                sink.add("CONTRACT_VMAD_BINDING_MISSING", Severity::error,
                         record->form_key.to_string(), editorIdOf(*record),
                         record->header.signature.to_string(), name,
                         "Contract expects property " + name + " to bind " + expectedForm +
                             " but it binds " +
                             (actualForm.empty() ? std::string("nothing") : actualForm));
            }
        }
    }
}

void checkRequiredFields(const fo4::Plugin& plugin,
                         const nlohmann::json& entries,
                         FindingSink& sink) {
    for (const auto& entry : entries) {
        if (!entry.is_object()) continue;
        const auto formKeyText = stringField(entry, "formKey");
        const auto editorIdText = stringField(entry, "editorId");
        const auto field = stringField(entry, "field");
        const auto label = formKeyText.empty() ? editorIdText : formKeyText;
        const auto* record = contractRecord(plugin, formKeyText, editorIdText, {}, field, entry, sink);
        if (record == nullptr) continue;
        if (field.empty()) continue;

        if (field.size() != 4) {
            sink.add("CONTRACT_FIELD_UNSUPPORTED", Severity::error, record->form_key.to_string(),
                     editorIdOf(*record), record->header.signature.to_string(), field,
                     "Contract requires " + label + " to carry " + field +
                         ", but requiredFields takes a 4-character subrecord signature, so this field was not checked");
            continue;
        }

        const auto* subrecord = findSubrecord(*record, fo4::Signature::from_string(field));
        if (subrecord == nullptr || subrecord->data.empty()) {
            sink.add("CONTRACT_REQUIRED_FIELD_UNSET", Severity::error,
                     record->form_key.to_string(), editorIdOf(*record),
                     record->header.signature.to_string(), field,
                     "Contract requires " + label + " to carry " + field +
                         " but it is missing or empty");
        }
    }
}

}

void validateContract(const fo4::Plugin& plugin,
                      const ValidationOptions& options,
                      FindingSink& sink) {
    if (options.contract == nullptr) return;
    const auto& contract = *options.contract;
    if (!contract.is_object()) return;

    const auto records = contract.find("records");
    if (records != contract.end() && records->is_array()) checkRecords(plugin, *records, sink);
    const auto globals = contract.find("globals");
    if (globals != contract.end() && globals->is_array()) checkGlobals(plugin, *globals, sink);
    const auto vmad = contract.find("vmad");
    if (vmad != contract.end() && vmad->is_array()) checkVmad(plugin, *vmad, sink);
    const auto fields = contract.find("requiredFields");
    if (fields != contract.end() && fields->is_array()) {
        checkRequiredFields(plugin, *fields, sink);
    }
}

}
