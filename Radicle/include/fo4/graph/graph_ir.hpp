#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace fo4::graph::ir {

struct IrNode {
    std::optional<std::string> nodeId;
    std::optional<std::string> pinId;
    virtual ~IrNode() = default;
};

struct IrExpression : IrNode {
    std::string typeName;
    bool isArray{false};
};

struct IrLiteral : IrExpression {
    std::string text;
};

struct IrName : IrExpression {
    std::string name;
};

struct IrSelf : IrExpression {};

struct IrParent : IrExpression {};

struct IrMember : IrExpression {
    std::shared_ptr<IrExpression> target;
    std::string name;
};

struct IrIndex : IrExpression {
    std::shared_ptr<IrExpression> target;
    std::shared_ptr<IrExpression> index;
};

struct IrArgument {
    std::shared_ptr<IrExpression> value;
    std::optional<std::string> name;
};

struct IrCall : IrExpression {
    std::shared_ptr<IrExpression> receiver;
    std::string name;
    std::vector<IrArgument> arguments;
    bool isGlobal{false};
};

struct IrUnary : IrExpression {
    std::string op;
    std::shared_ptr<IrExpression> operand;
};

struct IrBinary : IrExpression {
    std::string op;
    std::shared_ptr<IrExpression> left;
    std::shared_ptr<IrExpression> right;
};

struct IrCast : IrExpression {
    std::shared_ptr<IrExpression> value;
    std::string targetType;
    bool targetIsArray{false};
};

struct IrTypeCheck : IrExpression {
    std::shared_ptr<IrExpression> value;
    std::string targetType;
    bool targetIsArray{false};
};

struct IrNewArray : IrExpression {
    std::string elementType;
    std::shared_ptr<IrExpression> size;
};

struct IrNewStruct : IrExpression {
    std::string structName;
};

struct IrStatement : IrNode {};

struct IrDefine : IrStatement {
    std::string name;
    std::string typeName;
    bool isArray{false};
    std::shared_ptr<IrExpression> value;
};

struct IrAssign : IrStatement {
    std::shared_ptr<IrExpression> target;
    std::shared_ptr<IrExpression> value;
};

struct IrExpressionStatement : IrStatement {
    std::shared_ptr<IrExpression> expression;
};

struct IrReturn : IrStatement {
    std::shared_ptr<IrExpression> value;
};

struct IrBranch {
    std::shared_ptr<IrExpression> condition;
    std::vector<std::shared_ptr<IrStatement>> body;
};

struct IrIf : IrStatement {
    std::vector<IrBranch> branches;
    std::vector<std::shared_ptr<IrStatement>> elseBody;
};

struct IrWhile : IrStatement {
    std::shared_ptr<IrExpression> condition;
    std::vector<std::shared_ptr<IrStatement>> body;
};

struct IrParameter {
    std::string name;
    std::string typeName;
    bool isArray{false};
    std::optional<std::string> defaultText;
};

struct IrLocal {
    std::string name;
    std::string typeName;
    bool isArray{false};
};

struct IrCallable {
    std::string name;
    std::string entryNodeId;
    bool isEvent{false};
    bool isGlobal{false};
    std::optional<std::string> remoteObjectType;
    std::optional<std::string> stateName;
    std::optional<std::string> returnTypeName;
    bool returnIsArray{false};
    std::vector<IrParameter> parameters;
    std::vector<IrLocal> locals;
    std::vector<std::shared_ptr<IrStatement>> body;
};

struct IrProperty {
    std::string name;
    std::string typeName;
    bool isArray{false};
    std::vector<std::string> flags;
    std::optional<std::string> initialText;
};

struct IrVariable {
    std::string name;
    std::string typeName;
    bool isArray{false};
    std::vector<std::string> flags;
    std::optional<std::string> initialText;
};

struct IrStructMember {
    std::string name;
    std::string typeName;
    bool isArray{false};
};

struct IrStruct {
    std::string name;
    std::vector<IrStructMember> members;
};

struct IrScript {
    std::string name;
    std::optional<std::string> extends;
    std::vector<std::string> flags;
    std::vector<std::string> imports;
    std::optional<std::string> docComment;
    std::vector<IrStruct> structs;
    std::vector<IrVariable> variables;
    std::vector<IrProperty> properties;
    std::vector<IrCallable> callables;
    std::optional<std::string> autoState;
    std::vector<std::string> customEvents;
};

}
