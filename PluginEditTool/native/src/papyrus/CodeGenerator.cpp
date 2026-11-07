#include <fo4ide/papyrus/CodeGenerator.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>
#include <stack>

#include <fo4ide/papyrus/Resolver.hpp>

namespace fo4ide::papyrus {

namespace {

std::string lower(std::string_view text) {
    std::string out(text);
    for (auto& ch : out) {
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    }
    return out;
}

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto x = static_cast<unsigned char>(a[i]);
        const auto y = static_cast<unsigned char>(b[i]);
        if (std::tolower(x) != std::tolower(y)) return false;
    }
    return true;
}

bool hasFlag(const Declaration& decl, std::string_view flag) {
    return decl.hasFlag(flag);
}

std::uint8_t opcodeFor(const std::string& name) {
    for (std::size_t i = 0; i < std::size(PexFile::kOpCodes); ++i) {
        if (name == PexFile::kOpCodes[i].name) return static_cast<std::uint8_t>(i);
    }
    throw std::runtime_error("No PEX opcode named '" + name + "'.");
}

}  // namespace

// ---------------------------------------------------------------------------
// UserFlagTable

void UserFlagTable::declare(std::string name, std::uint8_t bit) {
    masks_[lower(name)] = 1u << bit;
    const auto found = std::find_if(flags_.begin(), flags_.end(),
                                    [bit](const PexUserFlag& f) { return f.index == bit; });
    if (found == flags_.end()) {
        PexUserFlag flag;
        flag.name = lower(name);
        flag.index = bit;
        flags_.push_back(std::move(flag));
    }
}

void UserFlagTable::declareComposite(std::string name, std::uint32_t mask) {
    masks_[lower(name)] = mask;
}

void UserFlagTable::sort() {
    std::stable_sort(flags_.begin(), flags_.end(),
                     [](const PexUserFlag& a, const PexUserFlag& b) { return a.index < b.index; });
}

std::uint32_t UserFlagTable::maskFor(std::string_view flagName) const {
    const auto it = masks_.find(lower(flagName));
    return it == masks_.end() ? 0u : it->second;
}

bool UserFlagTable::knows(std::string_view flagName) const {
    return masks_.contains(lower(flagName));
}

std::uint32_t UserFlagTable::maskFor(const std::vector<std::string>& written) const {
    std::uint32_t mask = 0;
    for (const auto& name : written) mask |= maskFor(name);
    return mask;
}

UserFlagTable UserFlagTable::fallout4Default() {
    UserFlagTable table;
    table.declare("Hidden", 0);
    table.declare("Conditional", 1);
    table.declare("Default", 2);
    table.declare("CollapsedOnRef", 3);
    table.declare("CollapsedOnBase", 4);
    table.declare("Mandatory", 5);
    table.declareComposite("Collapsed", (1u << 3) | (1u << 4));
    table.sort();
    return table;
}

std::string stripComments(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    std::size_t i = 0;
    while (i < text.size()) {
        if (text[i] == '/' && i + 1 < text.size() && text[i + 1] == '/') {
            while (i < text.size() && text[i] != '\n') ++i;
            if (i < text.size()) out.push_back('\n');
            continue;
        }
        if (text[i] == '/' && i + 1 < text.size() && text[i + 1] == '*') {
            i += 2;
            while (i + 1 < text.size() && !(text[i] == '*' && text[i + 1] == '/')) ++i;
            ++i;
            continue;
        }
        out.push_back(text[i]);
        ++i;
    }
    return out;
}

std::vector<std::string> splitWhitespace(std::string_view line) {
    std::vector<std::string> parts;
    std::size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
        std::size_t start = i;
        while (i < line.size() && !std::isspace(static_cast<unsigned char>(line[i]))) ++i;
        if (i > start) parts.emplace_back(line.substr(start, i - start));
    }
    return parts;
}

bool parseByte(std::string_view text, std::uint8_t& out) {
    try {
        const auto value = std::stoul(std::string(text));
        if (value > 31) return false;
        out = static_cast<std::uint8_t>(value);
        return true;
    } catch (...) {
        return false;
    }
}

UserFlagTable UserFlagTable::parse(std::string_view text) {
    UserFlagTable table;
    struct Composite {
        std::string name;
        std::vector<std::string> children;
    };
    std::vector<Composite> composites;

    std::istringstream stream(stripComments(text));
    std::string raw;
    while (std::getline(stream, raw)) {
        auto line = raw;
        line.erase(0, line.find_first_not_of(" \t"));
        line.erase(line.find_last_not_of(" \t\r") + 1);
        if (line.empty()) continue;

        const auto brace = line.find('{');
        if (brace != std::string::npos) {
            line = line.substr(0, brace);
            line.erase(line.find_last_not_of(" \t") + 1);
        }
        if (line.empty()) continue;

        const auto parts = splitWhitespace(line);
        if (parts.size() < 3 || !iequals(parts[0], "Flag")) continue;

        std::uint8_t bit = 0;
        if (parseByte(parts[2], bit)) {
            table.declare(parts[1], bit);
            continue;
        }

        Composite composite;
        composite.name = parts[1];
        for (std::size_t i = 2; i < parts.size(); ++i) {
            if (parts[i] != "&") composite.children.push_back(parts[i]);
        }
        if (!composite.children.empty()) composites.push_back(std::move(composite));
    }

    for (const auto& composite : composites) {
        std::uint32_t mask = 0;
        for (const auto& child : composite.children) mask |= table.maskFor(child);
        if (mask != 0) table.declareComposite(composite.name, mask);
    }

    table.sort();
    return table;
}

UserFlagTable UserFlagTable::fromFileOrDefault(const std::filesystem::path& path) {
    if (path.empty()) return fallout4Default();
    std::ifstream stream(path);
    if (!stream) return fallout4Default();
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    const auto table = parse(buffer.str());
    return table.flags_.empty() ? fallout4Default() : table;
}

std::optional<std::filesystem::path> UserFlagTable::findFlagFile(
    const std::vector<std::filesystem::path>& roots) {
    for (const auto& root : roots) {
        if (root.empty()) continue;
        const auto direct = root / "Institute_Papyrus_Flags.flg";
        if (std::filesystem::exists(direct)) return direct;
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// DeclarationCheck

std::vector<Diagnostic> declarationCheck(const Script& script) {
    std::vector<Diagnostic> problems;
    const auto keyOf = [](const Declaration& declaration) -> std::string {
        const auto* ev = dynamic_cast<const EventDecl*>(&declaration);
        if (ev && ev->remoteObjectType && !ev->remoteObjectType->empty()) {
            return *ev->remoteObjectType + "." + ev->name;
        }
        return declaration.name;
    };

    const auto unique = [&](const std::vector<const Declaration*>& declarations, const std::string& kind,
                            const std::string& scope) {
        std::map<std::string, const Declaration*, std::less<>> seen;
        // OrderBy(d => d.NameSpan.Start)
        std::vector<const Declaration*> ordered = declarations;
        std::stable_sort(ordered.begin(), ordered.end(),
                         [](const Declaration* a, const Declaration* b) {
                             return a->nameSpan.start < b->nameSpan.start;
                         });
        for (const Declaration* declaration : ordered) {
            if (declaration->name.empty()) continue;
            const auto key = keyOf(*declaration);
            const auto existing = seen.find(key);
            if (existing != seen.end()) {
                problems.push_back(Diagnostic{
                    DiagnosticCode::DuplicateDeclaration, Severity::Error,
                    "'" + key + "' is already declared as a " + kind + " in " + scope +
                        ", on line " + std::to_string(existing->second->nameSpan.line) +
                        ". Papyrus has no overloading, so the two cannot both exist.",
                    declaration->nameSpan, script.filePath});
                continue;
            }
            seen.emplace(key, declaration);
        }
    };

    {
        std::vector<const Declaration*> members;
        for (const auto& fn : script.functions) members.push_back(&fn);
        for (const auto& ev : script.events) members.push_back(&ev);
        unique(members, "function or event", "the empty state");
    }
    for (const auto& state : script.states) {
        std::vector<const Declaration*> members;
        for (const auto& fn : state.functions) members.push_back(&fn);
        for (const auto& ev : state.events) members.push_back(&ev);
        unique(members, "function or event", "state '" + state.name + "'");
    }
    {
        std::vector<const Declaration*> members;
        for (const auto& v : script.variables) members.push_back(&v);
        unique(members, "variable", "this script");
    }
    {
        std::vector<const Declaration*> members;
        for (const auto& p : script.properties) members.push_back(&p);
        unique(members, "property", "this script");
    }
    {
        std::vector<const Declaration*> members;
        for (const auto& s : script.structs) members.push_back(&s);
        unique(members, "struct", "this script");
    }
    {
        std::vector<const Declaration*> members;
        for (const auto& s : script.states) members.push_back(&s);
        unique(members, "state", "this script");
    }
    {
        std::vector<const Declaration*> members;
        for (const auto& c : script.customEvents) members.push_back(&c);
        unique(members, "custom event", "this script");
    }
    return problems;
}

// ---------------------------------------------------------------------------
// CodeGenerator

class BodyEmitter {
public:
    struct Local {
        std::string name;
        Type type;
    };
    struct Storage {
        std::string name;
        bool isProperty{false};
        std::optional<PexValue> receiver;
    };

    BodyEmitter(CodeGenerator& gen, PexFunction& fn, const CallableDecl& decl, const Type& returnType)
        : gen_(gen), fn_(fn), decl_(decl), returnType_(returnType) {}

    void run();

private:
    int add(const std::string& mnemonic, const std::vector<PexValue>& args);
    int here() const { return static_cast<int>(fn_.instructions.size()); }
    void patchJump(int jumpIndex, int target);
    static PexValue id(const std::string& name) {
        return PexValue{PexValueType::Identifier, name, 0, 0.0F, false};
    }
    static PexValue intValue(int value) {
        return PexValue{PexValueType::Integer, "", value, 0.0F, false};
    }
    static PexValue noneValue() { return PexValue{PexValueType::None, "", 0, 0.0F, false}; }

    std::string declareLocal(const std::string& sourceName, const Type& type);
    std::optional<Local> findLocal(const std::string& name);
    std::string noneVar();
    std::string allocTemp(const Type& type);
    void releaseTemps(int mark);

    void emitBlock(const std::vector<StatementPtr>& body);
    void emitStatement(const Statement* statement);
    void emitIf(const IfStatement& iff);
    void emitWhile(const WhileStatement& wh);
    void emitAssign(const AssignStatement& assign);
    static std::optional<TokenKind> compoundOperator(TokenKind assignOperator);

    void emitDiscarded(const Node* expression);
    PexValue value(const Node* expression);
    PexValue valueAs(const Node* expression, const Type& want);
    static bool needsNoCast(const Type& from, const Type& to);
    void emitInto(const Node* expression, const std::string& destination, const Type& destinationType);
    std::optional<PexValue> tryDirectValue(const Node* expression);
    std::optional<Storage> storageFor(const IdentifierExpression& id);
    PexValue propertyGet(const Storage& storage, const Node* at);
    PexValue materialise(const PexValue& value, const Type& type);

    void emitComputation(const Node* expression, const std::string& destination,
                         const Type& destinationType);
    void emitMemberRead(const MemberExpression& member, const std::string& destination);
    bool isOwnBackingProperty(const MemberExpression& member, const Binding& binding);
    static bool isAutoBackingProperty(const Binding& binding);
    void emitUnary(const UnaryExpression& unary, const std::string& destination,
                   const Type& destinationType);
    void emitBinary(const BinaryExpression& binary, const std::string& destination,
                    const Type& destinationType);
    PexValue binaryInto(const std::optional<std::string>& destination, const Type& type,
                        TokenKind op, const PexValue& left, const PexValue& right,
                        const Span& span);
    static std::optional<std::string> arithmeticMnemonic(TokenKind op, const Type& type);
    static bool isComparison(TokenKind op);
    void emitComparison(const BinaryExpression& binary, const std::string& destination);
    Type commonComparisonType(const Type& left, const Type& right);
    bool inherits(const std::string& child, const std::string& ancestor);
    void emitShortCircuit(const BinaryExpression& binary, const std::string& destination);

    void emitCall(const CallExpression& call, const std::string& destination);
    void emit(const std::string& mnemonic, const std::vector<PexValue>& parts);
    std::optional<std::vector<PexValue>> buildArguments(const CallExpression& call,
                                                        const CallableDecl& callee,
                                                        const Script* calleeOwner);
    void emitArrayBuiltin(const CallExpression& call, const Binding& binding,
                          const std::string& destination);
    void cannotEmit(const Node& node);

    CodeGenerator& gen_;
    PexFunction& fn_;
    const CallableDecl& decl_;
    const Type& returnType_;

    std::vector<std::unordered_map<std::string, Local>> scopes_;
    std::unordered_map<std::string, std::stack<std::string>> freeTemps_;
    std::vector<std::pair<std::string, std::string>> liveTemps_;
    std::set<std::string> usedLocalNames_;
    std::optional<std::string> noneVar_;
    int line_{0};
    int mangleCounter_{0};
};

void BodyEmitter::run() {
    scopes_.emplace_back();
    for (const auto& p : decl_.parameters) {
        usedLocalNames_.insert(p.name);
        scopes_.back()[lower(p.name)] = Local{p.name, gen_.typeOf(p.type)};
    }

    emitBlock(decl_.body);
    scopes_.clear();
}

int BodyEmitter::add(const std::string& mnemonic, const std::vector<PexValue>& args) {
    const auto op = opcodeFor(mnemonic);
    const auto& meta = PexFile::kOpCodes[op];
    PexInstruction instruction;
    instruction.opCode = op;
    instruction.mnemonic = meta.name;
    instruction.fixedArgCount = meta.args;
    instruction.hasVarArgs = meta.varargs;
    instruction.line = line_;
    instruction.args = args;
    fn_.instructions.push_back(std::move(instruction));
    return static_cast<int>(fn_.instructions.size()) - 1;
}

void BodyEmitter::patchJump(int jumpIndex, int target) {
    auto& instruction = fn_.instructions[static_cast<std::size_t>(jumpIndex)];
    auto& operand = instruction.args.back();
    operand.type = PexValueType::Integer;
    operand.int_ = target - jumpIndex;
}

std::string BodyEmitter::declareLocal(const std::string& sourceName, const Type& type) {
    auto emitted = sourceName;
    if (!usedLocalNames_.insert(emitted).second) {
        emitted = "::mangled_" + sourceName + "_" + std::to_string(mangleCounter_++);
        usedLocalNames_.insert(emitted);
    }

    PexTypedName local;
    local.name = emitted;
    local.type = gen_.pexTypeName(type);
    fn_.locals.push_back(std::move(local));
    scopes_.back()[lower(sourceName)] = Local{emitted, type};
    return emitted;
}

std::optional<BodyEmitter::Local> BodyEmitter::findLocal(const std::string& name) {
    const auto key = lower(name);
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        const auto found = it->find(key);
        if (found != it->end()) return found->second;
    }
    return std::nullopt;
}

std::string BodyEmitter::noneVar() {
    if (noneVar_) return *noneVar_;
    noneVar_ = "::nonevar";
    usedLocalNames_.insert(*noneVar_);
    PexTypedName local;
    local.name = *noneVar_;
    local.type = "None";
    fn_.locals.push_back(std::move(local));
    return *noneVar_;
}

std::string BodyEmitter::allocTemp(const Type& type) {
    const auto typeName = gen_.pexTypeName(type);
    const auto pool = freeTemps_.find(typeName);
    if (pool != freeTemps_.end() && !pool->second.empty()) {
        const auto reused = pool->second.top();
        pool->second.pop();
        liveTemps_.emplace_back(reused, typeName);
        return reused;
    }

    const auto name = "::temp" + std::to_string(gen_.tempCounter_++);
    PexTypedName local;
    local.name = name;
    local.type = typeName;
    fn_.locals.push_back(std::move(local));
    liveTemps_.emplace_back(name, typeName);
    return name;
}

void BodyEmitter::releaseTemps(int mark) {
    for (std::size_t i = liveTemps_.size(); i > static_cast<std::size_t>(mark); --i) {
        const auto& [name, type] = liveTemps_[i - 1];
        freeTemps_[type].push(name);
    }
    if (static_cast<int>(liveTemps_.size()) > mark) {
        liveTemps_.erase(liveTemps_.begin() + mark, liveTemps_.end());
    }
}

void BodyEmitter::emitBlock(const std::vector<StatementPtr>& body) {
    scopes_.emplace_back();
    for (const auto& statement : body) emitStatement(statement.get());
    scopes_.pop_back();
}

void BodyEmitter::emitStatement(const Statement* statement) {
    const int mark = static_cast<int>(liveTemps_.size());
    line_ = statement->span.line;

    switch (statement->nodeKind()) {
        case NodeKind::DefineStatement: {
            const auto* define = static_cast<const DefineStatement*>(statement);
            const Type& type = gen_.typeOf(define->type);
            const auto name = declareLocal(define->name, type);

            if (define->initializer) {
                emitInto(define->initializer.get(), name, type);
            } else {
                add("assign", {id(name), CodeGenerator::zeroValue(type)});
            }
            break;
        }

        case NodeKind::AssignStatement:
            emitAssign(*static_cast<const AssignStatement*>(statement));
            break;

        case NodeKind::ExpressionStatement: {
            const auto* expr = static_cast<const ExpressionStatement*>(statement);
            emitDiscarded(expr->expression.get());
            break;
        }

        case NodeKind::ReturnStatement: {
            const auto* ret = static_cast<const ReturnStatement*>(statement);
            if (!ret->value || returnType_.kind == TypeKind::None) {
                add("return", {noneValue()});
            } else {
                add("return", {valueAs(ret->value.get(), returnType_)});
            }
            break;
        }

        case NodeKind::IfStatement:
            emitIf(*static_cast<const IfStatement*>(statement));
            break;

        case NodeKind::WhileStatement:
            emitWhile(*static_cast<const WhileStatement*>(statement));
            break;

        default:
            break;
    }

    releaseTemps(mark);
}

void BodyEmitter::emitIf(const IfStatement& iff) {
    std::vector<int> toEnd;

    for (const auto& branch : iff.branches) {
        line_ = branch.condition->span.line;
        const auto condition = value(branch.condition.get());
        const int jumpPastBranch = add("jmpf", {condition, intValue(0)});

        emitBlock(branch.body);

        toEnd.push_back(add("jmp", {intValue(0)}));
        patchJump(jumpPastBranch, here());
    }

    if (!iff.elseBody.empty()) emitBlock(iff.elseBody);

    for (const int jump : toEnd) patchJump(jump, here());
}

void BodyEmitter::emitWhile(const WhileStatement& wh) {
    const int top = here();
    line_ = wh.condition->span.line;
    const auto condition = value(wh.condition.get());
    const int exit = add("jmpf", {condition, intValue(0)});

    emitBlock(wh.body);
    patchJump(add("jmp", {intValue(0)}), top);
    patchJump(exit, here());
}

void BodyEmitter::emitAssign(const AssignStatement& assign) {
    const Type& targetType = gen_.typeOf(assign.target.get());
    const Node* valueNode = assign.value.get();
    const auto op = compoundOperator(assign.op);

    switch (assign.target->nodeKind()) {
        case NodeKind::IdentifierExpression: {
            const auto* idNode = static_cast<const IdentifierExpression*>(assign.target.get());
            const auto slot = storageFor(*idNode);
            if (!slot) return;
            if (slot->isProperty) {
                if (!op) {
                    // StoreOrder: evaluate the stored value before the receiver.
                    const auto stored = materialise(valueAs(valueNode, targetType), targetType);
                    add("propset", {id(slot->name), *slot->receiver, stored});
                    return;
                }
                const auto current = propertyGet(*slot, idNode);
                const auto result = binaryInto(std::nullopt, targetType, *op, current,
                                               valueAs(valueNode, targetType), assign.span);
                add("propset", {id(slot->name), *slot->receiver, materialise(result, targetType)});
                return;
            }

            if (!op) {
                emitInto(valueNode, slot->name, targetType);
                return;
            }
            binaryInto(slot->name, targetType, *op, id(slot->name),
                       valueAs(valueNode, targetType), assign.span);
            return;
        }

        case NodeKind::MemberExpression: {
            const auto* member = static_cast<const MemberExpression*>(assign.target.get());
            const Binding* binding = gen_.resolution_->bindingFor(member);
            if (!binding) {
                cannotEmit(*member);
                return;
            }

            if (binding->kind == BindingKind::StructMember) {
                if (!op) {
                    const auto stored = materialise(valueAs(valueNode, targetType), targetType);
                    add("struct_set", {value(member->target.get()), id(member->name), stored});
                    return;
                }
                const auto structValue = value(member->target.get());
                const auto newValue = [&] {
                    const auto current = allocTemp(targetType);
                    add("struct_get", {id(current), structValue, id(member->name)});
                    return binaryInto(std::nullopt, targetType, *op, id(current),
                                      valueAs(valueNode, targetType), assign.span);
                }();
                add("struct_set", {structValue, id(member->name), materialise(newValue, targetType)});
                return;
            }

            if (binding->kind == BindingKind::Property) {
                if (!op) {
                    const auto stored = materialise(valueAs(valueNode, targetType), targetType);
                    add("propset", {id(member->name), value(member->target.get()), stored});
                    return;
                }
                const auto receiver = value(member->target.get());
                const auto current = allocTemp(targetType);
                add("propget", {id(member->name), receiver, id(current)});
                const auto newValue = binaryInto(std::nullopt, targetType, *op, id(current),
                                                 valueAs(valueNode, targetType), assign.span);
                add("propset", {id(member->name), receiver, materialise(newValue, targetType)});
                return;
            }

            cannotEmit(*member);
            return;
        }

        case NodeKind::IndexExpression: {
            const auto* index = static_cast<const IndexExpression*>(assign.target.get());
            if (!op) {
                const auto stored = valueAs(valueNode, targetType);
                const auto target = value(index->target.get());
                const auto subscript = valueAs(index->index.get(), Type::int_());
                add("array_setelement", {target, subscript, stored});
                return;
            }

            const auto array = value(index->target.get());
            const auto subscript = valueAs(index->index.get(), Type::int_());
            const auto current = allocTemp(targetType);
            add("array_getelement", {id(current), array, subscript});
            const auto newValue = binaryInto(std::nullopt, targetType, *op, id(current),
                                             valueAs(valueNode, targetType), assign.span);
            add("array_setelement", {array, subscript, newValue});
            return;
        }

        default:
            cannotEmit(*assign.target);
            return;
    }
}

std::optional<TokenKind> BodyEmitter::compoundOperator(TokenKind assignOperator) {
    switch (assignOperator) {
        case TokenKind::PlusAssign: return TokenKind::Plus;
        case TokenKind::MinusAssign: return TokenKind::Minus;
        case TokenKind::StarAssign: return TokenKind::Star;
        case TokenKind::SlashAssign: return TokenKind::Slash;
        case TokenKind::PercentAssign: return TokenKind::Percent;
        default: return std::nullopt;
    }
}

void BodyEmitter::emitDiscarded(const Node* expression) {
    const auto* call = dynamic_cast<const CallExpression*>(expression);
    if (!call) {
        value(expression);
        return;
    }

    const Binding* callBinding = gen_.resolution_->bindingFor(call);
    if (!callBinding) callBinding = gen_.resolution_->bindingFor(call->callee.get());
    if (callBinding && gen_.excludedBy(*callBinding)) return;

    const Type& type = gen_.typeOf(call);
    if (type.kind == TypeKind::None) {
        emitCall(*call, noneVar());
        return;
    }
    emitCall(*call, allocTemp(type));
}

PexValue BodyEmitter::value(const Node* expression) {
    const auto direct = tryDirectValue(expression);
    if (direct) return *direct;

    const Type& type = gen_.typeOf(expression);
    if (type.kind == TypeKind::Error) {
        cannotEmit(*expression);
        return noneValue();
    }

    const auto temp = allocTemp(type);
    emitComputation(expression, temp, type);
    return id(temp);
}

PexValue BodyEmitter::valueAs(const Node* expression, const Type& want) {
    const Type& natural = gen_.typeOf(expression);
    if (natural == want || needsNoCast(natural, want)) return value(expression);

    if (natural.kind == TypeKind::Int && want.kind == TypeKind::Float) {
        const auto folded = CodeGenerator::tryConstant(expression, &want);
        if (folded && folded->type == PexValueType::Float) return *folded;
    }

    if (want.kind == TypeKind::Bool &&
        (natural.kind == TypeKind::Int || natural.kind == TypeKind::Float)) {
        const auto folded = CodeGenerator::tryConstant(expression, &want);
        if (folded && folded->type == PexValueType::Bool) return *folded;
    }

    const auto source = value(expression);
    const auto temp = allocTemp(want);
    add("cast", {id(temp), source});
    return id(temp);
}

bool BodyEmitter::needsNoCast(const Type& from, const Type& to) {
    return (from.kind == TypeKind::None && to.isReference()) || from.kind == TypeKind::Error ||
           to.kind == TypeKind::Error;
}

void BodyEmitter::emitInto(const Node* expression, const std::string& destination,
                           const Type& destinationType) {
    const Type& natural = gen_.typeOf(expression);

    if (natural == destinationType) {
        const auto direct = tryDirectValue(expression);
        if (direct) {
            add("assign", {id(destination), *direct});
            return;
        }
        emitComputation(expression, destination, destinationType);
        return;
    }

    if (needsNoCast(natural, destinationType)) {
        add("assign", {id(destination), value(expression)});
        return;
    }

    if (natural.kind == TypeKind::Int && destinationType.kind == TypeKind::Float) {
        const auto folded = CodeGenerator::tryConstant(expression, &destinationType);
        if (folded && folded->type == PexValueType::Float) {
            add("assign", {id(destination), *folded});
            return;
        }
    }

    add("cast", {id(destination), value(expression)});
}

std::optional<PexValue> BodyEmitter::tryDirectValue(const Node* expression) {
    if (expression->nodeKind() == NodeKind::LiteralExpression) {
        return CodeGenerator::tryConstant(expression, &gen_.typeOf(expression));
    }

    if (expression->nodeKind() == NodeKind::IdentifierExpression) {
        const auto* idNode = static_cast<const IdentifierExpression*>(expression);
        const auto storage = storageFor(*idNode);
        if (!storage) return std::nullopt;
        if (storage->isProperty) return std::nullopt;
        return id(storage->name);
    }

    return std::nullopt;
}

std::optional<BodyEmitter::Storage> BodyEmitter::storageFor(const IdentifierExpression& idNode) {
    const Binding* binding = gen_.resolution_->bindingFor(&idNode);
    if (!binding) {
        cannotEmit(idNode);
        return std::nullopt;
    }

    switch (binding->kind) {
        case BindingKind::Local:
        case BindingKind::Parameter: {
            const auto local = findLocal(idNode.name);
            if (!local) {
                cannotEmit(idNode);
                return std::nullopt;
            }
            return Storage{local->name, false, std::nullopt};
        }

        case BindingKind::ScriptVariable:
            return Storage{binding->name, false, std::nullopt};

        case BindingKind::SelfKeyword:
            return Storage{"self", false, std::nullopt};

        case BindingKind::Property: {
            if (isAutoBackingProperty(*binding)) {
                return Storage{CodeGenerator::backingVarName(binding->name), false, std::nullopt};
            }
            return Storage{binding->name, true, id("self")};
        }

        default:
            cannotEmit(idNode);
            return std::nullopt;
    }
}

PexValue BodyEmitter::propertyGet(const Storage& storage, const Node* at) {
    const auto temp = allocTemp(gen_.typeOf(at));
    add("propget", {id(storage.name), *storage.receiver, id(temp)});
    return id(temp);
}

PexValue BodyEmitter::materialise(const PexValue& value, const Type& type) {
    if (value.type == PexValueType::Identifier && value.str.rfind("::temp", 0) == 0) return value;
    const auto temp = allocTemp(type);
    add("assign", {id(temp), value});
    return id(temp);
}

void BodyEmitter::emitComputation(const Node* expression, const std::string& destination,
                                  const Type& destinationType) {
    switch (expression->nodeKind()) {
        case NodeKind::LiteralExpression:
        case NodeKind::IdentifierExpression: {
            const auto direct = tryDirectValue(expression);
            if (direct) {
                add("assign", {id(destination), *direct});
                return;
            }
            if (expression->nodeKind() == NodeKind::IdentifierExpression) {
                const auto* idNode = static_cast<const IdentifierExpression*>(expression);
                const auto storage = storageFor(*idNode);
                if (storage && storage->isProperty) {
                    add("propget", {id(storage->name), *storage->receiver, id(destination)});
                    return;
                }
            }
            cannotEmit(*expression);
            return;
        }

        case NodeKind::MemberExpression:
            emitMemberRead(*static_cast<const MemberExpression*>(expression), destination);
            return;

        case NodeKind::IndexExpression: {
            const auto* index = static_cast<const IndexExpression*>(expression);
            add("array_getelement",
                {id(destination), value(index->target.get()),
                 valueAs(index->index.get(), Type::int_())});
            return;
        }

        case NodeKind::CallExpression:
            emitCall(*static_cast<const CallExpression*>(expression), destination);
            return;

        case NodeKind::UnaryExpression:
            emitUnary(*static_cast<const UnaryExpression*>(expression), destination, destinationType);
            return;

        case NodeKind::BinaryExpression:
            emitBinary(*static_cast<const BinaryExpression*>(expression), destination,
                       destinationType);
            return;

        case NodeKind::CastExpression: {
            const auto* cast = static_cast<const CastExpression*>(expression);
            const Type& natural = gen_.typeOf(cast->operand.get());
            const Type& target = gen_.typeOf(cast->type);
            if (natural == target || needsNoCast(natural, target)) {
                add("assign", {id(destination), value(cast->operand.get())});
                return;
            }
            add("cast", {id(destination), value(cast->operand.get())});
            return;
        }

        case NodeKind::TypeCheckExpression: {
            const auto* check = static_cast<const TypeCheckExpression*>(expression);
            add("is", {id(destination), value(check->operand.get()),
                       id(gen_.pexTypeName(gen_.typeOf(check->type), check->type.span))});
            return;
        }

        case NodeKind::NewArrayExpression: {
            const auto* newArray = static_cast<const NewArrayExpression*>(expression);
            add("array_create", {id(destination), valueAs(newArray->size.get(), Type::int_())});
            return;
        }

        case NodeKind::NewStructExpression:
            add("struct_create", {id(destination)});
            return;

        default:
            cannotEmit(*expression);
            return;
    }
}

void BodyEmitter::emitMemberRead(const MemberExpression& member, const std::string& destination) {
    const Binding* binding = gen_.resolution_->bindingFor(&member);
    if (!binding) {
        cannotEmit(member);
        return;
    }

    switch (binding->kind) {
        case BindingKind::ArrayMember:
            if (iequals(member.name, "length")) {
                add("array_length", {id(destination), value(member.target.get())});
                return;
            }
            cannotEmit(member);
            return;

        case BindingKind::StructMember:
            add("struct_get", {id(destination), value(member.target.get()), id(member.name)});
            return;

        case BindingKind::Property: {
            if (isOwnBackingProperty(member, *binding)) {
                add("assign", {id(destination), id(CodeGenerator::backingVarName(binding->name))});
                return;
            }
            add("propget", {id(binding->name), value(member.target.get()), id(destination)});
            return;
        }

        default:
            cannotEmit(member);
            return;
    }
}

bool BodyEmitter::isOwnBackingProperty(const MemberExpression& member, const Binding& binding) {
    if (!isAutoBackingProperty(binding)) return false;
    const Binding* targetBinding = gen_.resolution_->bindingFor(member.target.get());
    return targetBinding && targetBinding->kind == BindingKind::SelfKeyword;
}

bool BodyEmitter::isAutoBackingProperty(const Binding& binding) {
    const auto* prop = dynamic_cast<const PropertyDecl*>(binding.declaration);
    return prop && prop->kind == PropertyKind::Auto;
}

void BodyEmitter::emitUnary(const UnaryExpression& unary, const std::string& destination,
                            const Type&) {
    if (unary.op == TokenKind::Not) {
        add("not", {id(destination), value(unary.operand.get())});
        return;
    }

    const Type& operandType = gen_.typeOf(unary.operand.get());
    std::optional<std::string> mnemonic;
    if (operandType.kind == TypeKind::Float) mnemonic = "fneg";
    else if (operandType.kind == TypeKind::Int) mnemonic = "ineg";
    if (!mnemonic) {
        cannotEmit(unary);
        return;
    }
    add(*mnemonic, {id(destination), valueAs(unary.operand.get(), operandType)});
}

void BodyEmitter::emitBinary(const BinaryExpression& binary, const std::string& destination,
                             const Type&) {
    if (binary.op == TokenKind::And || binary.op == TokenKind::Or) {
        emitShortCircuit(binary, destination);
        return;
    }

    if (isComparison(binary.op)) {
        emitComparison(binary, destination);
        return;
    }

    const Type& result = gen_.typeOf(&binary);
    const auto left = valueAs(binary.left.get(), result);
    const auto right = valueAs(binary.right.get(), result);
    binaryInto(destination, result, binary.op, left, right, binary.span);
}

PexValue BodyEmitter::binaryInto(const std::optional<std::string>& destination, const Type& type,
                                 TokenKind op, const PexValue& left, const PexValue& right,
                                 const Span& span) {
    const auto mnemonic = arithmeticMnemonic(op, type);
    if (!mnemonic) {
        gen_.report(DiagnosticCode::CannotEmit,
                    "There is no instruction for this operator on '" + type.toString() + "'.", span);
        return noneValue();
    }

    const auto slot = destination ? *destination : allocTemp(type);
    add(*mnemonic, {id(slot), left, right});
    return id(slot);
}

std::optional<std::string> BodyEmitter::arithmeticMnemonic(TokenKind op, const Type& type) {
    if (type.kind == TypeKind::String) return op == TokenKind::Plus ? std::optional<std::string>("strcat") : std::nullopt;

    const bool isFloat = type.kind == TypeKind::Float;
    if (type.kind != TypeKind::Int && !isFloat) return std::nullopt;

    switch (op) {
        case TokenKind::Plus: return isFloat ? "fadd" : "iadd";
        case TokenKind::Minus: return isFloat ? "fsub" : "isub";
        case TokenKind::Star: return isFloat ? "fmul" : "imul";
        case TokenKind::Slash: return isFloat ? "fdiv" : "idiv";
        case TokenKind::Percent: return isFloat ? std::nullopt : std::optional<std::string>("imod");
        default: return std::nullopt;
    }
}

bool BodyEmitter::isComparison(TokenKind op) {
    switch (op) {
        case TokenKind::Equal:
        case TokenKind::NotEqual:
        case TokenKind::Less:
        case TokenKind::LessEqual:
        case TokenKind::Greater:
        case TokenKind::GreaterEqual:
            return true;
        default:
            return false;
    }
}

void BodyEmitter::emitComparison(const BinaryExpression& binary, const std::string& destination) {
    const auto common = commonComparisonType(gen_.typeOf(binary.left.get()),
                                             gen_.typeOf(binary.right.get()));

    const auto left = valueAs(binary.left.get(), common);
    const auto right = valueAs(binary.right.get(), common);

    std::string mnemonic;
    switch (binary.op) {
        case TokenKind::Equal:
        case TokenKind::NotEqual: mnemonic = "cmp_eq"; break;
        case TokenKind::Less: mnemonic = "cmp_lt"; break;
        case TokenKind::LessEqual: mnemonic = "cmp_lte"; break;
        case TokenKind::Greater: mnemonic = "cmp_gt"; break;
        default: mnemonic = "cmp_gte"; break;
    }

    add(mnemonic, {id(destination), left, right});
    if (binary.op == TokenKind::NotEqual) add("not", {id(destination), id(destination)});
}

Type BodyEmitter::commonComparisonType(const Type& left, const Type& right) {
    if (left == right) return left;
    if (left.kind == TypeKind::Error) return right;
    if (right.kind == TypeKind::Error) return left;
    if (left.kind == TypeKind::None) return right;
    if (right.kind == TypeKind::None) return left;
    if (left.kind == TypeKind::Float || right.kind == TypeKind::Float) return Type::float_();
    if (left.kind == TypeKind::String || right.kind == TypeKind::String) return Type::string_();
    if (left.kind == TypeKind::Var || right.kind == TypeKind::Var) return Type::var_();

    if (left.kind == TypeKind::Object && right.kind == TypeKind::Object) {
        if (inherits(right.name, left.name)) return left;
        if (inherits(left.name, right.name)) return right;
        return left;
    }

    return left;
}

bool BodyEmitter::inherits(const std::string& child, const std::string& ancestor) {
    const Script* script = gen_.index_.resolve(child);
    if (!script) return false;
    for (const Script* level : gen_.index_.baseChain(script)) {
        if (iequals(level->name, ancestor)) return true;
    }
    return false;
}

void BodyEmitter::emitShortCircuit(const BinaryExpression& binary, const std::string& destination) {
    add("cast", {id(destination), value(binary.left.get())});
    const int skip =
        add(binary.op == TokenKind::And ? "jmpf" : "jmpt", {id(destination), intValue(0)});
    add("cast", {id(destination), value(binary.right.get())});
    patchJump(skip, here());
}

void BodyEmitter::emitCall(const CallExpression& call, const std::string& destination) {
    const Binding* binding = gen_.resolution_->bindingFor(&call);
    if (!binding) binding = gen_.resolution_->bindingFor(call.callee.get());
    if (!binding) {
        cannotEmit(call);
        return;
    }

    const auto excluded = gen_.excludedBy(*binding);
    if (excluded) {
        gen_.report(DiagnosticCode::CannotEmit,
                    "'" + binding->name + "' is " + *excluded +
                        " and is not part of this build, but its result is used here. Only a call "
                        "written as a whole statement can be dropped; there is no value to "
                        "substitute for this one.",
                    call.span);
        return;
    }

    if (binding->kind == BindingKind::ArrayMember) {
        emitArrayBuiltin(call, *binding, destination);
        return;
    }

    const auto* callee = dynamic_cast<const CallableDecl*>(binding->declaration);
    if (!callee) {
        gen_.report(DiagnosticCode::UnknownCallTarget,
                    "'" + binding->name +
                        "' has no source declaration on the import roots, so its parameter list "
                        "and defaults are unknown and the call cannot be emitted. Add the root "
                        "that declares it.",
                    call.span);
        return;
    }

    if (call.callee->nodeKind() == NodeKind::MemberExpression) {
        const auto* member = static_cast<const MemberExpression*>(call.callee.get());
        const Binding* targetBinding = gen_.resolution_->bindingFor(member->target.get());

        if (targetBinding && targetBinding->kind == BindingKind::ParentKeyword) {
            const auto parentArgs = buildArguments(call, *callee, binding->owner);
            if (!parentArgs) return;
            std::vector<PexValue> parts = {id(binding->name), id(destination)};
            parts.insert(parts.end(), parentArgs->begin(), parentArgs->end());
            emit("callparent", parts);
            return;
        }

        if (targetBinding && targetBinding->kind == BindingKind::Script) {
            const auto staticArgs = buildArguments(call, *callee, binding->owner);
            if (!staticArgs) return;
            std::vector<PexValue> parts = {
                id(targetBinding->owner ? targetBinding->owner->name : targetBinding->name),
                id(binding->name), id(destination)};
            parts.insert(parts.end(), staticArgs->begin(), staticArgs->end());
            emit("callstatic", parts);
            return;
        }

        const auto receiver = value(member->target.get());
        const auto methodArgs = buildArguments(call, *callee, binding->owner);
        if (!methodArgs) return;
        std::vector<PexValue> parts = {id(binding->name), receiver, id(destination)};
        parts.insert(parts.end(), methodArgs->begin(), methodArgs->end());
        emit("callmethod", parts);
        return;
    }

    const auto arguments = buildArguments(call, *callee, binding->owner);
    if (!arguments) return;

    const auto* fn = dynamic_cast<const FunctionDecl*>(callee);
    if (fn && fn->isGlobal) {
        std::vector<PexValue> parts = {
            id(binding->owner ? binding->owner->name : gen_.script_->name), id(binding->name),
            id(destination)};
        parts.insert(parts.end(), arguments->begin(), arguments->end());
        emit("callstatic", parts);
        return;
    }

    std::vector<PexValue> parts = {id(binding->name), id("self"), id(destination)};
    parts.insert(parts.end(), arguments->begin(), arguments->end());
    emit("callmethod", parts);
}

void BodyEmitter::emit(const std::string& mnemonic, const std::vector<PexValue>& parts) {
    add(mnemonic, parts);
}

std::optional<std::vector<PexValue>> BodyEmitter::buildArguments(const CallExpression& call,
                                                                 const CallableDecl& callee,
                                                                 const Script* calleeOwner) {
    std::vector<const Node*> slots(callee.parameters.size(), nullptr);
    std::size_t positional = 0;
    bool named = false;

    for (const auto& argument : call.arguments) {
        if (!argument.name) {
            if (named) {
                gen_.report(DiagnosticCode::ParameterOrder,
                            "A positional argument cannot follow a named one.", argument.span);
                return std::nullopt;
            }
            if (positional >= slots.size()) {
                gen_.report(DiagnosticCode::ArgumentCount,
                            "'" + callee.name + "' takes " + std::to_string(slots.size()) +
                                " argument(s).",
                            argument.span);
                return std::nullopt;
            }
            slots[positional++] = argument.value.get();
            continue;
        }

        named = true;
        std::size_t at = callee.parameters.size();
        for (std::size_t i = 0; i < callee.parameters.size(); ++i) {
            if (iequals(callee.parameters[i].name, *argument.name)) {
                at = i;
                break;
            }
        }
        if (at == callee.parameters.size()) {
            gen_.report(DiagnosticCode::UnknownArgumentName,
                        "'" + callee.name + "' has no parameter named '" + *argument.name + "'.",
                        argument.nameSpan);
            return std::nullopt;
        }
        slots[at] = argument.value.get();
    }

    // Custom-event qualification needs the receiver script: the C# resolves it
    // from the callee member's target type, or the current script.
    std::optional<std::string> receiverScript;
    if (call.callee->nodeKind() == NodeKind::MemberExpression) {
        const auto* member = static_cast<const MemberExpression*>(call.callee.get());
        const Type& receiver = gen_.typeOf(member->target.get());
        if (receiver.kind == TypeKind::Object) receiverScript = receiver.name;
    }
    if (!receiverScript) receiverScript = gen_.script_->name;

    std::vector<PexValue> operands;
    operands.reserve(slots.size());
    for (std::size_t i = 0; i < slots.size(); ++i) {
        const auto& parameter = callee.parameters[i];
        const auto parameterType = gen_.foreignType(parameter.type, calleeOwner);

        if (slots[i]) {
            if (parameter.semantic() == ParameterSemantic::CustomEventName) {
                if (const auto qualified =
                        gen_.qualifyCustomEvent(*slots[i], slots, static_cast<int>(i),
                                                receiverScript ? &*receiverScript : nullptr)) {
                    operands.push_back(*qualified);
                    continue;
                }
            }
            operands.push_back(valueAs(slots[i], parameterType));
            continue;
        }

        if (!parameter.defaultValue) {
            gen_.report(DiagnosticCode::ArgumentCount,
                        "'" + callee.name + "' has no default for '" + parameter.name +
                            "', so it must be given.",
                        call.span);
            return std::nullopt;
        }

        const auto constant = CodeGenerator::tryConstant(parameter.defaultValue.get(), &parameterType);
        if (!constant) {
            gen_.report(DiagnosticCode::NonConstantInitializer,
                        "The default for '" + parameter.name +
                            "' is not a literal, so it cannot be materialised at the call site.",
                        call.span);
            return std::nullopt;
        }
        operands.push_back(*constant);
    }

    return operands;
}

namespace {
const std::unordered_map<std::string, std::pair<int, int>>& arrayBuiltinArity() {
    static const std::unordered_map<std::string, std::pair<int, int>> arity = {
        {"find", {1, 2}},       {"rfind", {1, 2}},       {"findstruct", {2, 3}},
        {"rfindstruct", {2, 3}}, {"add", {1, 2}},        {"insert", {2, 2}},
        {"remove", {1, 2}},     {"removelast", {0, 0}},  {"clear", {0, 0}},
    };
    return arity;
}
}  // namespace

void BodyEmitter::emitArrayBuiltin(const CallExpression& call, const Binding& binding,
                                   const std::string& destination) {
    const auto* member = dynamic_cast<const MemberExpression*>(call.callee.get());
    if (!member) {
        cannotEmit(call);
        return;
    }

    std::vector<const Node*> arguments;
    for (const auto& argument : call.arguments) arguments.push_back(argument.value.get());

    const auto arity = arrayBuiltinArity().find(lower(binding.name));
    if (arity == arrayBuiltinArity().end()) {
        gen_.report(DiagnosticCode::CannotEmit,
                    "The array built-in '" + binding.name +
                        "' has no Fallout 4 opcode, so there is no instruction to emit for it.",
                    call.span);
        return;
    }

    if (arguments.size() < static_cast<std::size_t>(arity->second.first) ||
        arguments.size() > static_cast<std::size_t>(arity->second.second)) {
        std::string wanted = arity->second.first == arity->second.second
                                 ? std::to_string(arity->second.first)
                                 : std::to_string(arity->second.first) + " to " +
                                       std::to_string(arity->second.second);
        gen_.report(DiagnosticCode::ArgumentCount,
                    "'" + binding.name + "' on an array takes " + wanted +
                        " argument(s), not " + std::to_string(arguments.size()) +
                        ". Array built-ins are declared in no script, so this is not caught "
                        "earlier.",
                    call.span);
        return;
    }

    const Type& arrayType = gen_.typeOf(member->target.get());
    const Type& element = arrayType.element ? *arrayType.element : Type::error();
    const auto array = value(member->target.get());

    const auto argumentAt = [&](std::size_t at, const Type& type, int fallback) {
        return at < arguments.size() ? valueAs(arguments[at], type) : intValue(fallback);
    };

    const auto key = lower(binding.name);
    if (key == "find") {
        add("array_findelement",
            {array, id(destination), valueAs(arguments[0], element), argumentAt(1, Type::int_(), 0)});
        return;
    }
    if (key == "rfind") {
        add("array_rfindelement",
            {array, id(destination), valueAs(arguments[0], element), argumentAt(1, Type::int_(), -1)});
        return;
    }
    if (key == "findstruct") {
        add("array_findstruct",
            {array, id(destination), value(arguments[0]), value(arguments[1]),
             argumentAt(2, Type::int_(), 0)});
        return;
    }
    if (key == "rfindstruct") {
        add("array_rfindstruct",
            {array, id(destination), value(arguments[0]), value(arguments[1]),
             argumentAt(2, Type::int_(), -1)});
        return;
    }
    if (key == "add") {
        add("array_add", {array, valueAs(arguments[0], element), argumentAt(1, Type::int_(), 1)});
        return;
    }
    if (key == "insert") {
        add("array_insert",
            {array, valueAs(arguments[0], element), valueAs(arguments[1], Type::int_())});
        return;
    }
    if (key == "remove") {
        add("array_remove",
            {array, valueAs(arguments[0], Type::int_()), argumentAt(1, Type::int_(), 1)});
        return;
    }
    if (key == "removelast") {
        add("array_removelast", {array});
        return;
    }
    if (key == "clear") {
        add("array_clear", {array});
        return;
    }

    cannotEmit(call);
}

void BodyEmitter::cannotEmit(const Node& node) {
    gen_.report(DiagnosticCode::CannotEmit,
                "This did not resolve to anything the back end can name, so no instruction was "
                "emitted for it. Nothing is guessed here; the file is refused instead.",
                node.span);
}

// ---------------------------------------------------------------------------
// CodeGenerator members

PexFile* CodeGenerator::generate(const Script& script, const Resolution& resolution,
                                 const CodeGenOptions& options,
                                 std::vector<Diagnostic>& diagnostics) {
    script_ = &script;
    resolution_ = &resolution;
    if (options.userFlags) {
        userFlags_ = options.userFlags;
    } else {
        ownedUserFlags_ = UserFlagTable::fallout4Default();
        userFlags_ = &ownedUserFlags_;
    }
    diagnostics_ = &diagnostics;
    debug_.clear();
    emitDebugOnly_ = options.emitDebugOnlyCode;
    emitBetaOnly_ = options.emitBetaOnlyCode;
    tempCounter_ = 0;

    auto pex = std::make_unique<PexFile>();
    pex->majorVersion = 3;
    pex->minorVersion = 9;
    pex->gameId = 2;
    pex->compilationTime = options.compilationTime;
    pex->modificationTime = options.modificationTime;
    pex->sourceFileName = options.sourceFileName;
    pex->userName = options.userName;
    pex->computerName = options.computerName;
    pex->hasDebugInfo = options.emitDebugInfo;
    pex->userFlags = userFlags_->flags();

    pex->objects.push_back(buildObject());

    if (options.emitDebugInfo) {
        pex->debugFunctions = std::move(debug_);
        pex->propertyGroups = buildPropertyGroups();
        pex->structOrders = buildStructOrders();
    }

    pex->rebuildStringTable();

    for (const auto& d : *diagnostics_) {
        if (d.severity == Severity::Error) return nullptr;
    }
    return pex.release();
}

PexObject CodeGenerator::buildObject() {
    PexObject obj;
    obj.name = script_->name;
    obj.parentClassName = parentClassName();
    obj.docString = script_->documentation.value_or("");
    obj.isConst = hasFlag(*script_, "const");
    obj.userFlags = userFlags_->maskFor(script_->flags);
    obj.autoStateName = "";
    for (const auto& state : script_->states) {
        if (state.isAuto) {
            obj.autoStateName = state.name;
            break;
        }
    }

    for (const auto& st : script_->structs) {
        PexStruct pexStruct;
        pexStruct.name = st.name;
        for (const auto& m : st.members) {
            PexStructMember member;
            member.name = m.name;
            member.type = pexTypeName(typeOf(m.type), m.type.span);
            member.userFlags = userFlags_->maskFor(m.flags);
            member.defaultValue = constantOrDefault(m.initializer.get(), typeOf(m.type));
            member.isConst = hasFlag(m, "const");
            member.docString = m.documentation.value_or("");
            pexStruct.members.push_back(std::move(member));
        }
        obj.structs.push_back(std::move(pexStruct));
    }

    for (const auto& p : script_->properties) {
        if (p.kind != PropertyKind::Auto) continue;
        PexVariable variable;
        variable.name = backingVarName(p.name);
        variable.type = pexTypeName(typeOf(p.type), p.type.span);
        variable.userFlags = userFlags_->maskFor(p.flags) & conditionalMask();
        variable.defaultValue = constantOrDefault(p.initializer.get(), typeOf(p.type));
        variable.isConst = hasFlag(p, "const");
        obj.variables.push_back(std::move(variable));
    }

    for (const auto& v : script_->variables) {
        PexVariable variable;
        variable.name = v.name;
        variable.type = pexTypeName(typeOf(v.type), v.type.span);
        variable.userFlags = userFlags_->maskFor(v.flags);
        variable.defaultValue = constantOrDefault(v.initializer.get(), typeOf(v.type));
        variable.isConst = hasFlag(v, "const");
        obj.variables.push_back(std::move(variable));
    }

    for (const auto& p : script_->properties) obj.properties.push_back(buildProperty(p));

    PexState emptyState;
    emptyState.name = "";
    for (const auto& fn : script_->functions) {
        emptyState.functions.push_back(buildFunction(fn, "", fn.name, 0));
    }
    for (const auto& ev : script_->events) {
        emptyState.functions.push_back(buildEvent(ev, ""));
    }
    obj.states.push_back(std::move(emptyState));

    for (const auto& state : script_->states) {
        PexState pexState;
        pexState.name = state.name;
        for (const auto& fn : state.functions) {
            pexState.functions.push_back(buildFunction(fn, state.name, fn.name, 0));
        }
        for (const auto& ev : state.events) {
            pexState.functions.push_back(buildEvent(ev, state.name));
        }
        obj.states.push_back(std::move(pexState));
    }

    return obj;
}

PexProperty CodeGenerator::buildProperty(const PropertyDecl& p) {
    const auto type = pexTypeName(typeOf(p.type), p.type.span);
    PexProperty prop;
    prop.name = p.name;
    prop.type = type;
    prop.docString = p.documentation.value_or("");
    prop.userFlags = userFlags_->maskFor(p.flags) & ~conditionalMask();

    switch (p.kind) {
        case PropertyKind::Auto:
            prop.flags = 0x07;
            prop.autoVarName = backingVarName(p.name);
            break;

        case PropertyKind::AutoReadOnly:
            prop.flags = 0x01;
            prop.readHandler = constantGetter(p, type);
            break;

        default:
            prop.flags = static_cast<std::uint8_t>((p.getter ? 0x01 : 0) | (p.setter ? 0x02 : 0));
            if (p.getter) prop.readHandler = buildFunction(*p.getter, "", p.name, 1);
            if (p.setter) prop.writeHandler = buildFunction(*p.setter, "", p.name, 2);
            break;
    }

    return prop;
}

PexFunction CodeGenerator::constantGetter(const PropertyDecl& property, const std::string& pexType) {
    const auto value = constantOrDefault(property.initializer.get(), typeOf(property.type));
    PexFunction fn;
    fn.name = "";
    fn.returnType = pexType;
    PexInstruction ret;
    ret.opCode = opcodeFor("return");
    ret.mnemonic = "return";
    ret.fixedArgCount = 1;
    ret.line = property.span.line;
    ret.args.push_back(value);
    fn.instructions.push_back(std::move(ret));

    PexDebugFunction df;
    df.objectName = script_->name;
    df.stateName = "";
    df.functionName = property.name;
    df.functionType = 1;
    df.lineNumbers.push_back(static_cast<std::uint16_t>(
        std::clamp(property.span.line, 0, static_cast<int>(0xFFFF))));
    debug_.push_back(std::move(df));
    return fn;
}

std::string CodeGenerator::backingVarName(const std::string& propertyName) {
    return "::" + propertyName + "_var";
}

std::string CodeGenerator::parentClassName() {
    if (script_->extends) {
        const Script* parent = index_.resolve(*script_->extends);
        return parent ? parent->name : *script_->extends;
    }
    return iequals(script_->name, "ScriptObject") ? "" : "ScriptObject";
}

std::vector<PexPropertyGroup> CodeGenerator::buildPropertyGroups() const {
    std::vector<PexPropertyGroup> groups;

    std::vector<std::string> ungrouped;
    for (const auto& p : script_->properties) {
        if (!p.groupName) ungrouped.push_back(p.name);
    }
    if (!ungrouped.empty()) {
        PexPropertyGroup group;
        group.objectName = script_->name;
        group.groupName = "";
        group.docString = "";
        group.propertyNames = std::move(ungrouped);
        groups.push_back(std::move(group));
    }

    for (const auto& g : script_->groups) {
        PexPropertyGroup group;
        group.objectName = script_->name;
        group.groupName = g.name;
        group.docString = g.documentation.value_or("");
        group.userFlags = userFlags_->maskFor(g.flags);
        for (const PropertyDecl* p : g.properties) group.propertyNames.push_back(p->name);
        groups.push_back(std::move(group));
    }
    return groups;
}

std::vector<PexStructOrder> CodeGenerator::buildStructOrders() const {
    std::vector<PexStructOrder> orders;
    for (const auto& st : script_->structs) {
        PexStructOrder order;
        order.objectName = script_->name;
        order.orderName = st.name;
        for (const auto& m : st.members) order.memberNames.push_back(m.name);
        orders.push_back(std::move(order));
    }
    return orders;
}

PexFunction CodeGenerator::buildFunction(const FunctionDecl& decl, const std::string& stateName,
                                         const std::string& debugName, std::uint8_t debugType) {
    const Type& returnType = decl.returnType ? typeOf(*decl.returnType) : Type::none();

    PexFunction fn;
    fn.name = debugType == 0 ? decl.name : "";
    fn.returnType = decl.returnType ? pexTypeName(returnType, decl.returnType->span) : "None";
    fn.docString = decl.documentation.value_or("");
    fn.userFlags = userFlags_->maskFor(decl.flags);
    fn.isGlobal = decl.isGlobal;
    fn.isNative = decl.isNative;

    emitBody(fn, decl, returnType, stateName, debugName, debugType);
    return fn;
}

PexFunction CodeGenerator::buildEvent(const EventDecl& decl, const std::string& stateName) {
    std::string name = decl.name;
    if (decl.remoteObjectType) name = "::remote_" + *decl.remoteObjectType + "_" + decl.name;

    PexFunction fn;
    fn.name = name;
    fn.returnType = "None";
    fn.docString = decl.documentation.value_or("");
    fn.userFlags = userFlags_->maskFor(decl.flags);
    fn.isGlobal = false;
    fn.isNative = decl.isNative;

    emitBody(fn, decl, Type::none(), stateName, name, 0);
    return fn;
}

void CodeGenerator::emitBody(PexFunction& fn, const CallableDecl& decl, const Type& returnType,
                             const std::string& stateName, const std::string& debugName,
                             std::uint8_t debugType) {
    for (const auto& p : decl.parameters) {
        PexTypedName param;
        param.name = p.name;
        param.type = pexTypeName(typeOf(p.type), p.type.span);
        fn.params.push_back(std::move(param));
    }

    if (decl.isNative) return;

    BodyEmitter emitter(*this, fn, decl, returnType);
    emitter.run();

    PexDebugFunction df;
    df.objectName = script_->name;
    df.stateName = stateName;
    df.functionName = debugName;
    df.functionType = debugType;
    df.lineNumbers.reserve(fn.instructions.size());
    for (const auto& instruction : fn.instructions) {
        df.lineNumbers.push_back(
            static_cast<std::uint16_t>(std::clamp(instruction.line, 0, static_cast<int>(0xFFFF))));
    }
    debug_.push_back(std::move(df));
}

const Type& CodeGenerator::typeOf(const Node* node) const {
    const Type* type = resolution_->typeOf(node);
    return type ? *type : Type::error();
}

std::optional<PexValue> CodeGenerator::qualifyCustomEvent(const Node& argument,
                                                          const std::vector<const Node*>& slots,
                                                          int index,
                                                          const std::string* receiverScript) const {
    const auto* literal = dynamic_cast<const LiteralExpression*>(&argument);
    if (!literal || literal->kind != LiteralKind::String) return std::nullopt;

    std::optional<std::string> target;
    if (receiverScript) target = *receiverScript;
    for (int i = index - 1; i >= 0; --i) {
        if (!slots[static_cast<std::size_t>(i)]) continue;
        const Type& type = typeOf(slots[static_cast<std::size_t>(i)]);
        if (type.kind == TypeKind::Object) {
            target = type.name;
            break;
        }
    }
    if (!target) return std::nullopt;

    const Script* declarer = declaringScriptOf(*target, literal->text);
    if (!declarer) return std::nullopt;

    PexValue value;
    value.type = PexValueType::String;
    value.str = lower(declarer->name) + "_" + literal->text;
    return value;
}

const Script* CodeGenerator::declaringScriptOf(const std::string& scriptName,
                                               const std::string& eventName) const {
    const Script* script = index_.resolve(scriptName);
    if (!script) return nullptr;

    for (const Script* ancestor : index_.baseChain(script)) {
        for (const auto& event : ancestor->customEvents) {
            if (iequals(event.name, eventName)) return ancestor;
        }
    }
    return nullptr;
}

Type CodeGenerator::foreignType(const TypeRef& reference, const Script* owner) const {
    const Type* known = resolution_->typeOf(&reference);
    if (known && known->kind != TypeKind::Error) return *known;
    if (!owner) return Type::error();

    const auto resolved = Resolver::resolveTypeName(index_, reference.name, owner);
    if (!resolved) return Type::error();
    return reference.isArray ? Type::arrayOf(*resolved) : *resolved;
}

void CodeGenerator::report(DiagnosticCode code, std::string message, const Span& span) {
    for (const auto& existing : *diagnostics_) {
        if (existing.code == code && existing.span == span && existing.message == message) return;
    }
    diagnostics_->push_back(Diagnostic{code, Severity::Error, std::move(message), span,
                                       script_->filePath});
}

std::string CodeGenerator::pexTypeName(const Type& type, const Span& at) {
    switch (type.kind) {
        case TypeKind::None: return "None";
        case TypeKind::Bool: return "Bool";
        case TypeKind::Int: return "Int";
        case TypeKind::Float: return "Float";
        case TypeKind::String: return "String";
        case TypeKind::Var: return "Var";
        case TypeKind::Object: return type.name;
        case TypeKind::Array: return pexTypeName(*type.element, at) + "[]";

        case TypeKind::Struct: {
            const auto split = type.name.find_last_of(':');
            if (split == std::string::npos || split == 0) return type.name;
            return type.name.substr(0, split) + "#" + type.name.substr(split + 1);
        }

        default:
            report(DiagnosticCode::CannotEmit,
                   "The type of this is not known, so no instruction can name it. This is a "
                   "resolver gap or a missing import root, not a syntax error.",
                   at);
            return "None";
    }
}

PexValue CodeGenerator::constantOrDefault(const Node* initializer, const Type& declared) {
    if (!initializer) return zeroValue(declared);
    const auto value = tryConstant(initializer, &declared);
    if (value) return *value;

    report(DiagnosticCode::NonConstantInitializer,
           "A variable, property or struct member may only be initialised with a literal; the "
           "format stores one value, not an expression.",
           initializer->span);
    return zeroValue(declared);
}

PexValue CodeGenerator::zeroValue(const Type& type) {
    switch (type.kind) {
        case TypeKind::Bool: return PexValue{PexValueType::Bool, "", 0, 0.0F, false};
        case TypeKind::Int: return PexValue{PexValueType::Integer, "", 0, 0.0F, false};
        case TypeKind::Float: return PexValue{PexValueType::Float, "", 0, 0.0F, false};
        case TypeKind::String: return PexValue{PexValueType::String, "", 0, 0.0F, false};
        default: return PexValue{PexValueType::None, "", 0, 0.0F, false};
    }
}

bool tryParseInt(const std::string& text, int& value) {
    try {
        if (text.size() > 2 && (text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))) {
            value = static_cast<int>(std::stoul(text.substr(2), nullptr, 16));
            return true;
        }
        value = std::stoi(text);
        return true;
    } catch (...) {
        return false;
    }
}

std::optional<PexValue> CodeGenerator::tryConstant(const Node* expression, const Type* want) {
    const auto* unary = dynamic_cast<const UnaryExpression*>(expression);
    if (unary && unary->op == TokenKind::Minus) {
        const auto inner = tryConstant(unary->operand.get(), want);
        if (!inner) return std::nullopt;
        switch (inner->type) {
            case PexValueType::Integer:
                return PexValue{PexValueType::Integer, "", -inner->int_, 0.0F, false};
            case PexValueType::Float:
                return PexValue{PexValueType::Float, "", 0, -inner->float_, false};
            default:
                return std::nullopt;
        }
    }

    const auto* literal = dynamic_cast<const LiteralExpression*>(expression);
    if (!literal) return std::nullopt;

    switch (literal->kind) {
        case LiteralKind::String:
            return PexValue{PexValueType::String, literal->text, 0, 0.0F, false};

        case LiteralKind::Bool:
            return PexValue{PexValueType::Bool, "", 0, 0.0F, iequals(literal->text, "true")};

        case LiteralKind::None:
            return PexValue{PexValueType::None, "", 0, 0.0F, false};

        case LiteralKind::Int: {
            int parsed = 0;
            if (!tryParseInt(literal->text, parsed)) return std::nullopt;

            if (want && want->kind == TypeKind::Float) {
                return PexValue{PexValueType::Float, "", 0, static_cast<float>(parsed), false};
            }
            if (want && want->kind == TypeKind::Bool) {
                return PexValue{PexValueType::Bool, "", 0, 0.0F, parsed != 0};
            }
            return PexValue{PexValueType::Integer, "", parsed, 0.0F, false};
        }

        case LiteralKind::Float: {
            auto text = literal->text;
            if (!text.empty() && (text.back() == 'f' || text.back() == 'F')) text.pop_back();
            float parsed = 0.0F;
            try {
                parsed = std::stof(text);
            } catch (...) {
                return std::nullopt;
            }
            if (want && want->kind == TypeKind::Bool) {
                return PexValue{PexValueType::Bool, "", 0, 0.0F, parsed != 0.0F};
            }
            return PexValue{PexValueType::Float, "", 0, parsed, false};
        }

        default:
            return std::nullopt;
    }
}

std::optional<std::string> CodeGenerator::excludedBy(const Binding& binding) const {
    const auto flagged = [&](std::string_view flag) {
        return (binding.declaration && hasFlag(*binding.declaration, flag)) ||
               (binding.owner && hasFlag(*binding.owner, flag));
    };

    if (!emitDebugOnly_ && flagged("debugonly")) return "DebugOnly";
    if (!emitBetaOnly_ && flagged("betaonly")) return "BetaOnly";
    return std::nullopt;
}

}  // namespace fo4ide::papyrus
