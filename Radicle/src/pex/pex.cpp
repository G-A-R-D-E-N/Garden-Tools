#include <fo4/pex/pex.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <unordered_map>

namespace fo4::pex {

namespace {

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}

class Reader {
public:
    explicit Reader(const std::vector<std::uint8_t>& bytes) : bytes_(bytes) {}

    std::uint8_t u8() {
        require(1);
        return bytes_[pos_++];
    }

    std::uint16_t u16() {
        require(2);
        const auto value = static_cast<std::uint16_t>(bytes_[pos_]) |
                           static_cast<std::uint16_t>(bytes_[pos_ + 1]) << 8;
        pos_ += 2;
        return static_cast<std::uint16_t>(value);
    }

    std::uint32_t u32() {
        require(4);
        const auto value = static_cast<std::uint32_t>(bytes_[pos_]) |
                           static_cast<std::uint32_t>(bytes_[pos_ + 1]) << 8 |
                           static_cast<std::uint32_t>(bytes_[pos_ + 2]) << 16 |
                           static_cast<std::uint32_t>(bytes_[pos_ + 3]) << 24;
        pos_ += 4;
        return value;
    }

    std::int32_t i32() { return static_cast<std::int32_t>(u32()); }

    std::int64_t i64() {
        const auto lo = static_cast<std::uint64_t>(u32());
        const auto hi = static_cast<std::uint64_t>(u32());
        return static_cast<std::int64_t>(lo | (hi << 32));
    }

    float f32() {
        const auto bits = u32();
        float value;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }

    std::string latin1() {
        const auto length = u16();
        require(length);
        std::string out;
        out.reserve(length);
        for (std::size_t i = 0; i < length; ++i) out.push_back(static_cast<char>(bytes_[pos_ + i]));
        pos_ += length;
        return out;
    }

    std::vector<std::uint8_t> rest() {
        require(0);
        return std::vector<std::uint8_t>(bytes_.begin() + static_cast<std::ptrdiff_t>(pos_),
                                         bytes_.end());
    }

    std::size_t position() const { return pos_; }
    std::size_t size() const { return bytes_.size(); }

private:
    void require(std::size_t count) const {
        if (pos_ + count > bytes_.size()) fail("Unexpected end of .pex data.");
    }

    const std::vector<std::uint8_t>& bytes_;
    std::size_t pos_{0};
};

class Writer {
public:
    void u8(std::uint8_t value) { bytes_.push_back(value); }

    void u16(std::uint16_t value) {
        bytes_.push_back(static_cast<std::uint8_t>(value));
        bytes_.push_back(static_cast<std::uint8_t>(value >> 8));
    }

    void u32(std::uint32_t value) {
        bytes_.push_back(static_cast<std::uint8_t>(value));
        bytes_.push_back(static_cast<std::uint8_t>(value >> 8));
        bytes_.push_back(static_cast<std::uint8_t>(value >> 16));
        bytes_.push_back(static_cast<std::uint8_t>(value >> 24));
    }

    void i64(std::int64_t value) {
        u32(static_cast<std::uint32_t>(static_cast<std::uint64_t>(value)));
        u32(static_cast<std::uint32_t>(static_cast<std::uint64_t>(value) >> 32));
    }

    void f32(float value) {
        std::uint32_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        u32(bits);
    }

    void latin1(const std::string& text) {
        if (text.size() > 0xFFFF) {
            fail("String of " + std::to_string(text.size()) +
                 " bytes; the format length-prefixes with a u16.");
        }
        u16(static_cast<std::uint16_t>(text.size()));
        bytes_.insert(bytes_.end(), text.begin(), text.end());
    }

    void count(std::size_t count, const char* what) {
        if (count > 0xFFFF) {
            fail(std::to_string(count) + std::string(" ") + what +
                 "; the format counts them with a u16.");
        }
        u16(static_cast<std::uint16_t>(count));
    }

    void append(std::vector<std::uint8_t> other) {
        bytes_.insert(bytes_.end(), other.begin(), other.end());
    }

    std::vector<std::uint8_t> take() && { return std::move(bytes_); }

private:
    std::vector<std::uint8_t> bytes_;
};

const std::string* tableString(const std::vector<std::string>& table, std::uint16_t index) {
    return index < table.size() ? &table[index] : nullptr;
}

const std::string& stringAt(const std::vector<std::string>& table, std::uint16_t index) {
    if (const auto* s = tableString(table, index)) return *s;
    fail("String table index " + std::to_string(index) + " is out of range; the table has " +
         std::to_string(table.size()) + " entries.");
}

std::string escaped(std::string_view text) {
    std::string out;
    for (const char ch : text) {
        if (ch == '\\' || ch == '"') out.push_back('\\');
        out.push_back(ch);
    }
    return out;
}

PexValue readValue(Reader& r, const std::vector<std::string>& table) {
    PexValue value;
    const auto tag = r.u8();
    if (tag > 5) fail("Invalid value type tag " + std::to_string(tag) + ".");
    value.type = static_cast<PexValueType>(tag);
    switch (value.type) {
        case PexValueType::None:
            break;
        case PexValueType::Identifier:
        case PexValueType::String:
            value.str = stringAt(table, r.u16());
            break;
        case PexValueType::Integer:
            value.int_ = r.i32();
            break;
        case PexValueType::Float:
            value.float_ = r.f32();
            break;
        case PexValueType::Bool:
            value.bool_ = r.u8() != 0;
            break;
    }
    return value;
}

PexFunction readFunction(Reader& r, const std::vector<std::string>& table) {
    PexFunction fn;
    fn.returnType = stringAt(table, r.u16());
    fn.docString = stringAt(table, r.u16());
    fn.userFlags = r.u32();
    const auto flags = r.u8();
    fn.isGlobal = (flags & 0x01) != 0;
    fn.isNative = (flags & 0x02) != 0;

    const auto paramCount = r.u16();
    for (std::size_t i = 0; i < paramCount; ++i) {
        PexTypedName p;
        p.name = stringAt(table, r.u16());
        p.type = stringAt(table, r.u16());
        fn.params.push_back(std::move(p));
    }
    const auto localCount = r.u16();
    for (std::size_t i = 0; i < localCount; ++i) {
        PexTypedName l;
        l.name = stringAt(table, r.u16());
        l.type = stringAt(table, r.u16());
        fn.locals.push_back(std::move(l));
    }

    const auto instructionCount = r.u16();
    for (std::size_t i = 0; i < instructionCount; ++i) {
        const auto op = r.u8();
        if (op >= std::size(PexFile::kOpCodes)) {
            fail("Unknown/unsupported opcode 0x" + [&] {
                std::string hex = "00";
                const char* digits = "0123456789ABCDEF";
                hex[0] = digits[op >> 4];
                hex[1] = digits[op & 0x0F];
                return hex;
            }() + " (FO4 supports 0x00-0x2E).");
        }
        const auto& meta = PexFile::kOpCodes[op];
        PexInstruction instruction;
        instruction.opCode = op;
        instruction.mnemonic = meta.name;
        instruction.fixedArgCount = meta.args;
        instruction.hasVarArgs = meta.varargs;
        for (int a = 0; a < meta.args; ++a) instruction.args.push_back(readValue(r, table));
        if (meta.varargs) {
            const auto countValue = readValue(r, table);
            if (countValue.type != PexValueType::Integer || countValue.int_ < 0) {
                fail("Variable-argument count for " + std::string(meta.name) +
                     " must be a non-negative integer.");
            }
            const auto n = countValue.int_;
            if (static_cast<std::size_t>(n) > r.size() - r.position()) {
                fail("Variable-argument count " + std::to_string(n) + " for " +
                     std::string(meta.name) + " exceeds the " +
                     std::to_string(r.size() - r.position()) + " bytes left in the file.");
            }
            for (int a = 0; a < n; ++a) instruction.args.push_back(readValue(r, table));
        }
        fn.instructions.push_back(std::move(instruction));
    }
    return fn;
}

void writeValue(Writer& w, const PexValue& value,
                const std::function<std::uint16_t(const std::string&)>& index) {
    w.u8(static_cast<std::uint8_t>(value.type));
    switch (value.type) {
        case PexValueType::None:
            break;
        case PexValueType::Identifier:
        case PexValueType::String:
            w.u16(index(value.str));
            break;
        case PexValueType::Integer:
            w.u32(static_cast<std::uint32_t>(value.int_));
            break;
        case PexValueType::Float:
            w.f32(value.float_);
            break;
        case PexValueType::Bool:
            w.u8(value.bool_ ? 1 : 0);
            break;
    }
}

void writeFunction(Writer& w, const PexFunction& fn,
                   const std::function<std::uint16_t(const std::string&)>& index) {
    w.u16(index(fn.returnType));
    w.u16(index(fn.docString));
    w.u32(fn.userFlags);
    w.u8(static_cast<std::uint8_t>((fn.isGlobal ? 0x01 : 0) | (fn.isNative ? 0x02 : 0)));

    w.count(fn.params.size(), "parameters");
    for (const auto& p : fn.params) {
        w.u16(index(p.name));
        w.u16(index(p.type));
    }
    w.count(fn.locals.size(), "locals");
    for (const auto& l : fn.locals) {
        w.u16(index(l.name));
        w.u16(index(l.type));
    }

    w.count(fn.instructions.size(), "instructions");
    for (const auto& instruction : fn.instructions) {
        if (instruction.opCode >= std::size(PexFile::kOpCodes)) {
            fail("Unknown opcode 0x" + std::to_string(instruction.opCode) +
                 " (FO4 supports 0x00-0x2E).");
        }
        const auto& meta = PexFile::kOpCodes[instruction.opCode];
        if (instruction.args.size() < static_cast<std::size_t>(meta.args)) {
            fail(std::string(meta.name) + " takes " + std::to_string(meta.args) +
                 " operands, got " + std::to_string(instruction.args.size()) + ".");
        }
        if (!meta.varargs && instruction.args.size() != static_cast<std::size_t>(meta.args)) {
            fail(std::string(meta.name) + " takes exactly " + std::to_string(meta.args) +
                 " operands, got " + std::to_string(instruction.args.size()) + ".");
        }

        w.u8(instruction.opCode);
        for (int a = 0; a < meta.args; ++a) writeValue(w, instruction.args[a], index);
        if (meta.varargs) {
            PexValue countValue;
            countValue.type = PexValueType::Integer;
            countValue.int_ =
                static_cast<std::int32_t>(instruction.args.size() - static_cast<std::size_t>(meta.args));
            writeValue(w, countValue, index);
            for (std::size_t a = static_cast<std::size_t>(meta.args); a < instruction.args.size(); ++a) {
                writeValue(w, instruction.args[a], index);
            }
        }
    }
}

void writeObjectBody(Writer& w, const PexObject& obj,
                     const std::function<std::uint16_t(const std::string&)>& index) {
    w.u16(index(obj.parentClassName));
    w.u16(index(obj.docString));
    w.u8(obj.isConst ? 1 : 0);
    w.u32(obj.userFlags);
    w.u16(index(obj.autoStateName));

    w.count(obj.structs.size(), "structs");
    for (const auto& st : obj.structs) {
        w.u16(index(st.name));
        w.count(st.members.size(), "struct members");
        for (const auto& m : st.members) {
            w.u16(index(m.name));
            w.u16(index(m.type));
            w.u32(m.userFlags);
            writeValue(w, m.defaultValue.value_or(PexValue{}), index);
            w.u8(m.isConst ? 1 : 0);
            w.u16(index(m.docString));
        }
    }

    w.count(obj.variables.size(), "variables");
    for (const auto& v : obj.variables) {
        w.u16(index(v.name));
        w.u16(index(v.type));
        w.u32(v.userFlags);
        writeValue(w, v.defaultValue.value_or(PexValue{}), index);
        w.u8(v.isConst ? 1 : 0);
    }

    w.count(obj.properties.size(), "properties");
    for (const auto& p : obj.properties) {
        w.u16(index(p.name));
        w.u16(index(p.type));
        w.u16(index(p.docString));
        w.u32(p.userFlags);
        w.u8(p.flags);
        if (p.isAutoVar()) {
            w.u16(index(p.autoVarName));
        } else {
            if (p.canRead()) {
                if (!p.readHandler) {
                    fail("Property \"" + p.name +
                         "\" is flagged readable but carries no handler to write.");
                }
                writeFunction(w, *p.readHandler, index);
            }
            if (p.canWrite()) {
                if (!p.writeHandler) {
                    fail("Property \"" + p.name +
                         "\" is flagged writable but carries no handler to write.");
                }
                writeFunction(w, *p.writeHandler, index);
            }
        }
    }

    w.count(obj.states.size(), "states");
    for (const auto& st : obj.states) {
        w.u16(index(st.name));
        w.count(st.functions.size(), "state functions");
        for (const auto& fn : st.functions) {
            w.u16(index(fn.name));
            writeFunction(w, fn, index);
        }
    }
}

PexFunction* findFunction(PexFile& pex, const std::string& objectName,
                          const std::string& stateName, const std::string& functionName,
                          std::uint8_t functionType) {
    const auto iequals = [](const std::string& a, const std::string& b) {
        if (a.size() != b.size()) return false;
        for (std::size_t i = 0; i < a.size(); ++i) {
            auto x = static_cast<unsigned char>(a[i]);
            auto y = static_cast<unsigned char>(b[i]);
            if (x >= 'A' && x <= 'Z') x = static_cast<unsigned char>(x - 'A' + 'a');
            if (y >= 'A' && y <= 'Z') y = static_cast<unsigned char>(y - 'A' + 'a');
            if (x != y) return false;
        }
        return true;
    };

    for (auto& obj : pex.objects) {
        if (!iequals(obj.name, objectName)) continue;
        if (functionType == 1 || functionType == 2) {
            for (auto& prop : obj.properties) {
                if (!iequals(prop.name, functionName)) continue;
                return functionType == 1 ? (prop.readHandler ? &*prop.readHandler : nullptr)
                                         : (prop.writeHandler ? &*prop.writeHandler : nullptr);
            }
            return nullptr;
        }
        for (auto& state : obj.states) {
            if (!iequals(state.name, stateName)) continue;
            for (auto& fn : state.functions) {
                if (iequals(fn.name, functionName)) return &fn;
            }
        }
        return nullptr;
    }
    return nullptr;
}

}  // namespace

std::string PexValue::toString() const {
    switch (type) {
        case PexValueType::None:
            return "None";
        case PexValueType::Identifier:
            return str;
        case PexValueType::String:
            return "\"" + escaped(str) + "\"";
        case PexValueType::Integer:
            return std::to_string(int_);
        case PexValueType::Float: {
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%.15g", static_cast<double>(float_));
            return buffer;
        }
        case PexValueType::Bool:
            return bool_ ? "true" : "false";
    }
    return "?";
}

PexFile PexFile::fromBytes(const std::vector<std::uint8_t>& bytes) {
    Reader r(bytes);
    const auto magic = r.u32();
    if (magic == 0xDEC057FAu) {
        fail("This is a big-endian (Skyrim LE) .pex; this decompiler targets Fallout 4 "
             "(little-endian).");
    }
    if (magic != 0xFA57C0DEu) {
        char buffer[16];
        std::snprintf(buffer, sizeof(buffer), "0x%08X", magic);
        fail(std::string("Not a .pex file (bad magic ") + buffer + ").");
    }

    PexFile pex;
    pex.majorVersion = r.u8();
    pex.minorVersion = r.u8();
    pex.gameId = r.u16();
    pex.compilationTime = r.i64();

    if (pex.gameId != 2) {
        fail("This .pex is for game id " + std::to_string(pex.gameId) + " at format " +
             std::to_string(pex.majorVersion) + "." + std::to_string(pex.minorVersion) +
             "; Fallout 4 is game id 2 at format 3.9. It was built by another game's Papyrus "
             "compiler.");
    }
    if (pex.majorVersion != 3) {
        fail("Unsupported .pex format major version " + std::to_string(pex.majorVersion) +
             "; only the Fallout 4 3.x container is supported.");
    }

    pex.sourceFileName = r.latin1();
    pex.userName = r.latin1();
    pex.computerName = r.latin1();

    const auto stringCount = r.u16();
    pex.stringTable.reserve(stringCount);
    for (std::size_t i = 0; i < stringCount; ++i) pex.stringTable.push_back(r.latin1());

    pex.hasDebugInfo = r.u8() != 0;
    if (pex.hasDebugInfo) {
        pex.modificationTime = r.i64();

        const auto fnCount = r.u16();
        for (std::size_t i = 0; i < fnCount; ++i) {
            PexDebugFunction df;
            df.objectName = stringAt(pex.stringTable, r.u16());
            df.stateName = stringAt(pex.stringTable, r.u16());
            df.functionName = stringAt(pex.stringTable, r.u16());
            df.functionType = r.u8();
            const auto lineCount = r.u16();
            df.lineNumbers.reserve(lineCount);
            for (std::size_t l = 0; l < lineCount; ++l) df.lineNumbers.push_back(r.u16());
            pex.debugFunctions.push_back(std::move(df));
        }

        const auto pgCount = r.u16();
        for (std::size_t i = 0; i < pgCount; ++i) {
            PexPropertyGroup pg;
            pg.objectName = stringAt(pex.stringTable, r.u16());
            pg.groupName = stringAt(pex.stringTable, r.u16());
            pg.docString = stringAt(pex.stringTable, r.u16());
            pg.userFlags = r.u32();
            const auto n = r.u16();
            for (std::size_t k = 0; k < n; ++k) pg.propertyNames.push_back(stringAt(pex.stringTable, r.u16()));
            pex.propertyGroups.push_back(std::move(pg));
        }

        const auto soCount = r.u16();
        for (std::size_t i = 0; i < soCount; ++i) {
            PexStructOrder so;
            so.objectName = stringAt(pex.stringTable, r.u16());
            so.orderName = stringAt(pex.stringTable, r.u16());
            const auto n = r.u16();
            for (std::size_t k = 0; k < n; ++k) so.memberNames.push_back(stringAt(pex.stringTable, r.u16()));
            pex.structOrders.push_back(std::move(so));
        }
    }

    const auto userFlagCount = r.u16();
    for (std::size_t i = 0; i < userFlagCount; ++i) {
        PexUserFlag uf;
        uf.name = stringAt(pex.stringTable, r.u16());
        uf.index = r.u8();
        pex.userFlags.push_back(std::move(uf));
    }

    const auto objectCount = r.u16();
    for (std::size_t i = 0; i < objectCount; ++i) {
        PexObject obj;
        obj.name = stringAt(pex.stringTable, r.u16());
        const auto declaredSize = r.u32();
        const auto bodyStart = r.position();
        obj.parentClassName = stringAt(pex.stringTable, r.u16());
        obj.docString = stringAt(pex.stringTable, r.u16());
        obj.isConst = r.u8() != 0;
        obj.userFlags = r.u32();
        obj.autoStateName = stringAt(pex.stringTable, r.u16());

        const auto structCount = r.u16();
        for (std::size_t s = 0; s < structCount; ++s) {
            PexStruct st;
            st.name = stringAt(pex.stringTable, r.u16());
            const auto memberCount = r.u16();
            for (std::size_t m = 0; m < memberCount; ++m) {
                PexStructMember member;
                member.name = stringAt(pex.stringTable, r.u16());
                member.type = stringAt(pex.stringTable, r.u16());
                member.userFlags = r.u32();
                member.defaultValue = readValue(r, pex.stringTable);
                member.isConst = r.u8() != 0;
                member.docString = stringAt(pex.stringTable, r.u16());
                st.members.push_back(std::move(member));
            }
            obj.structs.push_back(std::move(st));
        }

        const auto varCount = r.u16();
        for (std::size_t v = 0; v < varCount; ++v) {
            PexVariable variable;
            variable.name = stringAt(pex.stringTable, r.u16());
            variable.type = stringAt(pex.stringTable, r.u16());
            variable.userFlags = r.u32();
            variable.defaultValue = readValue(r, pex.stringTable);
            variable.isConst = r.u8() != 0;
            obj.variables.push_back(std::move(variable));
        }

        const auto propCount = r.u16();
        for (std::size_t p = 0; p < propCount; ++p) {
            PexProperty prop;
            prop.name = stringAt(pex.stringTable, r.u16());
            prop.type = stringAt(pex.stringTable, r.u16());
            prop.docString = stringAt(pex.stringTable, r.u16());
            prop.userFlags = r.u32();
            prop.flags = r.u8();
            if (prop.isAutoVar()) {
                prop.autoVarName = stringAt(pex.stringTable, r.u16());
            } else {
                if (prop.canRead()) prop.readHandler = readFunction(r, pex.stringTable);
                if (prop.canWrite()) prop.writeHandler = readFunction(r, pex.stringTable);
            }
            obj.properties.push_back(std::move(prop));
        }

        const auto stateCount = r.u16();
        for (std::size_t st = 0; st < stateCount; ++st) {
            PexState state;
            state.name = stringAt(pex.stringTable, r.u16());
            const auto fnCount = r.u16();
            for (std::size_t f = 0; f < fnCount; ++f) {
                const auto fnName = stringAt(pex.stringTable, r.u16());
                auto fn = readFunction(r, pex.stringTable);
                fn.name = fnName;
                state.functions.push_back(std::move(fn));
            }
            obj.states.push_back(std::move(state));
        }

        const auto body = r.position() - bodyStart;
        if (declaredSize == body) {
            obj.sizeIncludesItself = false;
        } else if (declaredSize != body + sizeof(std::uint32_t)) {
            fail("Object \"" + obj.name + "\" declares " + std::to_string(declaredSize) +
                 " bytes but its body is " + std::to_string(body) + " bytes.");
        }

        pex.objects.push_back(std::move(obj));
    }

    if (r.position() < r.size()) pex.trailingBytes = r.rest();

    // AttachLineNumbers
    if (pex.hasDebugInfo) {
        for (const auto& df : pex.debugFunctions) {
            auto* fn = findFunction(pex, df.objectName, df.stateName, df.functionName,
                                    df.functionType);
            if (!fn) continue;
            const auto count = std::min(fn->instructions.size(), df.lineNumbers.size());
            for (std::size_t i = 0; i < count; ++i) fn->instructions[i].line = df.lineNumbers[i];
        }
    }

    return pex;
}

std::vector<std::uint8_t> PexFile::toBytes() const {
    std::unordered_map<std::string, std::uint16_t> index;
    index.reserve(stringTable.size());
    for (std::size_t i = 0; i < stringTable.size(); ++i) {
        if (i > 0xFFFF) {
            fail("String table has " + std::to_string(stringTable.size()) +
                 " entries; the format indexes it with a u16.");
        }
        if (!index.contains(stringTable[i])) index[stringTable[i]] = static_cast<std::uint16_t>(i);
    }

    const auto id = [&](const std::string& s) -> std::uint16_t {
        const auto it = index.find(s);
        if (it == index.end()) {
            fail("\"" + s + "\" is not in the string table, so it has no index to write. " +
                 "Every string a .pex refers to must be present in PexFile.StringTable.");
        }
        return it->second;
    };

    Writer w;
    w.u32(0xFA57C0DEu);
    w.u8(majorVersion);
    w.u8(minorVersion);
    w.u16(gameId);
    w.i64(compilationTime);
    w.latin1(sourceFileName);
    w.latin1(userName);
    w.latin1(computerName);

    w.count(stringTable.size(), "string table");
    for (const auto& s : stringTable) w.latin1(s);

    w.u8(hasDebugInfo ? 1 : 0);
    if (hasDebugInfo) {
        w.i64(modificationTime);
        w.count(debugFunctions.size(), "debug functions");
        for (const auto& df : debugFunctions) {
            w.u16(id(df.objectName));
            w.u16(id(df.stateName));
            w.u16(id(df.functionName));
            w.u8(df.functionType);
            w.count(df.lineNumbers.size(), "debug line numbers");
            for (const auto line : df.lineNumbers) w.u16(line);
        }

        w.count(propertyGroups.size(), "property groups");
        for (const auto& pg : propertyGroups) {
            w.u16(id(pg.objectName));
            w.u16(id(pg.groupName));
            w.u16(id(pg.docString));
            w.u32(pg.userFlags);
            w.count(pg.propertyNames.size(), "property group members");
            for (const auto& n : pg.propertyNames) w.u16(id(n));
        }

        w.count(structOrders.size(), "struct orders");
        for (const auto& so : structOrders) {
            w.u16(id(so.objectName));
            w.u16(id(so.orderName));
            w.count(so.memberNames.size(), "struct order members");
            for (const auto& n : so.memberNames) w.u16(id(n));
        }
    }

    w.count(userFlags.size(), "user flags");
    for (const auto& uf : userFlags) {
        w.u16(id(uf.name));
        w.u8(uf.index);
    }

    w.count(objects.size(), "objects");
    for (const auto& obj : objects) {
        w.u16(id(obj.name));

        Writer body;
        writeObjectBody(body, obj, id);
        auto bodyBytes = std::move(body).take();
        const auto size = static_cast<std::uint32_t>(
            bodyBytes.size() + (obj.sizeIncludesItself ? sizeof(std::uint32_t) : 0));
        w.u32(size);
        w.append(std::move(bodyBytes));
    }

    for (const auto byte : trailingBytes) w.u8(byte);
    return std::move(w).take();
}

void PexFile::rebuildStringTable() {
    std::vector<std::string> table;
    std::unordered_map<std::string, std::size_t> seen;
    const auto add = [&](const std::string& s) {
        const auto [it, inserted] = seen.emplace(s, table.size());
        if (inserted) table.push_back(s);
    };
    const auto addValue = [&](const std::optional<PexValue>& value) {
        if (!value) return;
        if (value->type == PexValueType::Identifier || value->type == PexValueType::String) {
            add(value->str);
        }
    };
    const auto addFunction = [&](const PexFunction& fn) {
        add(fn.returnType);
        add(fn.docString);
        for (const auto& p : fn.params) {
            add(p.name);
            add(p.type);
        }
        for (const auto& l : fn.locals) {
            add(l.name);
            add(l.type);
        }
        for (const auto& instruction : fn.instructions) {
            for (const auto& a : instruction.args) addValue(a);
        }
    };

    for (const auto& uf : userFlags) add(uf.name);

    for (const auto& df : debugFunctions) {
        add(df.objectName);
        add(df.stateName);
        add(df.functionName);
    }
    for (const auto& pg : propertyGroups) {
        add(pg.objectName);
        add(pg.groupName);
        add(pg.docString);
        for (const auto& n : pg.propertyNames) add(n);
    }
    for (const auto& so : structOrders) {
        add(so.objectName);
        add(so.orderName);
        for (const auto& n : so.memberNames) add(n);
    }

    for (const auto& obj : objects) {
        add(obj.name);
        add(obj.parentClassName);
        add(obj.docString);
        add(obj.autoStateName);

        for (const auto& st : obj.structs) {
            add(st.name);
            for (const auto& m : st.members) {
                add(m.name);
                add(m.type);
                add(m.docString);
                addValue(m.defaultValue);
            }
        }
        for (const auto& v : obj.variables) {
            add(v.name);
            add(v.type);
            addValue(v.defaultValue);
        }
        for (const auto& p : obj.properties) {
            add(p.name);
            add(p.type);
            add(p.docString);
            if (p.isAutoVar()) add(p.autoVarName);
            if (p.readHandler) addFunction(*p.readHandler);
            if (p.writeHandler) addFunction(*p.writeHandler);
        }
        for (const auto& st : obj.states) {
            add(st.name);
            for (const auto& fn : st.functions) {
                add(fn.name);
                addFunction(fn);
            }
        }
    }

    stringTable = std::move(table);
}

}
