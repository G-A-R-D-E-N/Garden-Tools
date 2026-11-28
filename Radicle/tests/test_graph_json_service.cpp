#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <fo4/c_api_graph.h>
#include <fo4/graph/graph_json_service.hpp>
#include <fo4/graph/node_palette.hpp>

#include "graph_builder.hpp"

using namespace fo4::graph;
using namespace fo4::graph::test;
using ojson = nlohmann::ordered_json;

namespace {

std::string uniqueRoot(std::string_view tag) {
    return (std::filesystem::temp_directory_path() / ("fo4re-json-" + std::string(tag)) /
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))
        .string();
}

const std::string& imports() {
    static const std::string value = baseStubs().string();
    return value;
}

struct Out {
    radicle_status status{RADICLE_STATUS_OK};
    std::string text;
    std::string error;
};

Out take(radicle_status status, uint8_t* bytes, size_t size) {
    Out out;
    out.status = status;
    out.error = radicle_last_error() ? radicle_last_error() : "";
    if (bytes != nullptr) out.text.assign(reinterpret_cast<const char*>(bytes), size);
    radicle_free_bytes(bytes);
    return out;
}

std::string goodDocument() {
    Env env;
    GraphBuilder graph("Fixture", "ObjectReference");
    const auto entry = graph.node(env.palette, "event:ObjectReference.OnActivate");
    const auto notify = graph.node(env.palette, "global:Debug.Notification");
    graph.value(notify, "arg:asNotificationText", "string", "\"opened\"");
    graph.wire(entry, pinIds::kExec, notify, pinIds::kExec);
    return documentJson::serialize(graph.document);
}

std::string brokenDocument() {
    Env env;
    GraphBuilder graph("Fixture", "ObjectReference");
    const auto entry = graph.node(env.palette, "event:ObjectReference.OnLoad");
    const auto wait = graph.node(env.palette, "global:Utility.Wait");
    graph.wire(entry, pinIds::kExec, wait, pinIds::kExec);
    return documentJson::serialize(graph.document);
}

Out validate(const std::string& document) {
    uint8_t* bytes = nullptr;
    size_t size = 0;
    const auto status =
        radicle_graph_validate_json(document.c_str(), imports().c_str(), nullptr, 0, &bytes, &size);
    return take(status, bytes, size);
}

Out compile(const std::string& document, const char* outputPath, bool sourceOnly) {
    uint8_t* bytes = nullptr;
    size_t size = 0;
    const auto status = radicle_graph_compile_json(document.c_str(), outputPath, imports().c_str(),
                                                   sourceOnly ? 1 : 0, nullptr, 0, &bytes, &size);
    return take(status, bytes, size);
}

Out linearize(const std::string& document) {
    uint8_t* bytes = nullptr;
    size_t size = 0;
    const auto status =
        radicle_graph_linearize_json(document.c_str(), imports().c_str(), nullptr, 0, &bytes, &size);
    return take(status, bytes, size);
}

std::string scoreDocument() {
    Env env;
    GraphBuilder graph("Fixture", "ObjectReference");
    const auto entry = graph.node(env.palette, builtinDefs::kFunctionEntry,
                                  {{"name", "Score"}, {"returns", "int"}});
    const auto ret = graph.node(env.palette, builtinDefs::kReturn);
    const auto one = graph.node(env.palette, "literal.int");
    graph.value(one, pinIds::kValue, "int", "1");
    graph.wire(entry, pinIds::kExec, ret, pinIds::kExec);
    graph.wire(one, pinIds::kValue, ret, pinIds::kValue);
    return documentJson::serialize(graph.document);
}

Out normalize(const std::string& document) {
    uint8_t* bytes = nullptr;
    size_t size = 0;
    const auto status = radicle_graph_document_normalize(document.c_str(), &bytes, &size);
    return take(status, bytes, size);
}

std::vector<std::string> keysOf(const ojson& object) {
    std::vector<std::string> keys;
    for (const auto& item : object.items()) keys.push_back(item.key());
    return keys;
}

using Keys = std::vector<std::string>;

const std::string kCSharpOrderDocument = R"({
  "schema": 1,
  "id": "doc1",
  "kind": "papyrusScript",
  "header": {
    "scriptName": "MyMod:Door",
    "extends": "ObjectReference",
    "flags": [ "Conditional" ],
    "imports": [ "Debug" ],
    "docComment": "A door.",
    "autoState": "Waiting"
  },
  "variables": [
    { "name": "Target", "type": "ObjectReference", "isProperty": true, "initial": "None", "flags": [ "Auto" ] }
  ],
  "structs": [ { "name": "Pair", "members": [ { "name": "A", "type": "int", "isProperty": false, "flags": [] } ] } ],
  "customEvents": [ "Opened" ],
  "nodes": [
    {
      "id": "n1",
      "def": "var.set",
      "kind": 12,
      "x": 40.5,
      "y": 120,
      "comment": "note",
      "config": { "zeta": "1", "alpha": "2" },
      "pinValues": { "value": { "type": "int", "value": "3" } },
      "futureField": true
    }
  ],
  "wires": [ { "id": "w1", "from": { "node": "n1", "pin": "then" }, "to": { "node": "n2", "pin": "exec" } } ],
  "somethingNew": 42
})";

}

TEST(GraphJsonService, NormalizeWritesTheDocumentInTheCSharpFieldOrder) {
    const auto out = normalize(kCSharpOrderDocument);
    ASSERT_EQ(out.status, RADICLE_STATUS_OK);
    const auto json = ojson::parse(out.text);

    EXPECT_EQ(keysOf(json), (Keys{"schema", "id", "kind", "header", "variables", "structs",
                                  "customEvents", "nodes", "wires"}));
    EXPECT_EQ(keysOf(json["header"]), (Keys{"scriptName", "extends", "flags", "imports",
                                            "docComment", "autoState"}));
    EXPECT_EQ(keysOf(json["variables"][0]),
              (Keys{"name", "type", "isProperty", "initial", "flags"}));
    EXPECT_EQ(keysOf(json["structs"][0]), (Keys{"name", "members"}));
    EXPECT_EQ(keysOf(json["nodes"][0]), (Keys{"id", "def", "kind", "x", "y", "comment", "config",
                                              "pinValues"}));
    EXPECT_EQ(keysOf(json["wires"][0]), (Keys{"id", "from", "to"}));
    EXPECT_EQ(keysOf(json["wires"][0]["from"]), (Keys{"node", "pin"}));
    EXPECT_EQ(keysOf(json["nodes"][0]["pinValues"]["value"]), (Keys{"type", "value"}));
}

TEST(GraphJsonService, NormalizeKeepsTheOrderOfTheFreeFormConfigBag) {
    const auto json = ojson::parse(normalize(kCSharpOrderDocument).text);

    EXPECT_EQ(keysOf(json["nodes"][0]["config"]), (Keys{"zeta", "alpha"}));
}

TEST(GraphJsonService, NormalizeDropsUnknownFieldsAndReadsAnIntegerNodeKind) {
    const auto json = ojson::parse(normalize(kCSharpOrderDocument).text);

    EXPECT_FALSE(json.contains("somethingNew"));
    EXPECT_FALSE(json["nodes"][0].contains("futureField"));
    EXPECT_EQ(json["nodes"][0]["kind"], "variableGet");
    EXPECT_EQ(json["nodes"][0]["x"], 40.5);
}

TEST(GraphJsonService, NormalizeReadsAnIntegerDocumentKind) {
    const auto binding = ojson::parse(normalize(R"({ "schema": 1, "kind": 1, "header": { "scriptName": "B" } })").text);
    const auto script = ojson::parse(normalize(R"({ "schema": 1, "kind": 0, "header": { "scriptName": "B" } })").text);
    const auto outOfRange = ojson::parse(normalize(R"({ "schema": 1, "kind": 7, "header": { "scriptName": "B" } })").text);

    EXPECT_EQ(binding["kind"], "f4seBinding");
    EXPECT_EQ(script["kind"], "papyrusScript");
    EXPECT_EQ(outOfRange["kind"], "papyrusScript");
}

TEST(GraphJsonService, NormalizeOmitsNullOptionalsAndKeepsEmptyCollections) {
    const auto out = normalize(R"({ "schema": 1, "header": { "scriptName": "A" } })");
    ASSERT_EQ(out.status, RADICLE_STATUS_OK);
    const auto json = ojson::parse(out.text);

    EXPECT_EQ(keysOf(json["header"]), (Keys{"scriptName", "flags", "imports"}));
    EXPECT_TRUE(json["variables"].is_array() && json["variables"].empty());
    EXPECT_TRUE(json["nodes"].is_array() && json["nodes"].empty());
}

TEST(GraphJsonService, NormalizeIsIdempotentAndIndentedByTwo) {
    const auto once = normalize(kCSharpOrderDocument);
    const auto twice = normalize(once.text);

    EXPECT_EQ(once.text, twice.text);
    EXPECT_NE(once.text.find("\n  \"schema\": 1,"), std::string::npos);
}

TEST(GraphJsonService, NormalizeRoundTripsADocumentTheNativeWriterProduced) {
    const auto document = goodDocument();

    EXPECT_EQ(normalize(document).text, document);
}

TEST(GraphJsonService, NormalizeRefusesBadDocumentsWithTheDiagnosticMessage) {
    const auto empty = normalize("   ");
    EXPECT_EQ(empty.status, RADICLE_STATUS_PARSE_ERROR);
    EXPECT_EQ(empty.error, "The graph document is empty.");
    EXPECT_TRUE(empty.text.empty());

    const auto malformed = normalize("{ nope");
    EXPECT_EQ(malformed.status, RADICLE_STATUS_PARSE_ERROR);
    EXPECT_EQ(malformed.error.rfind("The graph document could not be read:", 0), 0u);

    const auto future = normalize(R"({ "schema": 2 })");
    EXPECT_EQ(future.status, RADICLE_STATUS_PARSE_ERROR);
    EXPECT_NE(future.error.find("schema 2"), std::string::npos);
}

TEST(GraphJsonService, NormalizeAcceptsAByteOrderMark) {
    const auto out = normalize("\xEF\xBB\xBF{ \"schema\": 1 }");

    EXPECT_EQ(out.status, RADICLE_STATUS_OK);
}

TEST(GraphJsonService, SaveWritesTheNormalizedDocumentAndCreatesTheFolder) {
    const auto root = std::filesystem::path(uniqueRoot("save"));
    const auto path = root / "nested" / "Door.fograph";

    ASSERT_EQ(radicle_graph_document_save(kCSharpOrderDocument.c_str(), path.string().c_str()),
              RADICLE_STATUS_OK);

    std::string written;
    {
        std::ifstream stream(path, std::ios::binary);
        written.assign((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    }
    EXPECT_EQ(written, normalize(kCSharpOrderDocument).text);
    std::filesystem::remove_all(root);
}

TEST(GraphJsonService, SaveOfABadDocumentWritesNothing) {
    const auto root = std::filesystem::path(uniqueRoot("savebad"));
    const auto path = root / "Bad.fograph";

    EXPECT_EQ(radicle_graph_document_save("{ nope", path.string().c_str()),
              RADICLE_STATUS_PARSE_ERROR);
    EXPECT_FALSE(std::filesystem::exists(path));
    EXPECT_EQ(radicle_graph_document_save(nullptr, path.string().c_str()),
              RADICLE_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(radicle_graph_document_save("{}", ""), RADICLE_STATUS_INVALID_ARGUMENT);
    std::filesystem::remove_all(root);
}

TEST(GraphJsonService, LoadReturnsTheFileTextUnchangedWithoutItsByteOrderMark) {
    const auto root = std::filesystem::path(uniqueRoot("load"));
    std::filesystem::create_directories(root);
    const auto path = root / "Door.fograph";
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << "\xEF\xBB\xBF" << kCSharpOrderDocument;
    }

    uint8_t* bytes = nullptr;
    size_t size = 0;
    const auto status = radicle_graph_document_load(path.string().c_str(), &bytes, &size);
    const auto out = take(status, bytes, size);
    std::filesystem::remove_all(root);

    EXPECT_EQ(out.status, RADICLE_STATUS_OK);
    EXPECT_EQ(out.text, kCSharpOrderDocument);
}

TEST(GraphJsonService, LoadRefusesAMissingFileAndAnUnreadableDocument) {
    uint8_t* bytes = nullptr;
    size_t size = 0;
    const auto missingStatus = radicle_graph_document_load("/no/such/Door.fograph", &bytes, &size);
    const auto missing = take(missingStatus, bytes, size);
    EXPECT_EQ(missing.status, RADICLE_STATUS_IO_ERROR);
    EXPECT_NE(missing.error.find("no file at"), std::string::npos);

    const auto root = std::filesystem::path(uniqueRoot("loadbad"));
    std::filesystem::create_directories(root);
    const auto path = root / "Bad.fograph";
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << "{ nope";
    }
    bytes = nullptr;
    const auto badStatus = radicle_graph_document_load(path.string().c_str(), &bytes, &size);
    const auto bad = take(badStatus, bytes, size);
    std::filesystem::remove_all(root);
    EXPECT_EQ(bad.status, RADICLE_STATUS_PARSE_ERROR);
}

TEST(GraphJsonService, ValidateReportsAGoodDocumentAsOkWithNoDiagnostics) {
    const auto out = validate(goodDocument());
    ASSERT_EQ(out.status, RADICLE_STATUS_OK);
    const auto json = ojson::parse(out.text);

    EXPECT_EQ(keysOf(json), (Keys{"diagnostics", "ok"}));
    EXPECT_EQ(json["ok"], true);
    EXPECT_TRUE(json["diagnostics"].empty());
}

TEST(GraphJsonService, ValidateNamesTheNodeAndPinOfABrokenDocumentInTheDiagnosticShape) {
    const auto out = validate(brokenDocument());
    ASSERT_EQ(out.status, RADICLE_STATUS_OK);
    const auto json = ojson::parse(out.text);

    EXPECT_EQ(json["ok"], false);
    const ojson* found = nullptr;
    for (const auto& entry : json["diagnostics"]) {
        if (entry["code"] == std::string(codes::kMissingRequiredInput)) found = &entry;
    }
    ASSERT_NE(found, nullptr);
    const auto& diagnostic = *found;
    EXPECT_EQ(keysOf(diagnostic),
              (Keys{"code", "severity", "message", "nodeId", "pinId", "relatedNodes", "line"}));
    EXPECT_EQ(diagnostic["code"], std::string(codes::kMissingRequiredInput));
    EXPECT_EQ(diagnostic["severity"], "error");
    EXPECT_TRUE(diagnostic["relatedNodes"].is_array());
    EXPECT_EQ(diagnostic["line"], 0);
}

TEST(GraphJsonService, ValidateTurnsAnUnreadableDocumentIntoADiagnosticWithOkStatus) {
    const auto malformed = ojson::parse(validate("{ nope").text);
    EXPECT_EQ(malformed["ok"], false);
    EXPECT_EQ(malformed["diagnostics"][0]["code"], std::string(codes::kMalformedDocument));

    const auto future = ojson::parse(validate(R"({ "schema": 9 })").text);
    EXPECT_EQ(future["diagnostics"][0]["code"], std::string(codes::kUnsupportedSchema));
}

TEST(GraphJsonService, ValidateAgreesWithThePathExportOnTheErrorCount) {
    const auto doc = brokenDocument();
    const auto json = ojson::parse(validate(doc).text);
    const auto root = std::filesystem::path(uniqueRoot("agree"));
    std::filesystem::create_directories(root);
    const auto path = root / "Fixture.fograph";
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << doc;
    }
    uint8_t* bytes = nullptr;
    size_t size = 0;
    const auto status =
        radicle_graph_validate(path.string().c_str(), imports().c_str(), nullptr, 0, &bytes, &size);
    const auto text = take(status, bytes, size).text;
    std::filesystem::remove_all(root);

    int errors = 0;
    for (const auto& diagnostic : json["diagnostics"]) errors += diagnostic["severity"] == "error";
    EXPECT_GT(errors, 0);
    EXPECT_NE(text.find("RESULT: " + std::to_string(errors) + " error"), std::string::npos);
}

TEST(GraphJsonService, CompileSourceOnlyReturnsTheScriptAndNeverWrites) {
    const auto output = uniqueRoot("srcout");
    const auto out = compile(goodDocument(), output.c_str(), true);
    ASSERT_EQ(out.status, RADICLE_STATUS_OK);
    const auto json = ojson::parse(out.text);

    EXPECT_EQ(keysOf(json), (Keys{"source", "diagnostics", "ok"}));
    EXPECT_EQ(json["ok"], true);
    EXPECT_NE(json["source"].get<std::string>().find("Scriptname Fixture extends ObjectReference"),
              std::string::npos);
    EXPECT_FALSE(std::filesystem::exists(output));
}

TEST(GraphJsonService, CompileToPexWritesBothFilesAndReportsThePexPath) {
    const auto output = uniqueRoot("pexout");
    const auto out = compile(goodDocument(), output.c_str(), false);
    ASSERT_EQ(out.status, RADICLE_STATUS_OK);
    const auto json = ojson::parse(out.text);

    EXPECT_EQ(keysOf(json), (Keys{"source", "diagnostics", "ok", "written"}));
    EXPECT_EQ(json["ok"], true);
    EXPECT_EQ(std::filesystem::path(json["written"].get<std::string>()).filename(), "Fixture.pex");
    EXPECT_TRUE(std::filesystem::exists(std::filesystem::path(output) / "Fixture.pex"));
    EXPECT_TRUE(std::filesystem::exists(std::filesystem::path(output) / "Fixture.psc"));
    EXPECT_GT(std::filesystem::file_size(std::filesystem::path(output) / "Fixture.pex"), 0u);
    std::filesystem::remove_all(output);
}

TEST(GraphJsonService, CompileToPexWithoutAnOutputFolderOmitsWritten) {
    const auto none = ojson::parse(compile(goodDocument(), nullptr, false).text);
    const auto empty = ojson::parse(compile(goodDocument(), "", false).text);

    EXPECT_EQ(none["ok"], true);
    EXPECT_FALSE(none.contains("written"));
    EXPECT_FALSE(empty.contains("written"));
}

TEST(GraphJsonService, CompileOfABrokenDocumentHasNoSourceNoFilesAndNotOk) {
    const auto output = uniqueRoot("badout");
    for (const bool sourceOnly : {true, false}) {
        const auto json = ojson::parse(compile(brokenDocument(), output.c_str(), sourceOnly).text);
        EXPECT_EQ(json["ok"], false);
        EXPECT_FALSE(json.contains("source"));
        EXPECT_FALSE(json.contains("written"));
        EXPECT_FALSE(json["diagnostics"].empty());
    }
    EXPECT_FALSE(std::filesystem::exists(output));
}

TEST(GraphJsonService, CompileOfAnUnreadableDocumentIsADiagnosticWithOkStatus) {
    const auto out = compile("{ nope", nullptr, false);
    ASSERT_EQ(out.status, RADICLE_STATUS_OK);
    const auto json = ojson::parse(out.text);

    EXPECT_EQ(json["ok"], false);
    EXPECT_EQ(json["diagnostics"][0]["code"], std::string(codes::kMalformedDocument));
}

TEST(GraphJsonService, CompileRefusesAnOutputPathWithADotDotSegment) {
    const auto out = compile(goodDocument(), "out/../escape", false);

    EXPECT_EQ(out.status, RADICLE_STATUS_INVALID_ARGUMENT);
    EXPECT_TRUE(out.text.empty());
}

TEST(GraphJsonService, LinearizeReturnsTheLoweredScriptWithItsSourceNodeIds) {
    const auto out = linearize(goodDocument());
    ASSERT_EQ(out.status, RADICLE_STATUS_OK);
    const auto json = ojson::parse(out.text);

    EXPECT_EQ(keysOf(json), (Keys{"ir", "diagnostics", "ok"}));
    EXPECT_EQ(json["ok"], true);
    EXPECT_TRUE(json["diagnostics"].empty());
    const auto expected = ojson::parse(R"({
      "name": "Fixture",
      "extends": "ObjectReference",
      "flags": [],
      "imports": [],
      "structs": [],
      "variables": [],
      "properties": [],
      "callables": [
        {
          "name": "OnActivate",
          "entryNodeId": "n1",
          "isEvent": true,
          "isGlobal": false,
          "returnIsArray": false,
          "parameters": [ { "name": "akActionRef", "typeName": "ObjectReference", "isArray": false } ],
          "locals": [],
          "body": [
            {
              "kind": "expression",
              "nodeId": "n2",
              "expression": {
                "kind": "call",
                "nodeId": "n2",
                "pinId": "ret",
                "typeName": "",
                "isArray": false,
                "receiver": { "kind": "name", "nodeId": "n2", "typeName": "", "isArray": false, "name": "Debug" },
                "name": "Notification",
                "isGlobal": true,
                "arguments": [
                  {
                    "value": {
                      "kind": "literal",
                      "nodeId": "n2",
                      "pinId": "arg:asNotificationText",
                      "typeName": "",
                      "isArray": false,
                      "text": "\"opened\""
                    }
                  }
                ]
              }
            }
          ]
        }
      ],
      "customEvents": []
    })");
    EXPECT_EQ(json["ir"], expected);
    EXPECT_EQ(json["ir"].dump(2), expected.dump(2));
}

TEST(GraphJsonService, LinearizeCarriesAReturnValueFromItsPureSourceNode) {
    const auto json = ojson::parse(linearize(scoreDocument()).text);

    ASSERT_EQ(json["ok"], true);
    const auto& score = json["ir"]["callables"][0];
    EXPECT_EQ(score["name"], "Score");
    EXPECT_EQ(score["isEvent"], false);
    EXPECT_EQ(score["returnTypeName"], "int");
    const auto expectedBody = ojson::parse(R"([
      {
        "kind": "return",
        "nodeId": "n2",
        "value": { "kind": "literal", "nodeId": "n3", "pinId": "value", "typeName": "int", "isArray": false, "text": "1" }
      }
    ])");
    EXPECT_EQ(score["body"], expectedBody);
}

TEST(GraphJsonService, LinearizeIsDeterministicAndAgreesWithTheService) {
    const auto doc = goodDocument();
    const auto first = linearize(doc);
    const auto direct = GraphJsonService::linearize(doc, &imports(), {});

    EXPECT_EQ(linearize(doc).text, first.text);
    EXPECT_EQ(direct.text, first.text);
    const auto source = ojson::parse(compile(doc, nullptr, true).text)["source"].get<std::string>();
    EXPECT_NE(source.find("Debug.Notification(\"opened\")"), std::string::npos);
}

TEST(GraphJsonService, LinearizeOfABrokenDocumentHasNoIrAndNotOk) {
    const auto json = ojson::parse(linearize(brokenDocument()).text);

    EXPECT_EQ(keysOf(json), (Keys{"diagnostics", "ok"}));
    EXPECT_EQ(json["ok"], false);
    EXPECT_FALSE(json["diagnostics"].empty());
}

TEST(GraphJsonService, LinearizeOfAnUnreadableDocumentIsADiagnosticWithOkStatus) {
    const auto out = linearize("{ nope");
    ASSERT_EQ(out.status, RADICLE_STATUS_OK);
    const auto json = ojson::parse(out.text);

    EXPECT_EQ(json["ok"], false);
    EXPECT_FALSE(json.contains("ir"));
    EXPECT_EQ(json["diagnostics"][0]["code"], std::string(codes::kMalformedDocument));
}

TEST(GraphJsonService, RequiredArgumentsAreRefusedAndOutputsAreCleared) {
    uint8_t sentinel = 1;
    uint8_t* bytes = &sentinel;
    size_t size = 7;
    EXPECT_EQ(radicle_graph_validate_json(nullptr, nullptr, nullptr, 0, &bytes, &size),
              RADICLE_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(bytes, nullptr);
    EXPECT_EQ(size, 0u);

    EXPECT_EQ(radicle_graph_validate_json("{}", nullptr, nullptr, 0, nullptr, &size),
              RADICLE_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(radicle_graph_compile_json(nullptr, nullptr, nullptr, 0, nullptr, 0, &bytes, &size),
              RADICLE_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(radicle_graph_linearize_json(nullptr, nullptr, nullptr, 0, &bytes, &size),
              RADICLE_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(radicle_graph_describe_node_json(nullptr, nullptr, nullptr, 0, &bytes, &size),
              RADICLE_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(radicle_graph_load_script_json(nullptr, nullptr, nullptr, 0, &bytes, &size),
              RADICLE_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(radicle_graph_document_normalize(nullptr, &bytes, &size),
              RADICLE_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(radicle_graph_document_load(nullptr, &bytes, &size), RADICLE_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(radicle_graph_validate_json("{}", nullptr, nullptr, 1, &bytes, &size),
              RADICLE_STATUS_INVALID_ARGUMENT);
    const char* const nullBase[] = {nullptr};
    EXPECT_EQ(radicle_graph_core_palette_json(nullptr, nullBase, 1, &bytes, &size),
              RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST(GraphJsonService, CorePaletteListsBuiltinsWithPinsAndScriptsSortedIgnoringCase) {
    uint8_t* bytes = nullptr;
    size_t size = 0;
    const auto status = radicle_graph_core_palette_json(imports().c_str(), nullptr, 0, &bytes, &size);
    const auto out = take(status, bytes, size);
    ASSERT_EQ(out.status, RADICLE_STATUS_OK);
    const auto json = ojson::parse(out.text);

    EXPECT_EQ(keysOf(json), (Keys{"version", "builtins", "scripts", "wiki"}));
    EXPECT_EQ(json["version"], GraphDocument::kCurrentSchema);
    EXPECT_EQ(keysOf(json["wiki"]), (Keys{"available", "pages"}));
    EXPECT_EQ(json["wiki"]["available"], false);
    EXPECT_EQ(json["wiki"]["pages"], 0);

    const ojson* branch = nullptr;
    for (const auto& builtin : json["builtins"]) {
        if (builtin["type"] == "branch") branch = &builtin;
    }
    ASSERT_NE(branch, nullptr);
    EXPECT_EQ(keysOf(*branch), (Keys{"type", "label", "category", "kind", "summary", "isPure", "isGlobal", "pins"}));
    EXPECT_EQ((*branch)["kind"], "Branch");
    EXPECT_EQ(keysOf((*branch)["pins"][0]), (Keys{"id", "name", "kind", "dir", "dataType", "optional"}));

    std::vector<std::string> scripts = json["scripts"].get<std::vector<std::string>>();
    EXPECT_NE(std::find(scripts.begin(), scripts.end(), "Debug"), scripts.end());
    auto upper = [](std::string text) {
        for (auto& c : text) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return text;
    };
    EXPECT_TRUE(std::is_sorted(scripts.begin(), scripts.end(), [&](const auto& a, const auto& b) {
        return upper(a) < upper(b);
    }));
}

TEST(GraphJsonService, PaletteSearchReturnsEntriesWithATrueTotalAndHonoursTheLimit) {
    uint8_t* bytes = nullptr;
    size_t size = 0;
    const auto status = radicle_graph_palette_search_json("any", "Get", "", 2, imports().c_str(),
                                                          nullptr, 0, &bytes, &size);
    const auto out = take(status, bytes, size);
    ASSERT_EQ(out.status, RADICLE_STATUS_OK);
    const auto json = ojson::parse(out.text);

    EXPECT_EQ(keysOf(json), (Keys{"entries", "total"}));
    EXPECT_EQ(json["entries"].size(), 2u);
    EXPECT_GT(json["total"].get<int>(), 2);
    EXPECT_EQ(keysOf(json["entries"][0]),
              (Keys{"id", "title", "category", "kind", "signature", "isPure"}));
}

TEST(GraphJsonService, PaletteSearchFiltersByKindAndScriptAndDefaultsTheLimitToFifty) {
    uint8_t* bytes = nullptr;
    size_t size = 0;
    auto status = radicle_graph_palette_search_json("Call", "AddItem", nullptr, 0, imports().c_str(),
                                                    nullptr, 0, &bytes, &size);
    const auto calls = ojson::parse(take(status, bytes, size).text);
    ASSERT_FALSE(calls["entries"].empty());
    for (const auto& entry : calls["entries"]) EXPECT_EQ(entry["kind"], "Call");

    bytes = nullptr;
    status = radicle_graph_palette_search_json("branch", "", "", 0, imports().c_str(), nullptr, 0,
                                               &bytes, &size);
    const auto branches = ojson::parse(take(status, bytes, size).text);
    for (const auto& entry : branches["entries"]) EXPECT_EQ(entry["kind"], "Branch");
    EXPECT_FALSE(branches["entries"].empty());

    bytes = nullptr;
    status = radicle_graph_palette_search_json(nullptr, nullptr, "ObjectReference", 0,
                                               imports().c_str(), nullptr, 0, &bytes, &size);
    const auto scoped = ojson::parse(take(status, bytes, size).text);
    EXPECT_LE(scoped["entries"].size(), 50u);
    for (const auto& entry : scoped["entries"]) EXPECT_EQ(entry["category"], "ObjectReference");
}

TEST(GraphJsonService, DescribeNodeListsPinsWithDirectionTypeAndDefault) {
    uint8_t* bytes = nullptr;
    size_t size = 0;
    const auto status = radicle_graph_describe_node_json("call:ObjectReference.AddItem",
                                                         imports().c_str(), nullptr, 0, &bytes, &size);
    const auto out = take(status, bytes, size);
    ASSERT_EQ(out.status, RADICLE_STATUS_OK);
    const auto json = ojson::parse(out.text);

    EXPECT_EQ(json["type"], "call:ObjectReference.AddItem");
    EXPECT_EQ(json["kind"], "Call");
    EXPECT_EQ(json["isPure"], false);
    const ojson* count = nullptr;
    for (const auto& pin : json["pins"]) {
        if (pin["id"] == "arg:aiCount") count = &pin;
    }
    ASSERT_NE(count, nullptr);
    EXPECT_EQ((*count)["dir"], "in");
    EXPECT_EQ((*count)["kind"], "data");
    EXPECT_EQ((*count)["optional"], true);
    EXPECT_EQ((*count)["defaultLiteral"], "1");
    EXPECT_FALSE((*count)["dataType"].get<std::string>().empty());
}

TEST(GraphJsonService, DescribeNodeOfAnUnknownTypeIsNotFoundWithTheReason) {
    uint8_t* bytes = nullptr;
    size_t size = 0;
    const auto status = radicle_graph_describe_node_json("call:No.Such", imports().c_str(), nullptr,
                                                         0, &bytes, &size);
    const auto out = take(status, bytes, size);

    EXPECT_EQ(out.status, RADICLE_STATUS_NOT_FOUND);
    EXPECT_EQ(out.error, "no node type 'call:No.Such' on this palette");
    EXPECT_TRUE(out.text.empty());
}

TEST(GraphJsonService, LoadScriptWrapsTheLiftedDocumentWithItsDiagnostics) {
    const auto root = std::filesystem::path(uniqueRoot("script"));
    std::filesystem::create_directories(root);
    const auto path = root / "Fixture.psc";
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << "Scriptname Fixture extends ObjectReference\n\nEvent OnLoad()\n    Disable(false)\nEndEvent\n";
    }
    uint8_t* bytes = nullptr;
    size_t size = 0;
    const auto status = radicle_graph_load_script_json(path.string().c_str(), imports().c_str(),
                                                       nullptr, 0, &bytes, &size);
    const auto out = take(status, bytes, size);
    std::filesystem::remove_all(root);
    ASSERT_EQ(out.status, RADICLE_STATUS_OK);
    const auto json = ojson::parse(out.text);

    EXPECT_EQ(keysOf(json), (Keys{"ok", "document", "diagnostics"}));
    EXPECT_EQ(json["ok"], true);
    EXPECT_EQ(keysOf(json["document"]), (Keys{"schema", "id", "kind", "header", "variables",
                                               "structs", "customEvents", "nodes", "wires"}));
    bool sawEvent = false;
    for (const auto& node : json["document"]["nodes"]) {
        if (node["def"] == "event:ObjectReference.OnLoad") sawEvent = true;
    }
    EXPECT_TRUE(sawEvent);
}

TEST(GraphJsonService, LoadScriptTextCanBeNormalizedAndValidatedWithoutChange) {
    const auto root = std::filesystem::path(uniqueRoot("scriptloop"));
    std::filesystem::create_directories(root);
    const auto path = root / "Fixture.psc";
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << "Scriptname Fixture extends ObjectReference\n\nEvent OnLoad()\n    Disable(false)\nEndEvent\n";
    }
    uint8_t* bytes = nullptr;
    size_t size = 0;
    const auto status = radicle_graph_load_script_json(path.string().c_str(), imports().c_str(),
                                                       nullptr, 0, &bytes, &size);
    const auto envelope = ojson::parse(take(status, bytes, size).text);
    std::filesystem::remove_all(root);

    const auto document = envelope["document"].dump(2);
    EXPECT_EQ(normalize(document).text, document);
    EXPECT_EQ(ojson::parse(validate(document).text)["ok"], true);
}

TEST(GraphJsonService, LoadScriptOfAMissingFileIsAParseError) {
    uint8_t* bytes = nullptr;
    size_t size = 0;
    const auto status = radicle_graph_load_script_json("/no/such/Fixture.psc", imports().c_str(),
                                                       nullptr, 0, &bytes, &size);
    const auto missing = take(status, bytes, size);

    EXPECT_EQ(missing.status, RADICLE_STATUS_PARSE_ERROR);
    EXPECT_FALSE(missing.error.empty());
}

TEST(GraphJsonService, TheServiceAndTheCAbiProduceTheSameText) {
    const auto doc = goodDocument();
    const std::vector<std::filesystem::path> roots{imports()};
    const auto direct = GraphJsonService::validate(doc, nullptr, roots);
    const char* const bases[] = {imports().c_str()};
    uint8_t* bytes = nullptr;
    size_t size = 0;
    const auto status = radicle_graph_validate_json(doc.c_str(), nullptr, bases, 1, &bytes, &size);

    EXPECT_EQ(take(status, bytes, size).text, direct.text);
}
