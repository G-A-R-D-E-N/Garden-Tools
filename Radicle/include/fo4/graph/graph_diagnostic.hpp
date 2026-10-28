#pragma once

#include <optional>
#include <string>
#include <vector>

namespace fo4::graph {

enum class GraphSeverity { Warning, Error };

struct GraphDiagnostic {
    std::string code;
    GraphSeverity severity{GraphSeverity::Error};
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
        const auto kind = severity == GraphSeverity::Error ? "Error" : "Warning";
        return code + " " + kind + ": " + message + where;
    }
};

inline GraphDiagnostic errorDiagnostic(std::string code, std::string message,
                                       std::optional<std::string> nodeId = std::nullopt,
                                       std::optional<std::string> pinId = std::nullopt) {
    return GraphDiagnostic{std::move(code), GraphSeverity::Error, std::move(message),
                           std::move(nodeId), std::move(pinId), std::nullopt, {}, 0, std::nullopt};
}

inline GraphDiagnostic warningDiagnostic(std::string code, std::string message,
                                         std::optional<std::string> nodeId = std::nullopt,
                                         std::optional<std::string> pinId = std::nullopt) {
    return GraphDiagnostic{std::move(code), GraphSeverity::Warning, std::move(message),
                           std::move(nodeId), std::move(pinId), std::nullopt, {}, 0, std::nullopt};
}

namespace codes {
inline constexpr std::string_view kUnsupportedSchema = "GRA0001";
inline constexpr std::string_view kMalformedDocument = "GRA0002";

inline constexpr std::string_view kIncompatibleWireType = "GRA0010";
inline constexpr std::string_view kNarrowingWireNeedsCast = "GRA0011";
inline constexpr std::string_view kWireDirection = "GRA0012";
inline constexpr std::string_view kMultipleDataSources = "GRA0013";
inline constexpr std::string_view kMultipleExecSuccessors = "GRA0014";
inline constexpr std::string_view kJaggedArray = "GRA0015";
inline constexpr std::string_view kPinKindMismatch = "GRA0016";
inline constexpr std::string_view kDanglingWire = "GRA0017";

inline constexpr std::string_view kExecCycle = "GRA0020";
inline constexpr std::string_view kDataCycle = "GRA0021";
inline constexpr std::string_view kUnreachableExec = "GRA0022";
inline constexpr std::string_view kUnstructuredFlow = "GRA0023";
inline constexpr std::string_view kNoEntryNodes = "GRA0024";
inline constexpr std::string_view kLoopExitOutsideLoop = "GRA0025";

inline constexpr std::string_view kMissingRequiredInput = "GRA0030";
inline constexpr std::string_view kUndeclaredReference = "GRA0031";
inline constexpr std::string_view kDuplicateVariableName = "GRA0032";
inline constexpr std::string_view kUnknownCallTarget = "GRA0033";
inline constexpr std::string_view kArgumentCount = "GRA0034";
inline constexpr std::string_view kUnknownStructMember = "GRA0035";
inline constexpr std::string_view kUnknownNodeDefinition = "GRA0036";
inline constexpr std::string_view kDuplicateDeclaration = "GRA0037";
inline constexpr std::string_view kInvalidScriptHeader = "GRA0038";
inline constexpr std::string_view kUnconnectedSelf = "GRA0039";

inline constexpr std::string_view kNotAllPathsReturn = "GRA0040";
inline constexpr std::string_view kUseBeforeAssignment = "GRA0041";
inline constexpr std::string_view kReturnValueMissing = "GRA0042";
inline constexpr std::string_view kReturnValueUnexpected = "GRA0043";
inline constexpr std::string_view kLoopConditionFromLoopBody = "GRA0044";
inline constexpr std::string_view kOrphanNode = "GRA0045";

inline constexpr std::string_view kDuplicateNativeBinding = "GRA0050";
inline constexpr std::string_view kNativeArityUnsupported = "GRA0051";
inline constexpr std::string_view kUnmappedNativeType = "GRA0052";
inline constexpr std::string_view kNativeVersionMismatch = "GRA0053";
inline constexpr std::string_view kInvalidBindingName = "GRA0054";
inline constexpr std::string_view kStructOwnerMismatch = "GRA0055";
inline constexpr std::string_view kNoModules = "GRA0056";

inline constexpr std::string_view kInternalEmitterFault = "GRA0900";
}

}
