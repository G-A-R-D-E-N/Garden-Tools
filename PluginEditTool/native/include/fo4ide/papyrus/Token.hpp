#pragma once

#include <string>
#include <string_view>
#include <unordered_map>

#include <fo4ide/papyrus/Diagnostic.hpp>

namespace fo4ide::papyrus {

// Port of PapyrusTokenKind, in the same order as the C# enum.
enum class TokenKind {
    EndOfFile,

    Newline,

    Identifier,
    IntLiteral,
    FloatLiteral,
    StringLiteral,

    DocComment,

    As,
    Auto,
    AutoReadOnly,
    BetaOnly,
    Bool,
    Const,
    CustomEvent,
    CustomEventName,
    DebugOnly,
    Else,
    ElseIf,
    EndEvent,
    EndFunction,
    EndGroup,
    EndIf,
    EndProperty,
    EndState,
    EndStruct,
    EndWhile,
    Event,
    Extends,
    False,
    Float,
    Function,
    Global,
    Group,
    If,
    Import,
    Is,
    Int,
    Length,
    Native,
    New,
    None,
    Property,
    Return,
    ScriptName,
    ScriptEventName,
    State,
    String,
    Struct,
    StructVarName,
    True,
    Var,
    While,

    LParen,
    RParen,
    LBracket,
    RBracket,
    Comma,
    Dot,
    Colon,
    Hash,
    Assign,
    PlusAssign,
    MinusAssign,
    StarAssign,
    SlashAssign,
    PercentAssign,
    Equal,
    NotEqual,
    Less,
    Greater,
    LessEqual,
    GreaterEqual,
    Or,
    And,
    Not,
    Plus,
    Minus,
    Star,
    Slash,
    Percent,
};

constexpr std::string_view tokenKindName(TokenKind kind) {
    switch (kind) {
        case TokenKind::EndOfFile: return "EndOfFile";
        case TokenKind::Newline: return "Newline";
        case TokenKind::Identifier: return "Identifier";
        case TokenKind::IntLiteral: return "IntLiteral";
        case TokenKind::FloatLiteral: return "FloatLiteral";
        case TokenKind::StringLiteral: return "StringLiteral";
        case TokenKind::DocComment: return "DocComment";
        case TokenKind::As: return "As";
        case TokenKind::Auto: return "Auto";
        case TokenKind::AutoReadOnly: return "AutoReadOnly";
        case TokenKind::BetaOnly: return "BetaOnly";
        case TokenKind::Bool: return "Bool";
        case TokenKind::Const: return "Const";
        case TokenKind::CustomEvent: return "CustomEvent";
        case TokenKind::CustomEventName: return "CustomEventName";
        case TokenKind::DebugOnly: return "DebugOnly";
        case TokenKind::Else: return "Else";
        case TokenKind::ElseIf: return "ElseIf";
        case TokenKind::EndEvent: return "EndEvent";
        case TokenKind::EndFunction: return "EndFunction";
        case TokenKind::EndGroup: return "EndGroup";
        case TokenKind::EndIf: return "EndIf";
        case TokenKind::EndProperty: return "EndProperty";
        case TokenKind::EndState: return "EndState";
        case TokenKind::EndStruct: return "EndStruct";
        case TokenKind::EndWhile: return "EndWhile";
        case TokenKind::Event: return "Event";
        case TokenKind::Extends: return "Extends";
        case TokenKind::False: return "False";
        case TokenKind::Float: return "Float";
        case TokenKind::Function: return "Function";
        case TokenKind::Global: return "Global";
        case TokenKind::Group: return "Group";
        case TokenKind::If: return "If";
        case TokenKind::Import: return "Import";
        case TokenKind::Is: return "Is";
        case TokenKind::Int: return "Int";
        case TokenKind::Length: return "Length";
        case TokenKind::Native: return "Native";
        case TokenKind::New: return "New";
        case TokenKind::None: return "None";
        case TokenKind::Property: return "Property";
        case TokenKind::Return: return "Return";
        case TokenKind::ScriptName: return "ScriptName";
        case TokenKind::ScriptEventName: return "ScriptEventName";
        case TokenKind::State: return "State";
        case TokenKind::String: return "String";
        case TokenKind::Struct: return "Struct";
        case TokenKind::StructVarName: return "StructVarName";
        case TokenKind::True: return "True";
        case TokenKind::Var: return "Var";
        case TokenKind::While: return "While";
        case TokenKind::LParen: return "LParen";
        case TokenKind::RParen: return "RParen";
        case TokenKind::LBracket: return "LBracket";
        case TokenKind::RBracket: return "RBracket";
        case TokenKind::Comma: return "Comma";
        case TokenKind::Dot: return "Dot";
        case TokenKind::Colon: return "Colon";
        case TokenKind::Hash: return "Hash";
        case TokenKind::Assign: return "Assign";
        case TokenKind::PlusAssign: return "PlusAssign";
        case TokenKind::MinusAssign: return "MinusAssign";
        case TokenKind::StarAssign: return "StarAssign";
        case TokenKind::SlashAssign: return "SlashAssign";
        case TokenKind::PercentAssign: return "PercentAssign";
        case TokenKind::Equal: return "Equal";
        case TokenKind::NotEqual: return "NotEqual";
        case TokenKind::Less: return "Less";
        case TokenKind::Greater: return "Greater";
        case TokenKind::LessEqual: return "LessEqual";
        case TokenKind::GreaterEqual: return "GreaterEqual";
        case TokenKind::Or: return "Or";
        case TokenKind::And: return "And";
        case TokenKind::Not: return "Not";
        case TokenKind::Plus: return "Plus";
        case TokenKind::Minus: return "Minus";
        case TokenKind::Star: return "Star";
        case TokenKind::Slash: return "Slash";
        case TokenKind::Percent: return "Percent";
    }
    return "?";
}

// Port of PapyrusToken.
struct Token {
    TokenKind kind{TokenKind::EndOfFile};
    std::string text;
    Span span;

    std::string toString() const {
        return std::string(tokenKindName(kind)) + " '" + text + "' " + span.toString();
    }
};

// Port of PapyrusKeywords: the 45-case-insensitive Papyrus keyword table.
class Keywords {
public:
    static bool tryGet(std::string_view word, TokenKind& kind) {
        const auto it = map().find(lower(word));
        if (it == map().end()) return false;
        kind = it->second;
        return true;
    }

    static const std::unordered_map<std::string, TokenKind>& all() { return map(); }

private:
    static std::string lower(std::string_view word) {
        std::string out(word);
        for (auto& ch : out) {
            if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
        }
        return out;
    }

    static const std::unordered_map<std::string, TokenKind>& map() {
        static const std::unordered_map<std::string, TokenKind> k_map{
            {"as", TokenKind::As},
            {"auto", TokenKind::Auto},
            {"autoreadonly", TokenKind::AutoReadOnly},
            {"betaonly", TokenKind::BetaOnly},
            {"bool", TokenKind::Bool},
            {"const", TokenKind::Const},
            {"customevent", TokenKind::CustomEvent},
            {"customeventname", TokenKind::CustomEventName},
            {"debugonly", TokenKind::DebugOnly},
            {"else", TokenKind::Else},
            {"elseif", TokenKind::ElseIf},
            {"endevent", TokenKind::EndEvent},
            {"endfunction", TokenKind::EndFunction},
            {"endgroup", TokenKind::EndGroup},
            {"endif", TokenKind::EndIf},
            {"endproperty", TokenKind::EndProperty},
            {"endstate", TokenKind::EndState},
            {"endstruct", TokenKind::EndStruct},
            {"endwhile", TokenKind::EndWhile},
            {"event", TokenKind::Event},
            {"extends", TokenKind::Extends},
            {"false", TokenKind::False},
            {"float", TokenKind::Float},
            {"function", TokenKind::Function},
            {"global", TokenKind::Global},
            {"group", TokenKind::Group},
            {"if", TokenKind::If},
            {"import", TokenKind::Import},
            {"is", TokenKind::Is},
            {"int", TokenKind::Int},
            {"length", TokenKind::Length},
            {"native", TokenKind::Native},
            {"new", TokenKind::New},
            {"none", TokenKind::None},
            {"property", TokenKind::Property},
            {"return", TokenKind::Return},
            {"scriptname", TokenKind::ScriptName},
            {"scripteventname", TokenKind::ScriptEventName},
            {"state", TokenKind::State},
            {"string", TokenKind::String},
            {"struct", TokenKind::Struct},
            {"structvarname", TokenKind::StructVarName},
            {"true", TokenKind::True},
            {"var", TokenKind::Var},
            {"while", TokenKind::While},
        };
        return k_map;
    }
};

}
