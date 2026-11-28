#pragma once


#include <fo4ide/plugin/PluginValidation.hpp>

#include <array>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace fo4ide::plugin::detail {

inline std::string asciiLower(std::string_view text) {
    std::string out(text);
    for (auto& ch : out) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return out;
}

inline bool sameName(std::string_view left, std::string_view right) {
    return asciiLower(left) == asciiLower(right);
}

inline std::string editorIdOf(const fo4::Record& record) {
    for (const auto& subrecord : record.subrecords) {
        if (subrecord.signature != fo4::Signature::from_string("EDID")) continue;
        std::string out;
        out.reserve(subrecord.data.size());
        for (const auto byte : subrecord.data) {
            const auto ch = static_cast<char>(std::to_integer<std::uint8_t>(byte));
            if (ch == '\0') break;
            out.push_back(ch);
        }
        return out;
    }
    return {};
}

inline const fo4::Subrecord* findSubrecord(const fo4::Record& record,
                                           fo4::Signature signature) {
    for (const auto& subrecord : record.subrecords) {
        if (subrecord.signature == signature) return &subrecord;
    }
    return nullptr;
}

inline std::string hexFormId(std::uint32_t value) {
    static constexpr std::array<char, 16> kDigits = {
        '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};
    std::string text;
    text.reserve(8);
    for (int shift = 28; shift >= 0; shift -= 4) {
        text.push_back(kDigits[(value >> shift) & 0xFu]);
    }
    return text;
}

class FindingSink {
public:
    FindingSink(const ValidationOptions& options, ValidationReport& report)
        : minimum_(options.minimum), report_(report) {}

    void add(std::string code,
             Severity severity,
             std::string formKey,
             std::string editorId,
             std::string recordType,
             std::string field,
             std::string message) {
        if (severity < minimum_) return;
        ValidationFinding finding;
        finding.code = std::move(code);
        finding.severity = severity;
        finding.formKey = std::move(formKey);
        finding.editorId = std::move(editorId);
        finding.recordType = std::move(recordType);
        finding.field = std::move(field);
        finding.message = std::move(message);
        switch (severity) {
            case Severity::error: ++report_.errors; break;
            case Severity::warning: ++report_.warnings; break;
            case Severity::info: ++report_.infos; break;
        }
        report_.findings.push_back(std::move(finding));
    }

private:
    Severity minimum_;
    ValidationReport& report_;
};

void validateScripts(const fo4::Plugin& plugin,
                     const ValidationOptions& options,
                     FindingSink& sink);

void validateContract(const fo4::Plugin& plugin,
                      const ValidationOptions& options,
                      FindingSink& sink);

}
