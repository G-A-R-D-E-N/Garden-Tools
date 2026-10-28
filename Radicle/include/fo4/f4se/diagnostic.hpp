#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::f4se {

enum class DiagnosticSeverity { Warning, Error };

struct Diagnostic {
    std::string code;
    DiagnosticSeverity severity{DiagnosticSeverity::Error};
    std::string message;
    std::optional<std::string> nodeId;
    std::optional<std::string> pinId;
    std::optional<std::string> wireId;
    std::vector<std::string> relatedNodes;
    int sourceLine{0};
    std::optional<std::string> papyrusCode;

    std::string toString() const {
        std::string where;
        if (nodeId) {
            where = " [" + *nodeId;
            if (pinId) where += ":" + *pinId;
            where += "]";
        }
        const auto kind = severity == DiagnosticSeverity::Error ? "Error" : "Warning";
        return code + " " + kind + ": " + message + where;
    }
};

inline Diagnostic errorDiagnostic(std::string code, std::string message,
                                  std::optional<std::string> nodeId = std::nullopt,
                                  std::optional<std::string> pinId = std::nullopt) {
    return Diagnostic{std::move(code), DiagnosticSeverity::Error, std::move(message),
                      std::move(nodeId), std::move(pinId), std::nullopt, {}, 0, std::nullopt};
}

namespace codes {
inline constexpr std::string_view kDuplicateVariableName = "GRA0032";
inline constexpr std::string_view kArgumentCount = "GRA0034";
inline constexpr std::string_view kDuplicateDeclaration = "GRA0037";

inline constexpr std::string_view kDuplicateNativeBinding = "GRA0050";
inline constexpr std::string_view kNativeArityUnsupported = "GRA0051";
inline constexpr std::string_view kUnmappedNativeType = "GRA0052";
inline constexpr std::string_view kInvalidBindingName = "GRA0054";
inline constexpr std::string_view kStructOwnerMismatch = "GRA0055";
inline constexpr std::string_view kNoModules = "GRA0056";
}

}
