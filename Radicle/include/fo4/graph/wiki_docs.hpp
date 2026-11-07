#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace fo4::graph {

struct WikiParameterDoc {
    std::optional<std::string> description;
    std::optional<std::string> documentedDefault;
};

struct WikiFunctionDoc {
    std::optional<std::string> summary;
    std::optional<std::string> syntaxLine;
    std::optional<std::string> returnValue;
    std::map<std::string, WikiParameterDoc, std::less<>> parameters;
};

struct WikiScriptDoc {
    std::optional<std::string> summary;
};

struct WikiDocStats {
    int pagesIndexed{0};
    int pagesParsed{0};
    int pagesFailed{0};

    static WikiDocStats empty() { return {}; }

    bool available() const { return pagesIndexed > 0; }
};

class WikiDocProvider {
public:
    virtual ~WikiDocProvider() = default;

    virtual const WikiFunctionDoc* function(std::string_view scriptName,
                                            std::string_view functionName) = 0;
    virtual const WikiScriptDoc* script(std::string_view scriptName) = 0;
    virtual WikiDocStats stats() const = 0;
};

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

class CkWikiDocProvider final : public WikiDocProvider {
public:
    explicit CkWikiDocProvider(std::optional<std::filesystem::path> wikiRoot);

    const WikiFunctionDoc* function(std::string_view scriptName,
                                    std::string_view functionName) override;
    const WikiScriptDoc* script(std::string_view scriptName) override;
    WikiDocStats stats() const override;

private:
    void buildIndexes();
    std::optional<WikiFunctionDoc> readFunction(std::string_view scriptName,
                                                std::string_view functionName);
    void recordFailure();

    std::optional<std::filesystem::path> root_;
    std::map<std::string, std::filesystem::path, std::less<>> byFunction_;
    std::map<std::string, std::filesystem::path, std::less<>> byScript_;
    std::map<std::string, std::optional<WikiFunctionDoc>, std::less<>> functions_;
    std::map<std::string, WikiScriptDoc, std::less<>> scripts_;
    int parsed_{0};
    int failed_{0};
};

}
