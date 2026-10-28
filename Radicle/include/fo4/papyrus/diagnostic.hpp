#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::papyrus {

// The offset is a 0-based character index into the source text that produced the span.
struct Span {
    int start{0};
    int length{0};
    int line{1};
    int column{1};

    int end() const { return start + length; }
    bool contains(int offset) const { return offset >= start && offset <= end(); }

    // The union of two spans must keep the earlier position as its anchor.
    Span to(const Span& other) const {
        const auto begin = start < other.start ? start : other.start;
        const auto finish = end() > other.end() ? end() : other.end();
        if (start <= other.start) {
            return Span{begin, finish - begin, line, column};
        }
        return Span{begin, finish - begin, other.line, other.column};
    }

    bool operator==(const Span&) const = default;

    std::string toString() const {
        return "(" + std::to_string(line) + "," + std::to_string(column) + ")+" +
               std::to_string(length);
    }
};

enum class Severity { Warning, Error };

// The code strings must stay identical to the C# PapyrusDiagnosticCodes values.
enum class DiagnosticCode {
    UnterminatedString,
    UnterminatedBlockComment,
    UnterminatedDocComment,
    UnexpectedCharacter,
    MalformedNumber,
    ExpectedToken,
    ExpectedScriptName,
    ExpectedIdentifier,
    ExpectedType,
    ExpectedExpression,
    UnexpectedToken,
    UnterminatedBlock,
    PropertyNeedsAccessor,
    StructNeedsMember,
    TooManyErrors,
    DuplicateDeclaration,
    UnresolvedName,
    UnknownMember,
    TypeMismatch,
    ArgumentCount,
    UnknownArgumentName,
    InvalidCast,
    OverrideMismatch,
    ParameterOrder,
    CannotEmit,
    UnknownCallTarget,
    NonConstantInitializer,
};

constexpr std::string_view codeString(DiagnosticCode code) {
    switch (code) {
        case DiagnosticCode::UnterminatedString: return "PAP0001";
        case DiagnosticCode::UnterminatedBlockComment: return "PAP0002";
        case DiagnosticCode::UnterminatedDocComment: return "PAP0003";
        case DiagnosticCode::UnexpectedCharacter: return "PAP0004";
        case DiagnosticCode::MalformedNumber: return "PAP0005";
        case DiagnosticCode::ExpectedToken: return "PAP0010";
        case DiagnosticCode::ExpectedScriptName: return "PAP0011";
        case DiagnosticCode::ExpectedIdentifier: return "PAP0012";
        case DiagnosticCode::ExpectedType: return "PAP0013";
        case DiagnosticCode::ExpectedExpression: return "PAP0014";
        case DiagnosticCode::UnexpectedToken: return "PAP0015";
        case DiagnosticCode::UnterminatedBlock: return "PAP0016";
        case DiagnosticCode::PropertyNeedsAccessor: return "PAP0017";
        case DiagnosticCode::StructNeedsMember: return "PAP0018";
        case DiagnosticCode::TooManyErrors: return "PAP0019";
        case DiagnosticCode::DuplicateDeclaration: return "PAP0020";
        case DiagnosticCode::UnresolvedName: return "PAP0030";
        case DiagnosticCode::UnknownMember: return "PAP0031";
        case DiagnosticCode::TypeMismatch: return "PAP0040";
        case DiagnosticCode::ArgumentCount: return "PAP0041";
        case DiagnosticCode::UnknownArgumentName: return "PAP0042";
        case DiagnosticCode::InvalidCast: return "PAP0043";
        case DiagnosticCode::OverrideMismatch: return "PAP0044";
        case DiagnosticCode::ParameterOrder: return "PAP0045";
        case DiagnosticCode::CannotEmit: return "PAP0050";
        case DiagnosticCode::UnknownCallTarget: return "PAP0051";
        case DiagnosticCode::NonConstantInitializer: return "PAP0052";
    }
    return "PAP????";
}

// A reported diagnostic must carry severity, message, span and an optional file.
struct Diagnostic {
    DiagnosticCode code;
    Severity severity{Severity::Error};
    std::string message;
    Span span;
    std::optional<std::string> file;

    std::string toString() const {
        const auto where = file ? *file : std::string("<text>");
        const auto kind = severity == Severity::Error ? "error" : "warning";
        return where + "(" + std::to_string(span.line) + "," +
               std::to_string(span.column) + "): " + kind + " " +
               std::string(codeString(code)) + ": " + message;
    }
};

// The bag must cap at 200 items and then append exactly one TooManyErrors entry.
class DiagnosticBag {
public:
    static constexpr int k_maxDiagnostics = 200;

    void report(DiagnosticCode code, std::string message, Span span,
                Severity severity = Severity::Error) {
        if (capped_) return;
        if (items_.size() >= static_cast<std::size_t>(k_maxDiagnostics)) {
            capped_ = true;
            items_.push_back(Diagnostic{
                DiagnosticCode::TooManyErrors, Severity::Error,
                "More than " + std::to_string(k_maxDiagnostics) +
                    " errors; not reporting the rest.",
                span, std::nullopt});
            return;
        }
        items_.push_back(Diagnostic{code, severity, std::move(message), span, std::nullopt});
    }

    const std::vector<Diagnostic>& items() const { return items_; }

    bool hasErrors() const {
        for (const auto& item : items_) {
            if (item.severity == Severity::Error) return true;
        }
        return false;
    }

    void truncateTo(std::size_t count) {
        if (capped_) return;
        if (count < items_.size()) items_.resize(count);
    }

    void setFile(std::string_view file) {
        for (auto& item : items_) item.file = std::string(file);
    }

private:
    std::vector<Diagnostic> items_;
    bool capped_{false};
};

}
