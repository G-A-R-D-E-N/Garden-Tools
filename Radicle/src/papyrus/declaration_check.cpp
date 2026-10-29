#include <fo4/papyrus/declaration_check.hpp>

#include <algorithm>
#include <functional>
#include <map>
#include <string>

namespace fo4::papyrus {

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

}
