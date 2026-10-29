#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <fo4/graph/graph_ir.hpp>

namespace fo4::graph {

struct GraphSourceMapEntry {
    int start{0};
    int length{0};
    int line{1};
    int depth{0};
    std::string nodeId;
    std::optional<std::string> pinId;

    bool contains(int offset) const {
        return offset >= start && offset < start + length;
    }
};

class GraphSourceMap {
public:
    const std::vector<GraphSourceMapEntry>& entries() const { return entries_; }

    void add(GraphSourceMapEntry entry) { entries_.push_back(std::move(entry)); }

    std::optional<GraphSourceMapEntry> find(int offset) const {
        std::optional<GraphSourceMapEntry> best;
        for (const auto& entry : entries_) {
            if (!entry.contains(offset)) continue;
            if (!best || entry.depth > best->depth) best = entry;
        }
        return best;
    }

    std::optional<GraphSourceMapEntry> findByLine(int line) const {
        std::optional<GraphSourceMapEntry> best;
        for (const auto& entry : entries_) {
            if (entry.line != line) continue;
            if (!best || entry.depth > best->depth) best = entry;
        }
        return best;
    }

    std::optional<GraphSourceMapEntry> functionAt(int offset) const {
        std::optional<GraphSourceMapEntry> best;
        for (const auto& entry : entries_) {
            if (entry.depth != 0 || !entry.contains(offset)) continue;
            if (!best || entry.start > best->start) best = entry;
        }
        return best;
    }

private:
    std::vector<GraphSourceMapEntry> entries_;
};

struct PapyrusSourceWriterOptions {
    std::string indent = "\t";
    std::string newLine = "\n";
    bool emitHeaderComment{true};
    std::optional<std::string> generatorNote;
};

class PapyrusSourceWriter {
public:
    static constexpr int kUnaryPrecedence = 6;
    static constexpr int kCastPrecedence = 7;
    static constexpr int kPostfixPrecedence = 8;

    explicit PapyrusSourceWriter(PapyrusSourceWriterOptions options = {});

    std::string write(const ir::IrScript& script, GraphSourceMap& map);

private:
    static int precedence(std::string_view op);
    static std::string typeText(std::string_view typeName, bool isArray);
    static std::string declarationText(const ir::IrVariable& variable);
    static std::string propertyText(const ir::IrProperty& property);
    static std::string parameterText(const ir::IrParameter& parameter);

    void line(std::string text);
    void blank();

    void writeHeaderComment();
    void writeHeaderLine(const ir::IrScript& script);
    void writeStruct(const ir::IrStruct& declared);
    void writeCallable(const ir::IrCallable& callable);
    void writeStatement(const ir::IrStatement& statement);
    void writeIf(const ir::IrIf& branch);
    std::string expression(const ir::IrExpression& expression, int parentPrecedence);

    PapyrusSourceWriterOptions options_;
    std::string text_;
    GraphSourceMap map_;
    int depth_{0};
    int line_{1};
};

}
