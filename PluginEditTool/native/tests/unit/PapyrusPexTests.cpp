#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4ide/papyrus/Pex.hpp>

namespace {

using fo4ide::papyrus::PexFile;
using fo4ide::papyrus::PexFunction;
using fo4ide::papyrus::PexInstruction;
using fo4ide::papyrus::PexObject;
using fo4ide::papyrus::PexProperty;
using fo4ide::papyrus::PexState;
using fo4ide::papyrus::PexTypedName;
using fo4ide::papyrus::PexValue;
using fo4ide::papyrus::PexValueType;

PexFile makeFullPex() {
    PexFile pex;
    pex.majorVersion = 3;
    pex.minorVersion = 9;
    pex.gameId = 2;
    pex.compilationTime = 1234567890;
    pex.sourceFileName = "MyScript.psc";
    pex.userName = "dev";
    pex.computerName = "buildbox";
    pex.hasDebugInfo = true;
    pex.modificationTime = 987654321;

    pex.userFlags.push_back({"conditional", 0});
    pex.userFlags.push_back({"hidden", 1});

    PexObject obj;
    obj.name = "MyScript";
    obj.parentClassName = "ScriptObject";
    obj.docString = "A test script";
    obj.isConst = false;
    obj.userFlags = 0;
    obj.autoStateName = "";

    fo4ide::papyrus::PexStruct point;
    point.name = "Point";
    fo4ide::papyrus::PexStructMember x;
    x.name = "X";
    x.type = "int";
    x.userFlags = 0;
    x.defaultValue = PexValue{PexValueType::Integer, "", 5, 0.0F, false};
    x.isConst = false;
    x.docString = "the x";
    point.members.push_back(std::move(x));
    obj.structs.push_back(std::move(point));

    fo4ide::papyrus::PexVariable counter;
    counter.name = "Counter";
    counter.type = "int";
    counter.userFlags = 1;
    counter.defaultValue = PexValue{PexValueType::Integer, "", 0, 0.0F, false};
    counter.isConst = false;
    obj.variables.push_back(std::move(counter));

    PexProperty health;
    health.name = "Health";
    health.type = "int";
    health.docString = "";
    health.userFlags = 0;
    health.flags = 0x01 | 0x02;  // readable + writable, not auto
    PexFunction read;
    read.name = "";
    read.returnType = "int";
    read.docString = "";
    read.userFlags = 0;
    read.isGlobal = false;
    read.isNative = false;
    PexInstruction load;
    load.opCode = 0x0D;  // assign
    load.args = {PexValue{PexValueType::Identifier, "Health", 0, 0.0F, false},
                 PexValue{PexValueType::Identifier, "::temp0", 0, 0.0F, false}};
    read.instructions.push_back(std::move(load));
    PexInstruction ret;
    ret.opCode = 0x1A;  // return
    ret.args = {PexValue{PexValueType::None, "", 0, 0.0F, false}};
    read.instructions.push_back(std::move(ret));
    health.readHandler = std::move(read);

    PexFunction write;
    write.name = "";
    write.returnType = "None";
    write.docString = "";
    write.userFlags = 0;
    write.isGlobal = false;
    write.isNative = false;
    write.params.push_back({"value", "int"});
    PexInstruction setter;
    setter.opCode = 0x0D;  // assign
    setter.args = {PexValue{PexValueType::Identifier, "Health", 0, 0.0F, false},
                   PexValue{PexValueType::Identifier, "value", 0, 0.0F, false}};
    write.instructions.push_back(std::move(setter));
    PexInstruction ret2;
    ret2.opCode = 0x1A;  // return
    ret2.args = {PexValue{PexValueType::None, "", 0, 0.0F, false}};
    write.instructions.push_back(std::move(ret2));
    health.writeHandler = std::move(write);

    obj.properties.push_back(std::move(health));

    PexState state;
    state.name = "Sitting";
    PexFunction fn;
    fn.name = "DoThing";
    fn.returnType = "float";
    fn.docString = "docs";
    fn.userFlags = 0;
    fn.isGlobal = true;
    fn.isNative = false;
    fn.params.push_back({"a", "int"});
    fn.params.push_back({"b", "string"});
    fn.locals.push_back({"temp", "float"});
    PexInstruction add;
    add.opCode = 0x01;  // iadd
    add.args = {PexValue{PexValueType::Identifier, "::temp", 0, 0.0F, false},
                PexValue{PexValueType::Identifier, "a", 0, 0.0F, false},
                PexValue{PexValueType::Integer, "", 2, 0.0F, false}};
    fn.instructions.push_back(std::move(add));
    PexInstruction jmp;
    jmp.opCode = 0x16;  // jmpt
    jmp.args = {PexValue{PexValueType::Identifier, "::temp", 0, 0.0F, false},
                PexValue{PexValueType::Identifier, "label0", 0, 0.0F, false}};
    fn.instructions.push_back(std::move(jmp));
    state.functions.push_back(std::move(fn));
    obj.states.push_back(std::move(state));

    pex.objects.push_back(std::move(obj));
    pex.rebuildStringTable();
    return pex;
}

void checkEquivalent(const PexFile& a, const PexFile& b) {
    CHECK(a.majorVersion == b.majorVersion);
    CHECK(a.minorVersion == b.minorVersion);
    CHECK(a.gameId == b.gameId);
    CHECK(a.compilationTime == b.compilationTime);
    CHECK(a.sourceFileName == b.sourceFileName);
    CHECK(a.userName == b.userName);
    CHECK(a.computerName == b.computerName);
    CHECK(a.hasDebugInfo == b.hasDebugInfo);
    CHECK(a.modificationTime == b.modificationTime);
    REQUIRE(a.stringTable == b.stringTable);
    REQUIRE(a.userFlags.size() == b.userFlags.size());
    for (std::size_t i = 0; i < a.userFlags.size(); ++i) {
        CHECK(a.userFlags[i].name == b.userFlags[i].name);
        CHECK(a.userFlags[i].index == b.userFlags[i].index);
    }

    REQUIRE(a.objects.size() == b.objects.size());
    for (std::size_t i = 0; i < a.objects.size(); ++i) {
        const auto& oa = a.objects[i];
        const auto& ob = b.objects[i];
        CHECK(oa.name == ob.name);
        CHECK(oa.parentClassName == ob.parentClassName);
        CHECK(oa.docString == ob.docString);
        CHECK(oa.isConst == ob.isConst);
        CHECK(oa.userFlags == ob.userFlags);
        CHECK(oa.autoStateName == ob.autoStateName);

        REQUIRE(oa.structs.size() == ob.structs.size());
        for (std::size_t s = 0; s < oa.structs.size(); ++s) {
            CHECK(oa.structs[s].name == ob.structs[s].name);
            REQUIRE(oa.structs[s].members.size() == ob.structs[s].members.size());
            for (std::size_t m = 0; m < oa.structs[s].members.size(); ++m) {
                CHECK(oa.structs[s].members[m].name == ob.structs[s].members[m].name);
                CHECK(oa.structs[s].members[m].type == ob.structs[s].members[m].type);
                CHECK(oa.structs[s].members[m].userFlags == ob.structs[s].members[m].userFlags);
                CHECK(oa.structs[s].members[m].isConst == ob.structs[s].members[m].isConst);
                CHECK(oa.structs[s].members[m].docString == ob.structs[s].members[m].docString);
            }
        }

        REQUIRE(oa.variables.size() == ob.variables.size());
        for (std::size_t v = 0; v < oa.variables.size(); ++v) {
            CHECK(oa.variables[v].name == ob.variables[v].name);
            CHECK(oa.variables[v].type == ob.variables[v].type);
            CHECK(oa.variables[v].userFlags == ob.variables[v].userFlags);
            CHECK(oa.variables[v].isConst == ob.variables[v].isConst);
        }

        REQUIRE(oa.properties.size() == ob.properties.size());
        for (std::size_t p = 0; p < oa.properties.size(); ++p) {
            CHECK(oa.properties[p].name == ob.properties[p].name);
            CHECK(oa.properties[p].type == ob.properties[p].type);
            CHECK(oa.properties[p].docString == ob.properties[p].docString);
            CHECK(oa.properties[p].userFlags == ob.properties[p].userFlags);
            CHECK(oa.properties[p].flags == ob.properties[p].flags);
            CHECK(oa.properties[p].autoVarName == ob.properties[p].autoVarName);
            CHECK(oa.properties[p].readHandler.has_value() == ob.properties[p].readHandler.has_value());
            if (oa.properties[p].readHandler) {
                CHECK(oa.properties[p].readHandler->returnType == ob.properties[p].readHandler->returnType);
                REQUIRE(oa.properties[p].readHandler->instructions.size() ==
                        ob.properties[p].readHandler->instructions.size());
            }
        }

        REQUIRE(oa.states.size() == ob.states.size());
        for (std::size_t st = 0; st < oa.states.size(); ++st) {
            CHECK(oa.states[st].name == ob.states[st].name);
            REQUIRE(oa.states[st].functions.size() == ob.states[st].functions.size());
            for (std::size_t f = 0; f < oa.states[st].functions.size(); ++f) {
                const auto& fa = oa.states[st].functions[f];
                const auto& fb = ob.states[st].functions[f];
                CHECK(fa.name == fb.name);
                CHECK(fa.returnType == fb.returnType);
                CHECK(fa.docString == fb.docString);
                CHECK(fa.userFlags == fb.userFlags);
                CHECK(fa.isGlobal == fb.isGlobal);
                CHECK(fa.isNative == fb.isNative);
                REQUIRE(fa.params.size() == fb.params.size());
                for (std::size_t pa = 0; pa < fa.params.size(); ++pa) {
                    CHECK(fa.params[pa].name == fb.params[pa].name);
                    CHECK(fa.params[pa].type == fb.params[pa].type);
                }
                REQUIRE(fa.locals.size() == fb.locals.size());
                REQUIRE(fa.instructions.size() == fb.instructions.size());
                for (std::size_t instruction = 0; instruction < fa.instructions.size(); ++instruction) {
                    CHECK(fa.instructions[instruction].opCode == fb.instructions[instruction].opCode);
                    REQUIRE(fa.instructions[instruction].args.size() == fb.instructions[instruction].args.size());
                    for (std::size_t aa = 0; aa < fa.instructions[instruction].args.size(); ++aa) {
                        CHECK(static_cast<std::uint8_t>(fa.instructions[instruction].args[aa].type) ==
                              static_cast<std::uint8_t>(fb.instructions[instruction].args[aa].type));
                        CHECK(fa.instructions[instruction].args[aa].str == fb.instructions[instruction].args[aa].str);
                        CHECK(fa.instructions[instruction].args[aa].int_ == fb.instructions[instruction].args[aa].int_);
                        CHECK(fa.instructions[instruction].args[aa].bool_ == fb.instructions[instruction].args[aa].bool_);
                    }
                }
            }
        }
    }
}

}  // namespace

TEST_CASE("Pex full round-trip preserves the object graph") {
    auto pex = makeFullPex();
    const auto bytes = pex.toBytes();
    const auto back = PexFile::fromBytes(bytes);
    checkEquivalent(pex, back);
}

TEST_CASE("Pex varargs call instruction round-trips") {
    PexFile pex;
    pex.majorVersion = 3;
    pex.minorVersion = 9;
    pex.gameId = 2;
    pex.sourceFileName = "S.psc";

    PexObject obj;
    obj.name = "S";
    PexState state;
    state.name = "";
    PexFunction fn;
    fn.name = "F";
    PexInstruction call;
    call.opCode = 0x18;  // callmethod, varargs
    call.args = {
        PexValue{PexValueType::Identifier, "::temp0", 0, 0.0F, false},
        PexValue{PexValueType::Identifier, "self", 0, 0.0F, false},
        PexValue{PexValueType::Identifier, "Target", 0, 0.0F, false},
        PexValue{PexValueType::String, "hello", 0, 0.0F, false},
        PexValue{PexValueType::Integer, "", 42, 0.0F, false},
    };
    fn.instructions.push_back(std::move(call));
    state.functions.push_back(std::move(fn));
    obj.states.push_back(std::move(state));
    pex.objects.push_back(std::move(obj));
    pex.rebuildStringTable();

    const auto back = PexFile::fromBytes(pex.toBytes());
    REQUIRE(back.objects.size() == 1);
    REQUIRE(back.objects[0].states.size() == 1);
    REQUIRE(back.objects[0].states[0].functions.size() == 1);
    const auto& instr = back.objects[0].states[0].functions[0].instructions[0];
    REQUIRE(instr.args.size() == 5);
    CHECK(instr.args[3].type == PexValueType::String);
    CHECK(instr.args[3].str == "hello");
    CHECK(instr.args[4].type == PexValueType::Integer);
    CHECK(instr.args[4].int_ == 42);
}

TEST_CASE("Pex rebuildStringTable de-duplicates and covers all referenced strings") {
    PexFile pex;
    pex.stringTable = {"junk", "junk"};
    pex.objects.push_back([] {
        PexObject obj;
        obj.name = "A";
        obj.parentClassName = "A";
        return obj;
    }());
    pex.rebuildStringTable();

    // The oracle's Add() keeps even empty strings (doc string, auto state name),
    // and name + parentClassName (both "A") dedupe, so the rebuilt table is
    // {name/parent, doc(""), autostate("")}.
    CHECK(pex.stringTable == std::vector<std::string>{"A", ""});
}

TEST_CASE("Pex rejects a big-endian (Skyrim LE) file") {
    // 0xDEC057FA little-endian bytes
    const std::vector<std::uint8_t> bytes = {0xFA, 0x57, 0xC0, 0xDE};
    CHECK_THROWS_AS(PexFile::fromBytes(bytes), std::runtime_error);
}

TEST_CASE("Pex rejects a bad magic") {
    const std::vector<std::uint8_t> bytes = {0x01, 0x02, 0x03, 0x04};
    CHECK_THROWS_AS(PexFile::fromBytes(bytes), std::runtime_error);
}

TEST_CASE("Pex rejects a non-Fallout game id") {
    std::vector<std::uint8_t> bytes = {0xDE, 0xC0, 0x57, 0xFA,  // magic LE
                                       0x03, 0x09,              // 3.9
                                       0x01, 0x00,              // game id 1 (Skyrim)
                                       0, 0, 0, 0, 0, 0, 0, 0};
    CHECK_THROWS_AS(PexFile::fromBytes(bytes), std::runtime_error);
}

TEST_CASE("Pex rejects an unknown opcode") {
    PexFile pex;
    pex.majorVersion = 3;
    pex.minorVersion = 9;
    pex.gameId = 2;
    PexObject obj;
    obj.name = "S";
    PexState state;
    state.name = "";
    PexFunction fn;
    fn.name = "F";
    PexInstruction bad;
    bad.opCode = 0xFF;
    fn.instructions.push_back(std::move(bad));
    state.functions.push_back(std::move(fn));
    obj.states.push_back(std::move(state));
    pex.objects.push_back(std::move(obj));
    pex.rebuildStringTable();

    CHECK_THROWS_AS(PexFile::fromBytes(pex.toBytes()), std::runtime_error);
}

TEST_CASE("Pex a string missing from the table fails to write") {
    PexFile pex;
    pex.majorVersion = 3;
    pex.minorVersion = 9;
    pex.gameId = 2;
    PexObject obj;
    obj.name = "S";  // never added to the string table
    pex.objects.push_back(std::move(obj));

    CHECK_THROWS_AS(pex.toBytes(), std::runtime_error);
}
