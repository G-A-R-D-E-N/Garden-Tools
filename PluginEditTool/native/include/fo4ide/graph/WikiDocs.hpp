#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace fo4ide::graph {

// Port of WikiDocs.cs: the structured doc model behind the node palette's
// summaries, parameter descriptions and return values. CkWikiDocProvider
// indexes an offline Creation Kit Wiki HTML mirror (the same files the
// app-level wiki service reads) and parses pages into this model. The
// string-based app surface (fo4ide::app::wiki) is a different, coarser
// renderer; this provider is the palette's oracle.

// Port of WikiParameterDoc.
struct WikiParameterDoc {
    std::optional<std::string> description;
    std::optional<std::string> documentedDefault;
};

// Port of WikiFunctionDoc. Parameters is a case-insensitive map.
struct WikiFunctionDoc {
    std::optional<std::string> summary;
    std::optional<std::string> syntaxLine;
    std::optional<std::string> returnValue;
    std::map<std::string, WikiParameterDoc, std::less<>> parameters;
};

// Port of WikiScriptDoc.
struct WikiScriptDoc {
    std::optional<std::string> summary;
};

// Port of WikiDocStats.
struct WikiDocStats {
    int pagesIndexed{0};
    int pagesParsed{0};
    int pagesFailed{0};

    static WikiDocStats empty() { return {}; }

    bool available() const { return pagesIndexed > 0; }
};

// Port of IWikiDocProvider.
class WikiDocProvider {
public:
    virtual ~WikiDocProvider() = default;

    virtual const WikiFunctionDoc* function(std::string_view scriptName,
                                            std::string_view functionName) = 0;
    virtual const WikiScriptDoc* script(std::string_view scriptName) = 0;
    virtual WikiDocStats stats() const = 0;
};

// Port of NullWikiDocProvider: every lookup is null, stats empty.
class NullWikiDocProvider final : public WikiDocProvider {
public:
    static NullWikiDocProvider& instance() {
        static NullWikiDocProvider provider;
        return provider;
    }

    const WikiFunctionDoc* function(std::string_view, std::string_view) override {
        return nullptr;
    }
    const WikiScriptDoc* script(std::string_view) override { return nullptr; }
    WikiDocStats stats() const override { return WikiDocStats::empty(); }
};

// Port of CkWikiDocProvider: indexes a wiki mirror root and parses function
// pages on demand (cached). A null / non-existent root behaves like the null
// provider: every lookup returns null and stats report unavailable.
class CkWikiDocProvider final : public WikiDocProvider {
public:
    explicit CkWikiDocProvider(std::optional<std::filesystem::path> wikiRoot);

    const WikiFunctionDoc* function(std::string_view scriptName,
                                    std::string_view functionName) override;
    const WikiScriptDoc* script(std::string_view scriptName) override;
    WikiDocStats stats() const override;

private:
    // Indexes: "function|script" (case-insensitive) -> page path, and
    // script name -> page path for *_Script.html pages.
    void buildIndexes();
    std::optional<WikiFunctionDoc> readFunction(std::string_view scriptName,
                                                std::string_view functionName);
    void recordFailure();

    std::optional<std::filesystem::path> root_;
    std::map<std::string, std::filesystem::path, std::less<>> byFunction_;
    std::map<std::string, std::filesystem::path, std::less<>> byScript_;
    // Nullopt entries are cached misses, mirroring the C# GetOrAdd storing null.
    std::map<std::string, std::optional<WikiFunctionDoc>, std::less<>> functions_;
    std::map<std::string, WikiScriptDoc, std::less<>> scripts_;
    int parsed_{0};
    int failed_{0};
};

}  // namespace fo4ide::graph
