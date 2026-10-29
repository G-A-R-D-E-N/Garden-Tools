#include <fo4/graph/graph_type_resolver.hpp>

#include <algorithm>
#include <cctype>

#include <fo4/papyrus/resolver.hpp>

namespace fo4::graph {
namespace {

std::string lower(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

}

bool GenericBinding::tryGet(std::string_view variable, std::string& typeName,
                            bool& isArray) const {
    const auto found = bound.find(std::string(variable));
    if (found == bound.end()) {
        typeName = "";
        isArray = false;
        return false;
    }
    typeName = found->second.first;
    isArray = found->second.second;
    return true;
}

void GenericBinding::bind(std::string_view variable, std::string typeName, bool isArray) {
    const auto key = std::string(variable);
    if (!bound.contains(key)) bound[key] = {std::move(typeName), isArray};
}

bool GenericBinding::isBound(std::string_view variable) const {
    return bound.contains(std::string(variable));
}

GraphTypeResolver::GraphTypeResolver(papyrus::ScriptIndex& index, std::string selfType,
                                     std::optional<std::string> selfExtends)
    : index_(index), selfType_(std::move(selfType)), selfExtends_(std::move(selfExtends)) {}

bool GraphTypeResolver::inheritsFrom(std::string_view child, std::string_view ancestor) {
    if (iequals(child, ancestor)) return true;

    if (iequals(child, selfType_)) {
        return selfExtends_.has_value() && inheritsFrom(*selfExtends_, ancestor);
    }

    const auto key = std::pair<std::string, std::string>(lower(child), lower(ancestor));
    const auto known = inherits_.find(key);
    if (known != inherits_.end()) return known->second;

    const auto* script = index_.resolve(child);
    bool result = false;
    if (script == nullptr) {
        sourcesComplete_ = false;
    } else {
        const auto chain = index_.baseChain(script);
        result = std::any_of(chain.begin(), chain.end(), [&](const papyrus::Script* s) {
            return iequals(s->name, ancestor);
        });
    }

    inherits_[key] = result;
    return result;
}

std::pair<std::string, bool> GraphTypeResolver::typeOf(const PinTypeExpr* type,
                                                       const GenericBinding& generics) {
    if (type == nullptr) return {"", false};

    switch (type->form) {
        case PinTypeForm::Concrete:
            return {type->typeName, type->isArray};

        case PinTypeForm::SelfType:
            return {selfType_, false};

        case PinTypeForm::Any:
            return {"var", false};

        case PinTypeForm::Generic: {
            std::string name;
            bool array = false;
            return generics.tryGet(type->variable, name, array) ? std::pair(name, array)
                                                                : std::pair(std::string{}, false);
        }

        case PinTypeForm::ArrayOfGeneric: {
            std::string element;
            bool unused = false;
            return generics.tryGet(type->variable, element, unused)
                       ? std::pair(element, true)
                       : std::pair(std::string{}, false);
        }

        case PinTypeForm::ElementOfGeneric: {
            std::string whole;
            bool unused = false;
            return generics.tryGet(type->variable, whole, unused)
                       ? std::pair(whole, false)
                       : std::pair(std::string{}, false);
        }

        default:
            return {"", false};
    }
}

std::optional<papyrus::Type> GraphTypeResolver::resolve(std::string_view typeName, bool isArray,
                                                        const papyrus::Script* relativeTo) {
    if (typeName.empty()) return std::nullopt;

    const auto lowered = lower(typeName);
    std::optional<papyrus::Type> resolved;
    const papyrus::Type* element = nullptr;
    if (lowered == "bool") element = &papyrus::Type::bool_();
    else if (lowered == "int") element = &papyrus::Type::int_();
    else if (lowered == "float") element = &papyrus::Type::float_();
    else if (lowered == "string") element = &papyrus::Type::string_();
    else if (lowered == "var") element = &papyrus::Type::var_();
    else if (lowered == "none") element = &papyrus::Type::none();
    else if (relativeTo != nullptr) {
        resolved = papyrus::Resolver::resolveTypeName(index_, std::string(typeName), relativeTo);
        if (resolved) element = &*resolved;
    }

    if (element == nullptr) {
        sourcesComplete_ = false;
        return std::nullopt;
    }

    return isArray ? std::optional<papyrus::Type>(papyrus::Type::arrayOf(*element))
                   : std::optional<papyrus::Type>(*element);
}

GraphTypeResolver::WireVerdict GraphTypeResolver::judge(std::string_view fromType, bool fromArray,
                                                        std::string_view toType, bool toArray,
                                                        const papyrus::Script* relativeTo) {
    const auto from = resolve(fromType, fromArray, relativeTo);
    const auto to = resolve(toType, toArray, relativeTo);
    if (!from || !to) return WireVerdict::Unknown;

    const std::function<bool(const std::string&, const std::string&)> inherits =
        [this](const std::string& child, const std::string& ancestor) {
            return inheritsFrom(child, ancestor);
        };

    if (papyrus::isImplicit(*from, *to, inherits)) return WireVerdict::Implicit;
    if (papyrus::isExplicit(*from, *to, inherits)) return WireVerdict::NeedsCast;
    return WireVerdict::Incompatible;
}

GenericBinding GraphTypeResolver::solveGenerics(
    const GraphNode& node, const NodeDefinition& definition, const GraphDocument& document,
    const std::function<std::optional<std::pair<std::string, bool>>(const PinRef&)>& typeOfSource) {
    GenericBinding generics;

    struct PinEntry {
        const PinDefinition* pin;
        bool isInput;
    };
    std::vector<PinEntry> pins;
    const auto variant = definition.pinsFor(node, &document);
    for (const auto& pin : variant) {
        if (pin.kind != PinKind::Data || !pin.type) continue;
        pins.push_back(PinEntry{&pin, pin.direction == PinDirection::In});
    }
    std::stable_sort(pins.begin(), pins.end(), [](const PinEntry& a, const PinEntry& b) {
        if (a.isInput != b.isInput) return a.isInput;
        return a.pin->id < b.pin->id;
    });

    for (const auto& entry : pins) {
        const auto form = entry.pin->type->form;
        if (form != PinTypeForm::Generic && form != PinTypeForm::ArrayOfGeneric &&
            form != PinTypeForm::ElementOfGeneric) {
            continue;
        }
        if (generics.isBound(entry.pin->type->variable)) continue;

        const auto source = typeOfSource(PinRef{node.id, entry.pin->id});
        if (!source) continue;

        const auto& [typeName, isArray] = *source;
        if (typeName.empty()) continue;

        switch (form) {
            case PinTypeForm::Generic:
                generics.bind(entry.pin->type->variable, typeName, isArray);
                break;

            case PinTypeForm::ArrayOfGeneric:
                if (isArray) generics.bind(entry.pin->type->variable, typeName, false);
                break;

            case PinTypeForm::ElementOfGeneric:
                if (!isArray) generics.bind(entry.pin->type->variable, typeName, false);
                break;

            default:
                break;
        }
    }

    return generics;
}

}
