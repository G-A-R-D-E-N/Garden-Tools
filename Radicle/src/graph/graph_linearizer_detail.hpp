#pragma once

#include <cctype>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <fo4/graph/graph_ir.hpp>

namespace fo4::graph::linearizer_detail {

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

inline std::shared_ptr<ir::IrLiteral> lit(std::string text) {
    auto e = std::make_shared<ir::IrLiteral>();
    e->text = std::move(text);
    return e;
}

inline std::shared_ptr<ir::IrName> name(std::string text) {
    auto e = std::make_shared<ir::IrName>();
    e->name = std::move(text);
    return e;
}

inline std::shared_ptr<ir::IrSelf> selfExpr() { return std::make_shared<ir::IrSelf>(); }

inline std::shared_ptr<ir::IrParent> parentExpr() { return std::make_shared<ir::IrParent>(); }

inline std::shared_ptr<ir::IrMember> member(std::shared_ptr<ir::IrExpression> target, std::string memberName) {
    auto e = std::make_shared<ir::IrMember>();
    e->target = std::move(target);
    e->name = std::move(memberName);
    return e;
}

inline std::shared_ptr<ir::IrIndex> indexExpr(std::shared_ptr<ir::IrExpression> target,
                                       std::shared_ptr<ir::IrExpression> index) {
    auto e = std::make_shared<ir::IrIndex>();
    e->target = std::move(target);
    e->index = std::move(index);
    return e;
}

inline std::shared_ptr<ir::IrUnary> unary(std::string op, std::shared_ptr<ir::IrExpression> operand) {
    auto e = std::make_shared<ir::IrUnary>();
    e->op = std::move(op);
    e->operand = std::move(operand);
    return e;
}

inline std::shared_ptr<ir::IrBinary> binary(std::string op, std::shared_ptr<ir::IrExpression> left,
                                     std::shared_ptr<ir::IrExpression> right) {
    auto e = std::make_shared<ir::IrBinary>();
    e->op = std::move(op);
    e->left = std::move(left);
    e->right = std::move(right);
    return e;
}

inline std::shared_ptr<ir::IrCast> cast(std::shared_ptr<ir::IrExpression> value, std::string targetType,
                                 bool targetIsArray) {
    auto e = std::make_shared<ir::IrCast>();
    e->value = std::move(value);
    e->targetType = std::move(targetType);
    e->targetIsArray = targetIsArray;
    return e;
}

inline std::shared_ptr<ir::IrTypeCheck> typeCheck(std::shared_ptr<ir::IrExpression> value,
                                           std::string targetType, bool targetIsArray) {
    auto e = std::make_shared<ir::IrTypeCheck>();
    e->value = std::move(value);
    e->targetType = std::move(targetType);
    e->targetIsArray = targetIsArray;
    return e;
}

inline std::shared_ptr<ir::IrNewArray> newArray(std::string elementType,
                                         std::shared_ptr<ir::IrExpression> size) {
    auto e = std::make_shared<ir::IrNewArray>();
    e->elementType = std::move(elementType);
    e->size = std::move(size);
    return e;
}

inline std::shared_ptr<ir::IrNewStruct> newStruct(std::string structName) {
    auto e = std::make_shared<ir::IrNewStruct>();
    e->structName = std::move(structName);
    return e;
}

inline std::shared_ptr<ir::IrAssign> assign(std::shared_ptr<ir::IrExpression> target,
                                     std::shared_ptr<ir::IrExpression> value) {
    auto s = std::make_shared<ir::IrAssign>();
    s->target = std::move(target);
    s->value = std::move(value);
    return s;
}

inline std::shared_ptr<ir::IrExpressionStatement> exprStatement(std::shared_ptr<ir::IrExpression> e) {
    auto s = std::make_shared<ir::IrExpressionStatement>();
    s->expression = std::move(e);
    return s;
}

inline std::shared_ptr<ir::IrReturn> returnStmt(std::shared_ptr<ir::IrExpression> value) {
    auto s = std::make_shared<ir::IrReturn>();
    s->value = std::move(value);
    return s;
}

}
