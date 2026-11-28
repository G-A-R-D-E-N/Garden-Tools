#pragma once

#include <set>
#include <string>
#include <string_view>

#include <fo4/papyrus/script_index.hpp>

namespace fo4::graph {

class GraphNameTable {
public:
    GraphNameTable(papyrus::ScriptIndex* index = nullptr, const papyrus::Script* owner = nullptr);

    void reserve(std::string_view name);
    bool isReserved(std::string_view name) const;

    std::string allocate(std::string_view hint);

    static std::string clean(std::string_view hint);

private:
    std::set<std::string, std::less<>> taken_;
};

}
