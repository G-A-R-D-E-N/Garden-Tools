#pragma once

#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>

#include <fo4/papyrus/ast.hpp>

namespace fo4::graph::lifter_detail {

inline bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

inline std::string nodeKindName(papyrus::NodeKind kind) {
    switch (kind) {
        case papyrus::NodeKind::Import: return "Import";
        case papyrus::NodeKind::VariableDecl: return "VariableDecl";
        case papyrus::NodeKind::StructDecl: return "StructDecl";
        case papyrus::NodeKind::CustomEventDecl: return "CustomEventDecl";
        case papyrus::NodeKind::Parameter: return "Parameter";
        case papyrus::NodeKind::FunctionDecl: return "FunctionDecl";
        case papyrus::NodeKind::EventDecl: return "EventDecl";
        case papyrus::NodeKind::PropertyDecl: return "PropertyDecl";
        case papyrus::NodeKind::GroupDecl: return "GroupDecl";
        case papyrus::NodeKind::StateDecl: return "StateDecl";
        case papyrus::NodeKind::Script: return "Script";
        case papyrus::NodeKind::DefineStatement: return "DefineStatement";
        case papyrus::NodeKind::AssignStatement: return "AssignStatement";
        case papyrus::NodeKind::ExpressionStatement: return "ExpressionStatement";
        case papyrus::NodeKind::ReturnStatement: return "ReturnStatement";
        case papyrus::NodeKind::IfStatement: return "IfStatement";
        case papyrus::NodeKind::WhileStatement: return "WhileStatement";
        case papyrus::NodeKind::LiteralExpression: return "LiteralExpression";
        case papyrus::NodeKind::IdentifierExpression: return "IdentifierExpression";
        case papyrus::NodeKind::MemberExpression: return "MemberExpression";
        case papyrus::NodeKind::IndexExpression: return "IndexExpression";
        case papyrus::NodeKind::Argument: return "Argument";
        case papyrus::NodeKind::CallExpression: return "CallExpression";
        case papyrus::NodeKind::UnaryExpression: return "UnaryExpression";
        case papyrus::NodeKind::BinaryExpression: return "BinaryExpression";
        case papyrus::NodeKind::CastExpression: return "CastExpression";
        case papyrus::NodeKind::TypeCheckExpression: return "TypeCheckExpression";
        case papyrus::NodeKind::NewArrayExpression: return "NewArrayExpression";
        case papyrus::NodeKind::NewStructExpression: return "NewStructExpression";
        case papyrus::NodeKind::ErrorExpression: return "ErrorExpression";
    }
    return "Node";
}

}
