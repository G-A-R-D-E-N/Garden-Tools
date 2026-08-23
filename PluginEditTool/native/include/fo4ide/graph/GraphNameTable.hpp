#pragma once

#include <set>
#include <string>
#include <string_view>

#include <fo4ide/papyrus/ScriptIndex.hpp>

namespace fo4ide::graph {

// Port of GraphNameTable: case-insensitive name reservation and allocation
// for generated locals. Pre-seeded with the Papyrus keywords and the
// functions/properties/variables of the owner's base chain, so generated
// names never collide with the script's world.
class GraphNameTable {
public:
    GraphNameTable(papyrus::ScriptIndex* index = nullptr, const papyrus::Script* owner = nullptr);

    void reserve(std::string_view name);
    bool isReserved(std::string_view name) const;

    // Allocates `hint` if free, otherwise hint2, hint3, ...; "value" when
    // the hint cleans to nothing.
    std::string allocate(std::string_view hint);

    static std::string clean(std::string_view hint);

private:
    std::set<std::string, std::less<>> taken_;
};

}  // namespace fo4ide::graph
