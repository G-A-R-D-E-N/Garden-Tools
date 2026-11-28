#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <fo4/io/path_text.hpp>
#include <fo4/nif/physics_nif.hpp>
#include <fo4/previs/uvd.hpp>

#include "../helpers/TempRoot.hpp"
#include "../../src/plugin/RadiclePluginPrevisPrep.hpp"
#include "../../src/plugin/RadiclePluginPrevisTomeLedger.hpp"

namespace {

using fo4ide::plugin::GeneratedTomeLedger;
using fo4ide::plugin::TomeOrigin;
using fo4ide::plugin::guardTomeWrite;
using fo4ide::plugin::readBinaryFile;

void writeFile(const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    REQUIRE(stream.good());
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

std::vector<std::byte> textBytes(const std::string& text) {
    const auto* first = reinterpret_cast<const std::byte*>(text.data());
    return {first, first + text.size()};
}

std::vector<std::byte> makeTome(const std::filesystem::path& scratch, std::size_t bodies) {
    fo4ide::plugin::previs_prep::TomeRequest request;
    request.path = scratch;
    for (std::size_t index = 0; index < bodies; ++index) {
        const auto offset = 40.0f * static_cast<float>(index);
        request.bodies.push_back(fo4::nif::PhysicsMeshInput{
            {offset, 0.0f, 0.0f, offset + 10.0f, 0.0f, 0.0f, offset, 10.0f, 5.0f}, {0, 1, 2}});
    }
    REQUIRE(fo4ide::plugin::previs_prep::write_tome(request).has_value());
    auto bytes = readBinaryFile(scratch);
    REQUIRE(fo4::previs::validate_uvd(bytes).empty());
    return bytes;
}

}  // namespace

// Detects: replacing or removing a tome the ledger recorded must pass, and any other validated tome must be refused for both.
TEST_CASE("guardTomeWrite replaces or removes only a tome the ledger recorded") {
    fo4ide_test::TempRoot root{"fo4ide_tome_ledger_guard"};
    const auto tome = root.path() / "Vis" / "Patch.esp" / "00000801.uvd";
    const auto ours = makeTome(root.path() / "scratch-ours.uvd", 1);
    const auto other = makeTome(root.path() / "scratch-other.uvd", 2);
    REQUIRE(ours != other);

    GeneratedTomeLedger ledger{root.path() / "ledger.json"};
    writeFile(tome, ours);
    ledger.record(tome, ours, TomeOrigin::generated);
    CHECK_FALSE(guardTomeWrite(tome, other, ledger).has_value());
    CHECK(ledger.owns(tome, other));
    CHECK_FALSE(ledger.owns(tome, ours));

    writeFile(tome, other);
    CHECK_FALSE(guardTomeWrite(tome, {}, ledger).has_value());
    CHECK_FALSE(ledger.owns(tome, other));

    GeneratedTomeLedger fresh{root.path() / "fresh.json"};
    writeFile(tome, other);
    const auto replace = guardTomeWrite(tome, ours, fresh);
    REQUIRE(replace.has_value());
    CHECK(replace->find("refusing to replace") != std::string::npos);
    const auto remove = guardTomeWrite(tome, {}, fresh);
    REQUIRE(remove.has_value());
    CHECK(remove->find("refusing to remove") != std::string::npos);
    CHECK(fresh.pendingBytes().empty());
}

// Detects: an edited tome must stop matching its entry, an identical foreign tome must not be claimed, and unvalidated or missing files stay replaceable.
TEST_CASE("guardTomeWrite refuses an edited tome, does not claim an identical one and replaces garbage") {
    fo4ide_test::TempRoot root{"fo4ide_tome_ledger_edit"};
    const auto tome = root.path() / "Vis" / "Patch.esp" / "00000801.uvd";
    const auto ours = makeTome(root.path() / "scratch-ours.uvd", 1);
    const auto other = makeTome(root.path() / "scratch-other.uvd", 2);

    GeneratedTomeLedger ledger{root.path() / "ledger.json"};
    ledger.record(tome, ours, TomeOrigin::generated);
    writeFile(tome, other);
    CHECK(guardTomeWrite(tome, ours, ledger).has_value());

    GeneratedTomeLedger fresh{root.path() / "fresh.json"};
    writeFile(tome, ours);
    CHECK_FALSE(guardTomeWrite(tome, ours, fresh).has_value());
    CHECK_FALSE(fresh.owns(tome, ours));
    CHECK(fresh.pendingBytes().empty());

    writeFile(tome, std::vector<std::byte>(64, std::byte{0x5A}));
    REQUIRE_FALSE(fo4::previs::validate_uvd(readBinaryFile(tome)).empty());
    CHECK_FALSE(guardTomeWrite(tome, other, fresh).has_value());
    CHECK(fresh.owns(tome, other));

    const auto missing = root.path() / "Vis" / "Patch.esp" / "00000802.uvd";
    CHECK_FALSE(guardTomeWrite(missing, ours, fresh).has_value());
    CHECK(fresh.owns(missing, ours));
}

// Detects: a ledger must survive a round trip through its file, including a non-ASCII path, and an unreadable ledger must be reported, not read as empty.
TEST_CASE("GeneratedTomeLedger round-trips a non-ASCII path and reports an unreadable file") {
    fo4ide_test::TempRoot root{"fo4ide_tome_ledger_file"};
    const auto ledgerPath = root.path() / "ledger.json";
    const auto tome = root.path() / fo4::io::from_utf8("Vis/Caf\xC3\xA9.esp/00000801.uvd");
    const auto bytes = makeTome(root.path() / "scratch.uvd", 1);

    GeneratedTomeLedger writer{ledgerPath};
    CHECK_FALSE(writer.problem().has_value());
    writer.record(tome, bytes, TomeOrigin::generated);
    const auto pending = writer.pendingBytes();
    REQUIRE_FALSE(pending.empty());
    writeFile(ledgerPath, pending);

    GeneratedTomeLedger reader{ledgerPath};
    CHECK_FALSE(reader.problem().has_value());
    CHECK(reader.owns(tome, bytes));
    CHECK(reader.pendingBytes().empty());

    for (const auto* garbage : {"not json", "[]", "{\"tomes\": 3}", "{}"}) {
        writeFile(ledgerPath, textBytes(garbage));
        GeneratedTomeLedger broken{ledgerPath};
        CHECK(broken.problem().has_value());
        fo4ide::plugin::FileTransaction files;
        CHECK(fo4ide::plugin::stageGeneratedTomes(files, broken, tome, std::nullopt, bytes, TomeOrigin::generated).has_value());
    }
}

// Detects: a copied cluster tome must never be replaced or removed except by identical bytes, an edited one is refused like a foreign one, a generated one may become a copy, and a string entry still means generated.
TEST_CASE("guardTomeWrite refuses to replace or remove a copied tome") {
    fo4ide_test::TempRoot root{"fo4ide_tome_ledger_copied"};
    const auto tome = root.path() / "Vis" / "Patch.esp" / "00000801.uvd";
    const auto cluster = makeTome(root.path() / "scratch-cluster.uvd", 3);
    const auto other = makeTome(root.path() / "scratch-other.uvd", 1);
    const auto ledgerPath = root.path() / "ledger.json";

    GeneratedTomeLedger ledger{ledgerPath};
    CHECK_FALSE(guardTomeWrite(tome, cluster, ledger, TomeOrigin::copied).has_value());
    writeFile(tome, cluster);
    CHECK(ledger.originOf(tome, cluster) == TomeOrigin::copied);
    CHECK_FALSE(ledger.owns(tome, cluster));

    const auto replace = guardTomeWrite(tome, other, ledger, TomeOrigin::generated);
    REQUIRE(replace.has_value());
    CHECK(replace->find("copied cluster tome") != std::string::npos);
    const auto remove = guardTomeWrite(tome, {}, ledger);
    REQUIRE(remove.has_value());
    CHECK(remove->find("refusing to remove") != std::string::npos);
    CHECK(ledger.originOf(tome, cluster) == TomeOrigin::copied);

    CHECK_FALSE(guardTomeWrite(tome, cluster, ledger, TomeOrigin::copied).has_value());
    CHECK(guardTomeWrite(tome, other, ledger, TomeOrigin::copied).has_value());

    writeFile(ledgerPath, ledger.pendingBytes());
    GeneratedTomeLedger reloaded{ledgerPath};
    CHECK_FALSE(reloaded.problem().has_value());
    CHECK(reloaded.originOf(tome, cluster) == TomeOrigin::copied);
    CHECK(guardTomeWrite(tome, other, reloaded).has_value());

    writeFile(tome, other);
    const auto edited = guardTomeWrite(tome, cluster, reloaded, TomeOrigin::generated);
    REQUIRE(edited.has_value());
    CHECK(edited->find("did not generate") != std::string::npos);

    GeneratedTomeLedger generated{root.path() / "generated.json"};
    writeFile(tome, other);
    generated.record(tome, other, TomeOrigin::generated);
    const auto pending = generated.pendingBytes();
    const std::string pendingText(reinterpret_cast<const char*>(pending.data()), pending.size());
    CHECK(nlohmann::json::parse(pendingText)["tomes"].begin()->is_string());
    CHECK_FALSE(guardTomeWrite(tome, cluster, generated, TomeOrigin::copied).has_value());
    CHECK(generated.originOf(tome, cluster) == TomeOrigin::copied);
}
