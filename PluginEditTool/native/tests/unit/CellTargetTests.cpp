#include <filesystem>
#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include <fo4ide/app/ServiceTargets.hpp>
#include <fo4ide/plugin/IPluginBackend.hpp>

#include "../helpers/NifFixture.hpp"
#include "../helpers/ServiceTargetFakes.hpp"

using namespace fo4ide::app::test;

namespace {

// Constraint: records the operation it was asked to run, so record delegation is observable without a Radicle backend.
class CapturingCellBackend final : public fo4ide::plugin::IPluginBackend {
public:
    std::string lastOperation;

    fo4ide::plugin::Capabilities capabilities() const noexcept override {
        return {.available = true};
    }

    fo4ide::Result<nlohmann::json> invoke(std::string_view operation,
                                          const nlohmann::json&) override {
        lastOperation = operation;
        return nlohmann::json("delegated");
    }
};

}  // namespace

TEST_CASE("CellTarget geometry batch reports not-found meshes and dedupes case-insensitively") {
    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures;  // no roots -> nothing resolves
    fo4ide::plugin::NullPluginBackend backend;
    fo4ide::app::CellTarget target(backend, service, textures);

    auto result = target.invoke(
        "GetGeometryBatch",
        nlohmann::json::array({R"(["a/b.nif","A/B.NIF","c/d.nif"," c/d.nif"])"}));
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());

    const auto parsed = nlohmann::json::parse(result->get<std::string>());
    CHECK(parsed.at("count") == 2);  // "A/B.NIF" deduped against "a/b.nif"
    const auto& geometry = parsed.at("geometry");
    REQUIRE(geometry.size() == 4);
    CHECK(geometry.at("a/b.nif").at("error") == "Not found (loose or in any indexed archive).");
    CHECK(geometry.at("A/B.NIF") == geometry.at("a/b.nif"));
    CHECK(geometry.at("c/d.nif").contains("error"));
    CHECK(geometry.at(" c/d.nif") == geometry.at("c/d.nif"));

    const auto progress = nlohmann::json::parse(
        target.invoke("GetGeometryBatchProgress", nlohmann::json::array())->get<std::string>());
    CHECK(progress.at("done") == 2);
    CHECK(progress.at("total") == 2);
    CHECK(progress.at("phase") == "converting");
    CHECK(progress.at("indexDone") == 0);
    CHECK(progress.at("indexTotal") == 0);
}

TEST_CASE("Cell geometry progress JSON reports the indexing and converting phases") {
    using fo4ide::assets::ArchiveIndexPhase;
    using fo4ide::assets::ArchiveIndexProgress;

    const auto indexing = nlohmann::json::parse(fo4ide::app::geometryBatchProgressJson(
        1, 4, ArchiveIndexProgress{ArchiveIndexPhase::indexing, 3, 79}));
    CHECK(indexing.at("done") == 1);
    CHECK(indexing.at("total") == 4);
    CHECK(indexing.at("phase") == "indexing");
    CHECK(indexing.at("indexDone") == 3);
    CHECK(indexing.at("indexTotal") == 79);

    const auto enumerating = nlohmann::json::parse(fo4ide::app::geometryBatchProgressJson(
        0, 4, ArchiveIndexProgress{ArchiveIndexPhase::indexing, 0, 0}));
    CHECK(enumerating.at("phase") == "indexing");
    CHECK(enumerating.at("indexTotal") == 0);

    for (const auto phase : {ArchiveIndexPhase::idle, ArchiveIndexPhase::ready}) {
        const auto converting = nlohmann::json::parse(fo4ide::app::geometryBatchProgressJson(
            2, 4, ArchiveIndexProgress{phase, 79, 79}));
        CHECK(converting.at("phase") == "converting");
        CHECK(converting.at("done") == 2);
        CHECK(converting.at("indexDone") == 79);
    }
}

TEST_CASE("CellTarget progress reports the archive index counts after a scan") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    touch(root / "broken.ba2");

    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures;
    textures.setSessionRoots({root});
    fo4ide::plugin::NullPluginBackend backend;
    fo4ide::app::CellTarget target(backend, service, textures);

    CHECK_FALSE(textures.resolveNif("meshes/none.nif").has_value());

    const auto progress = nlohmann::json::parse(
        target.invoke("GetGeometryBatchProgress", nlohmann::json::array())->get<std::string>());
    CHECK(progress.at("phase") == "converting");
    CHECK(progress.at("indexDone") == 1);
    CHECK(progress.at("indexTotal") == 1);
}


TEST_CASE("CellTarget geometry batch embeds in-process geometry for a resolved mesh") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto mesh = root / "meshes" / "clutter" / "thing.nif";
    fo4ide::test::writeBytes(mesh, fo4ide::test::triangleNif());

    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures;
    textures.setSessionRoots({root});
    fo4ide::plugin::NullPluginBackend backend;
    fo4ide::app::CellTarget target(backend, service, textures);

    auto result = target.invoke(
        "GetGeometryBatch",
        nlohmann::json::array({R"(["meshes/clutter/thing.nif"])"}));
    REQUIRE(result.has_value());

    const auto parsed = nlohmann::json::parse(result->get<std::string>());
    CHECK(parsed.at("count") == 1);
    const auto& shapes = parsed.at("geometry").at("meshes/clutter/thing.nif").at("shapes");
    REQUIRE(shapes.size() == 1);
    CHECK(shapes[0].at("verts").size() == 9);
}

TEST_CASE("CellTarget geometry batch isolates a truncated mesh from its neighbours") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto good = root / "meshes" / "a.nif";
    const auto broken = root / "meshes" / "b.nif";
    fo4ide::test::writeBytes(good, fo4ide::test::triangleNif());
    auto truncated = fo4ide::test::triangleNif();
    truncated.resize(truncated.size() - 8);
    fo4ide::test::writeBytes(broken, truncated);

    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures;
    textures.setSessionRoots({root});
    fo4ide::plugin::NullPluginBackend backend;
    fo4ide::app::CellTarget target(backend, service, textures);

    auto result = target.invoke(
        "GetGeometryBatch",
        nlohmann::json::array({R"(["meshes/a.nif","meshes/b.nif"])"}));
    REQUIRE(result.has_value());

    const auto parsed = nlohmann::json::parse(result->get<std::string>());
    CHECK(parsed.at("count") == 2);
    CHECK(parsed.at("geometry").at("meshes/a.nif").contains("shapes"));
    REQUIRE(parsed.at("geometry").at("meshes/b.nif").contains("error"));
}

TEST_CASE("CellTarget GetTexture returns empty when the model cannot be resolved") {
    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures;
    fo4ide::plugin::NullPluginBackend backend;
    fo4ide::app::CellTarget target(backend, service, textures);

    auto result = target.invoke(
        "GetTexture", nlohmann::json::array({"meshes/x.nif", "textures/x_d.dds"}));
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    CHECK(result->get<std::string>().empty());
}

TEST_CASE("CellTarget GetTexture stays empty when a resolved model has no usable texture") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto mesh = root / "meshes" / "thing.nif";
    touch(mesh);

    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures;
    textures.setSessionRoots({root});
    fo4ide::plugin::NullPluginBackend backend;
    fo4ide::app::CellTarget target(backend, service, textures);

    auto result = target.invoke(
        "GetTexture", nlohmann::json::array({"meshes/thing.nif", "textures/missing.dds"}));
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    CHECK(result->get<std::string>().empty());
}

TEST_CASE("CellTarget GetGeometryBatch rejects a valid non-array payload") {
    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures;
    fo4ide::plugin::NullPluginBackend backend;
    fo4ide::app::CellTarget target(backend, service, textures);

    for (const auto* payload : {"{}", "5", "\"meshes/a.nif\""}) {
        auto result = target.invoke("GetGeometryBatch", nlohmann::json::array({payload}));
        REQUIRE(result.has_value());
        const auto parsed = nlohmann::json::parse(result->get<std::string>());
        REQUIRE(parsed.contains("error"));
        CHECK(parsed.find("count") == parsed.end());
    }
}

TEST_CASE("CellTarget delegates record queries to the plugin backend facet") {
    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures;
    CapturingCellBackend backend;
    fo4ide::app::CellTarget target(backend, service, textures);

    // Invariant: GetPlugins on the cell facet forwards to appInterop.GetPlugins.
    auto result = target.invoke("GetPlugins", nlohmann::json::array());
    REQUIRE(result.has_value());
    CHECK(backend.lastOperation == "appInterop.GetPlugins");
}

TEST_CASE("CellTarget GetCellTimings reports the last geometry batch and texture calls since it") {
    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures;
    fo4ide::plugin::NullPluginBackend backend;
    fo4ide::app::CellTarget target(backend, service, textures);

    const auto initial = nlohmann::json::parse(
        target.invoke("GetCellTimings", nlohmann::json::array())->get<std::string>());
    CHECK(initial.at("batch").is_null());

    REQUIRE(target.invoke("GetGeometryBatch", nlohmann::json::array({R"(["a/b.nif","c/d.nif"])"})).has_value());
    static_cast<void>(textures.getTexturePngDataUrl("", "textures/none.dds", ""));

    auto timings = nlohmann::json::parse(
        target.invoke("GetCellTimings", nlohmann::json::array())->get<std::string>());
    CHECK(timings.at("batch").at("meshes") == 2);
    CHECK(timings.at("batch").at("errors") == 2);
    CHECK(timings.at("batch").at("resolver").at("archiveOpens") == 0);
    CHECK(timings.at("textures").at("textureCalls") == 1);

    REQUIRE(target.invoke("GetGeometryBatch", nlohmann::json::array({R"(["e/f.nif"])"})).has_value());
    timings = nlohmann::json::parse(
        target.invoke("GetCellTimings", nlohmann::json::array())->get<std::string>());
    CHECK(timings.at("batch").at("meshes") == 1);
    CHECK(timings.at("textures").at("textureCalls") == 0);
}
