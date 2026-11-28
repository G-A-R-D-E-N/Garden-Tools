#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <fo4/graph/graph_compiler.hpp>

#include "graph_builder.hpp"

namespace fo4::graph::test {

inline std::string describe(const std::vector<GraphDiagnostic>& diagnostics) {
    std::string out;
    for (const auto& diagnostic : diagnostics) {
        if (!out.empty()) out += " | ";
        out += diagnostic.toString();
    }
    return out;
}

inline bool contains(const std::string& text, std::string_view needle) {
    return text.find(needle) != std::string::npos;
}

inline int occurrences(const std::string& text, std::string_view needle) {
    int count = 0;
    std::size_t at = 0;
    while ((at = text.find(needle, at)) != std::string::npos) {
        ++count;
        at += needle.size();
    }
    return count;
}

inline bool hasCode(const std::vector<GraphDiagnostic>& diagnostics, std::string_view code) {
    return std::any_of(diagnostics.begin(), diagnostics.end(),
                       [&](const GraphDiagnostic& d) { return d.code == code; });
}

inline const GraphDiagnostic* firstError(const std::vector<GraphDiagnostic>& diagnostics,
                                         std::string_view code) {
    for (const auto& diagnostic : diagnostics) {
        if (diagnostic.severity == GraphSeverity::Error && diagnostic.code == code) {
            return &diagnostic;
        }
    }
    return nullptr;
}

inline GraphCompileResult compileDoc(Env& env, GraphBuilder& graph, bool stopAfterSource = false) {
    GraphCompiler compiler(env.index, &env.palette);
    GraphCompileOptions options;
    options.stopAfterSource = stopAfterSource;
    options.treatPapyrusErrorsAsInternalFaults = true;
    graph.document.invalidate();
    return compiler.compile(graph.document, options);
}

inline std::string sourceOf(Env& env, GraphBuilder& graph) {
    const auto result = compileDoc(env, graph);
    EXPECT_TRUE(result.success()) << describe(result.diagnostics);
    return result.source.value_or("");
}

inline std::vector<std::string> trimmedLines(const std::string& text) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start <= text.size()) {
        auto end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        auto begin = start;
        auto stop = end;
        while (begin < stop && std::isspace(static_cast<unsigned char>(text[begin]))) ++begin;
        while (stop > begin && std::isspace(static_cast<unsigned char>(text[stop - 1]))) --stop;
        lines.push_back(text.substr(begin, stop - begin));
        start = end + 1;
    }
    return lines;
}

inline int indexOfLine(const std::vector<std::string>& lines, std::string_view wanted) {
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (lines[i] == wanted) return static_cast<int>(i);
    }
    return -1;
}

}
