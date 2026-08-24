#include <catch2/catch_test_macros.hpp>

#include <string>
#include <utility>

#include <fo4ide/papyrus/Decompiler.hpp>
#include <fo4ide/papyrus/Pex.hpp>

using namespace fo4ide::papyrus;

TEST_CASE("Decompiler rejects unsafe PEX object names before source output") {
    for (const auto& name : {
             std::string("Namespace:..:escape"),
             std::string("Namespace:folder/escape"),
             std::string("Namespace:folder\\escape"),
             std::string("Namespace::escape")}) {
        PexFile pex;
        PexObject object;
        object.name = name;
        pex.objects.push_back(std::move(object));
        CHECK_THROWS(Decompiler::toSource(pex));
    }
}

TEST_CASE("Decompiler accepts normal namespaced PEX object names") {
    PexFile pex;
    PexObject object;
    object.name = "Namespace:SafeScript";
    pex.objects.push_back(std::move(object));

    const auto source = Decompiler::toSource(pex);
    CHECK(source.find("ScriptName Namespace:SafeScript") != std::string::npos);
}
