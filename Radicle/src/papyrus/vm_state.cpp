#include "fo4/papyrus/vm_state.hpp"

#include "sha256.hpp"

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace fo4::papyrus {
namespace {

constexpr const char* kFormat = "radicle.papyrus.instance-state";
constexpr std::int32_t kVersion = 1;

using Refusal = InstanceStateRefusal;

InstanceStateResult refuse(Refusal kind, std::string reason) { return InstanceStateResult{kind, std::move(reason)}; }

void putString(std::string& out, std::string_view text) {
    out.push_back('"');
    for (const char raw : text) {
        const unsigned char ch = static_cast<unsigned char>(raw);
        if (ch == '"' || ch == '\\') {
            out.push_back('\\');
            out.push_back(raw);
        } else if (ch < 0x20 || ch >= 0x7F) {
            char buffer[8];
            std::snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned>(ch));
            out += buffer;
        } else {
            out.push_back(raw);
        }
    }
    out.push_back('"');
}

struct Json {
    enum class Kind : std::uint8_t { Bool, Number, String, Array, Object };

    Kind kind{Kind::Bool};
    bool boolean{false};
    std::string text;
    std::vector<Json> items;
    std::vector<std::pair<std::string, Json>> members;

    const Json* get(const char* name) const {
        for (const auto& [key, value] : members) {
            if (key == name) return &value;
        }
        return nullptr;
    }
};

class Parser {
public:
    explicit Parser(std::string_view text) : text_(text) {}

    bool parse(Json& out, std::string& error) {
        skip();
        if (!value(out, 0)) {
            error = error_;
            return false;
        }
        skip();
        if (pos_ != text_.size()) {
            error = "the instance state has trailing data";
            return false;
        }
        return true;
    }

private:
    bool fail(const char* message) {
        if (error_.empty()) error_ = std::string(message) + " at byte " + std::to_string(pos_);
        return false;
    }

    void skip() {
        while (pos_ < text_.size() &&
               (text_[pos_] == ' ' || text_[pos_] == '\n' || text_[pos_] == '\r' || text_[pos_] == '\t')) {
            ++pos_;
        }
    }

    bool literal(std::string_view word) {
        if (text_.compare(pos_, word.size(), word) != 0) return fail("the instance state has an invalid literal");
        pos_ += word.size();
        return true;
    }

    bool value(Json& out, int depth) {
        if (depth > 16) return fail("the instance state is nested too deeply");
        skip();
        if (pos_ >= text_.size()) return fail("the instance state ends early");
        const char ch = text_[pos_];
        if (ch == '{') return object(out, depth);
        if (ch == '[') return array(out, depth);
        if (ch == '"') {
            out.kind = Json::Kind::String;
            return string(out.text);
        }
        if (ch == 't' || ch == 'f') {
            out.kind = Json::Kind::Bool;
            out.boolean = ch == 't';
            return literal(out.boolean ? "true" : "false");
        }
        return number(out);
    }

    bool number(Json& out) {
        const std::size_t start = pos_;
        if (pos_ < text_.size() && text_[pos_] == '-') ++pos_;
        const std::size_t digits = pos_;
        while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') ++pos_;
        if (pos_ == digits || (text_[digits] == '0' && pos_ - digits > 1)) return fail("the instance state has an invalid number");
        out.kind = Json::Kind::Number;
        out.text = std::string(text_.substr(start, pos_ - start));
        return true;
    }

    bool string(std::string& out) {
        ++pos_;
        out.clear();
        while (pos_ < text_.size()) {
            const unsigned char ch = static_cast<unsigned char>(text_[pos_++]);
            if (ch == '"') return true;
            if (ch < 0x20 || ch >= 0x7F) return fail("the instance state has a raw control or non-ASCII byte in a string");
            if (ch != '\\') {
                out.push_back(static_cast<char>(ch));
                continue;
            }
            if (pos_ >= text_.size()) break;
            const char escape = text_[pos_++];
            if (escape == '"' || escape == '\\') {
                out.push_back(escape);
            } else if (escape == 'u') {
                if (pos_ + 4 > text_.size()) break;
                unsigned code = 0;
                for (int i = 0; i < 4; ++i) {
                    const char hex = text_[pos_++];
                    code <<= 4;
                    if (hex >= '0' && hex <= '9') {
                        code |= static_cast<unsigned>(hex - '0');
                    } else if (hex >= 'a' && hex <= 'f') {
                        code |= static_cast<unsigned>(hex - 'a' + 10);
                    } else if (hex >= 'A' && hex <= 'F') {
                        code |= static_cast<unsigned>(hex - 'A' + 10);
                    } else {
                        return fail("the instance state has an invalid unicode escape");
                    }
                }
                if (code > 0xFF) return fail("the instance state has a unicode escape above one byte");
                out.push_back(static_cast<char>(code));
            } else {
                return fail("the instance state has an invalid string escape");
            }
        }
        return fail("the instance state ends inside a string");
    }

    bool array(Json& out, int depth) {
        out.kind = Json::Kind::Array;
        ++pos_;
        skip();
        if (pos_ < text_.size() && text_[pos_] == ']') {
            ++pos_;
            return true;
        }
        for (;;) {
            Json item;
            if (!value(item, depth + 1)) return false;
            out.items.push_back(std::move(item));
            skip();
            if (pos_ >= text_.size()) return fail("the instance state ends inside an array");
            if (text_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (text_[pos_] == ']') {
                ++pos_;
                return true;
            }
            return fail("the instance state needs a comma or a closing bracket");
        }
    }

    bool object(Json& out, int depth) {
        out.kind = Json::Kind::Object;
        ++pos_;
        skip();
        if (pos_ < text_.size() && text_[pos_] == '}') {
            ++pos_;
            return true;
        }
        for (;;) {
            skip();
            if (pos_ >= text_.size() || text_[pos_] != '"') return fail("the instance state needs a member name");
            std::string name;
            if (!string(name)) return false;
            if (out.get(name.c_str()) != nullptr) return fail("the instance state repeats a member name");
            skip();
            if (pos_ >= text_.size() || text_[pos_] != ':') return fail("the instance state needs a colon");
            ++pos_;
            Json member;
            if (!value(member, depth + 1)) return false;
            out.members.emplace_back(std::move(name), std::move(member));
            skip();
            if (pos_ >= text_.size()) return fail("the instance state ends inside an object");
            if (text_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (text_[pos_] == '}') {
                ++pos_;
                return true;
            }
            return fail("the instance state needs a comma or a closing brace");
        }
    }

    std::string_view text_;
    std::size_t pos_{0};
    std::string error_;
};

bool toInt(const Json& node, std::int64_t low, std::int64_t high, std::int64_t& out) {
    if (node.kind != Json::Kind::Number) return false;
    errno = 0;
    char* end = nullptr;
    const long long parsed = std::strtoll(node.text.c_str(), &end, 10);
    if (errno != 0 || end == nullptr || *end != '\0' || parsed < low || parsed > high) return false;
    out = parsed;
    return true;
}

bool toHandle(const Json& node, std::uint64_t& out) {
    if (node.kind != Json::Kind::String || node.text.empty() || node.text.size() > 20) return false;
    std::uint64_t value = 0;
    for (const char ch : node.text) {
        if (ch < '0' || ch > '9') return false;
        const std::uint64_t digit = static_cast<std::uint64_t>(ch - '0');
        if (value > (std::numeric_limits<std::uint64_t>::max() - digit) / 10) return false;
        value = value * 10 + digit;
    }
    out = value;
    return true;
}

std::string fold(const std::string& text) { return lowerAscii(text); }

using Identity = std::tuple<int, std::string, std::string, std::string>;

Identity identityOf(const OwnerId& owner, const std::string& script) {
    return Identity{static_cast<int>(owner.kind), owner.baseFormKey, owner.discriminator, fold(script)};
}

std::string describe(const OwnerId& owner, const std::string& script) {
    return script + " on " + owner.key();
}

class Encoder {
public:
    std::map<InstanceId, std::size_t> index;
    std::set<std::uint64_t> engine;

    std::string value(const Value& input) {
        std::string out;
        if (isNoneLike(input)) return "{\"t\":\"n\"}";
        switch (input.type) {
            case ValueType::String:
                out = "{\"t\":\"s\",\"v\":";
                putString(out, input.str);
                return out + "}";
            case ValueType::Integer:
                return "{\"t\":\"i\",\"v\":" + std::to_string(input.integer) + "}";
            case ValueType::Float: {
                std::uint32_t bits = 0;
                std::memcpy(&bits, &input.real, sizeof(bits));
                return "{\"t\":\"f\",\"b\":" + std::to_string(bits) + "}";
            }
            case ValueType::Bool:
                return std::string("{\"t\":\"b\",\"v\":") + (input.boolean ? "true" : "false") + "}";
            case ValueType::Object:
                return object(*input.object);
            case ValueType::Array:
                return "{\"t\":\"a\",\"r\":" + std::to_string(pool(input.array.get(), nullptr)) + "}";
            case ValueType::Struct:
                return "{\"t\":\"st\",\"r\":" + std::to_string(pool(nullptr, input.structure.get())) + "}";
            case ValueType::None:
                break;
        }
        return "{\"t\":\"n\"}";
    }

    std::string poolEntries() {
        std::string out;
        for (std::size_t i = 0; i < items_.size(); ++i) {
            out += i == 0 ? "\n" : ",\n";
            const Item item = items_[i];
            if (item.array != nullptr) {
                out += "    {\"k\":\"a\",\"et\":" + std::to_string(static_cast<int>(item.array->elementType)) + ",\"etn\":";
                putString(out, item.array->elementTypeName);
                out += ",\"e\":[";
                for (std::size_t j = 0; j < item.array->elements.size(); ++j) {
                    if (j != 0) out += ",";
                    out += value(item.array->elements[j]);
                }
                out += "]}";
            } else {
                out += "    {\"k\":\"s\",\"t\":";
                putString(out, item.structure->typeName);
                out += ",\"m\":[";
                bool first = true;
                for (const auto& [name, member] : item.structure->members) {
                    if (!first) out += ",";
                    first = false;
                    out += "[";
                    putString(out, name);
                    out += "," + value(member) + "]";
                }
                out += "]}";
            }
        }
        return out;
    }

    bool hasPool() const { return !items_.empty(); }

private:
    struct Item {
        const Array* array{nullptr};
        const Struct* structure{nullptr};
    };

    std::size_t pool(const Array* array, const Struct* structure) {
        const void* key = array != nullptr ? static_cast<const void*>(array) : static_cast<const void*>(structure);
        const auto found = poolIndex_.find(key);
        if (found != poolIndex_.end()) return found->second;
        const std::size_t position = items_.size();
        poolIndex_.emplace(key, position);
        items_.push_back(Item{array, structure});
        return position;
    }

    std::string object(const ObjectRef& ref) {
        std::string out;
        if ((ref.handle & kEngineHandleBit) != 0) {
            engine.insert(ref.handle);
            out = "{\"t\":\"e\",\"h\":\"" + std::to_string(ref.handle) + "\",\"type\":";
        } else {
            const auto found = ref.handle == 0 ? index.end() : index.find(ref.handle);
            if (found != index.end()) {
                out = "{\"t\":\"o\",\"i\":" + std::to_string(found->second) + ",\"type\":";
            } else {
                out = "{\"t\":\"o\",\"type\":";
            }
        }
        putString(out, ref.type);
        out += ",\"state\":";
        putString(out, ref.state);
        return out + "}";
    }

    std::map<const void*, std::size_t> poolIndex_;
    std::vector<Item> items_;
};

struct PoolNode {
    std::shared_ptr<Array> array;
    std::shared_ptr<Struct> structure;
};

struct Decoder {
    const InstanceStateOptions* options{nullptr};
    std::vector<PoolNode> pool;
    std::vector<std::optional<InstanceId>> live;
    std::set<std::uint64_t> engineListed;
    bool counting{true};
    std::int32_t unbound{0};

    InstanceStateResult unresolved(const std::string& what, Value& out) {
        if (!options->skipUnknown) return refuse(Refusal::NotFound, what);
        if (counting) ++unbound;
        out = noneValue();
        return {};
    }

    InstanceStateResult value(const Json& node, Value& out) {
        if (node.kind != Json::Kind::Object) return refuse(Refusal::Malformed, "a value must be an object");
        const Json* tag = node.get("t");
        if (tag == nullptr || tag->kind != Json::Kind::String) return refuse(Refusal::Malformed, "a value needs a text tag");
        const std::string& t = tag->text;
        const std::size_t expected = t == "n" ? 1 : (t == "o" ? 0 : 2);
        if (t != "o" && t != "e" && node.members.size() != expected) {
            return refuse(Refusal::Malformed, "a '" + t + "' value has unexpected members");
        }
        if (t == "n") {
            out = noneValue();
            return {};
        }
        if (t == "s") {
            const Json* v = node.get("v");
            if (v == nullptr || v->kind != Json::Kind::String) return refuse(Refusal::Malformed, "a string value needs text 'v'");
            out = stringValue(v->text);
            return {};
        }
        std::int64_t number = 0;
        if (t == "i") {
            const Json* v = node.get("v");
            if (v == nullptr || !toInt(*v, std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::max(), number)) {
                return refuse(Refusal::Malformed, "an int value needs a 32-bit integer 'v'");
            }
            out = integerValue(static_cast<std::int32_t>(number));
            return {};
        }
        if (t == "f") {
            const Json* b = node.get("b");
            if (b == nullptr || !toInt(*b, 0, std::numeric_limits<std::uint32_t>::max(), number)) {
                return refuse(Refusal::Malformed, "a float value needs the 32-bit pattern 'b'");
            }
            const std::uint32_t bits = static_cast<std::uint32_t>(number);
            float real = 0.0F;
            std::memcpy(&real, &bits, sizeof(real));
            out = floatValue(real);
            return {};
        }
        if (t == "b") {
            const Json* v = node.get("v");
            if (v == nullptr || v->kind != Json::Kind::Bool) return refuse(Refusal::Malformed, "a bool value needs a boolean 'v'");
            out = boolValue(v->boolean);
            return {};
        }
        if (t == "a" || t == "st") {
            const Json* r = node.get("r");
            if (r == nullptr || !toInt(*r, 0, static_cast<std::int64_t>(pool.size()) - 1, number)) {
                return refuse(Refusal::Malformed, "a container value names a pool entry that does not exist");
            }
            const PoolNode& target = pool[static_cast<std::size_t>(number)];
            if (t == "a" && target.array == nullptr) return refuse(Refusal::Malformed, "an array value names a struct entry");
            if (t == "st" && target.structure == nullptr) return refuse(Refusal::Malformed, "a struct value names an array entry");
            out = t == "a" ? arrayValue(target.array) : structValue(target.structure);
            return {};
        }
        if (t == "o" || t == "e") return object(node, t == "e", out);
        return refuse(Refusal::Malformed, "a value has the unknown tag '" + t + "'");
    }

    InstanceStateResult object(const Json& node, bool engine, Value& out) {
        const Json* type = node.get("type");
        const Json* state = node.get("state");
        if (type == nullptr || state == nullptr || type->kind != Json::Kind::String || state->kind != Json::Kind::String) {
            return refuse(Refusal::Malformed, "an object value needs text 'type' and 'state'");
        }
        std::int64_t number = 0;
        if (engine) {
            std::uint64_t handle = 0;
            const Json* h = node.get("h");
            if (node.members.size() != 4 || h == nullptr || !toHandle(*h, handle) || (handle & kEngineHandleBit) == 0 ||
                engineListed.count(handle) == 0) {
                return refuse(Refusal::Malformed, "an engine object value needs a listed engine handle 'h'");
            }
            const auto mapped = options->engineRemap.find(handle);
            if (mapped == options->engineRemap.end() || (mapped->second & kEngineHandleBit) == 0) {
                return unresolved("the engine handle " + std::to_string(handle) + " has no mapping to this session", out);
            }
            out = fo4::papyrus::objectValue(mapped->second, type->text, state->text);
            return {};
        }
        const Json* i = node.get("i");
        if (node.members.size() != (i == nullptr ? 3U : 4U)) return refuse(Refusal::Malformed, "an object value has unexpected members");
        if (i == nullptr) {
            out = fo4::papyrus::objectValue(0, type->text, state->text);
            return {};
        }
        if (!toInt(*i, 0, static_cast<std::int64_t>(live.size()) - 1, number)) {
            return refuse(Refusal::Malformed, "an object value names an instance that is not in the blob");
        }
        const std::optional<InstanceId>& target = live[static_cast<std::size_t>(number)];
        if (!target.has_value()) {
            return unresolved("an object reference points at an instance that is not in this runtime", out);
        }
        out = fo4::papyrus::objectValue(*target, type->text, state->text);
        return {};
    }
};

InstanceStateResult needText(const Json& object, const char* name, std::string& out) {
    const Json* node = object.get(name);
    if (node == nullptr || node->kind != Json::Kind::String) return refuse(Refusal::Malformed, std::string("a text '") + name + "' is required");
    out = node->text;
    return {};
}

bool onlyMembers(const Json& object, std::initializer_list<const char*> names) {
    if (object.kind != Json::Kind::Object || object.members.size() != names.size()) return false;
    for (const char* name : names) {
        if (object.get(name) == nullptr) return false;
    }
    return true;
}

struct SavedInstance {
    OwnerId owner;
    std::string script;
    std::string state;
    const Json* variables{nullptr};
};

InstanceStateResult readPool(const Json& poolNode, Decoder& decoder) {
    decoder.pool.assign(poolNode.items.size(), PoolNode{});
    for (std::size_t i = 0; i < poolNode.items.size(); ++i) {
        const Json& item = poolNode.items[i];
        const Json* kind = item.kind == Json::Kind::Object ? item.get("k") : nullptr;
        if (kind == nullptr || kind->kind != Json::Kind::String) return refuse(Refusal::Malformed, "a pool entry needs a kind 'k'");
        if (kind->text == "a" && onlyMembers(item, {"k", "et", "etn", "e"})) {
            decoder.pool[i].array = std::make_shared<Array>();
        } else if (kind->text == "s" && onlyMembers(item, {"k", "t", "m"})) {
            decoder.pool[i].structure = std::make_shared<Struct>();
        } else {
            return refuse(Refusal::Malformed, "a pool entry has the wrong members for its kind");
        }
    }
    for (std::size_t i = 0; i < poolNode.items.size(); ++i) {
        const Json& item = poolNode.items[i];
        if (decoder.pool[i].array != nullptr) {
            Array& array = *decoder.pool[i].array;
            std::int64_t elementType = 0;
            const Json* et = item.get("et");
            const Json* elements = item.get("e");
            if (!toInt(*et, 0, static_cast<std::int64_t>(ValueType::Struct), elementType) || elements->kind != Json::Kind::Array) {
                return refuse(Refusal::Malformed, "an array pool entry has a bad element type or elements");
            }
            array.elementType = static_cast<ValueType>(elementType);
            if (const InstanceStateResult named = needText(item, "etn", array.elementTypeName); !named.ok()) return named;
            for (const Json& element : elements->items) {
                Value value;
                if (const InstanceStateResult decoded = decoder.value(element, value); !decoded.ok()) return decoded;
                array.elements.push_back(std::move(value));
            }
        } else {
            Struct& structure = *decoder.pool[i].structure;
            const Json* members = item.get("m");
            if (members->kind != Json::Kind::Array) return refuse(Refusal::Malformed, "a struct pool entry needs a members array");
            if (const InstanceStateResult named = needText(item, "t", structure.typeName); !named.ok()) return named;
            for (const Json& pair : members->items) {
                if (pair.kind != Json::Kind::Array || pair.items.size() != 2 || pair.items[0].kind != Json::Kind::String) {
                    return refuse(Refusal::Malformed, "a struct member must be a [name, value] pair");
                }
                Value value;
                if (const InstanceStateResult decoded = decoder.value(pair.items[1], value); !decoded.ok()) return decoded;
                if (!structure.members.emplace(pair.items[0].text, std::move(value)).second) {
                    return refuse(Refusal::Malformed, "a struct repeats the member " + pair.items[0].text);
                }
            }
        }
    }
    return {};
}

}  // namespace

std::vector<InstanceSnapshot> Runtime::snapshotInstances() const {
    std::vector<InstanceSnapshot> out;
    for (const auto& [id, instance] : instances_) {
        if (instance.detached) continue;
        out.push_back(InstanceSnapshot{id, instance.owner, instance.script, instance.state, instance.variables});
    }
    return out;
}

std::optional<std::string> Runtime::declaredVariableType(const std::string& script, const std::string& name) const {
    std::string current = script;
    for (int hop = 0; hop <= kMaxInheritanceHops && !current.empty(); ++hop) {
        const pex::PexObject* object = findObject(current);
        if (object == nullptr) return std::nullopt;
        for (const pex::PexVariable& variable : object->variables) {
            if (sameName(variable.name, name)) return variable.type;
        }
        current = object->parentClassName;
    }
    return std::nullopt;
}

bool Runtime::declaresState(const std::string& script, const std::string& state) const {
    if (state.empty()) return true;
    std::string current = script;
    for (int hop = 0; hop <= kMaxInheritanceHops && !current.empty(); ++hop) {
        const pex::PexObject* object = findObject(current);
        if (object == nullptr) return false;
        for (const pex::PexState& candidate : object->states) {
            if (sameName(candidate.name, state)) return true;
        }
        current = object->parentClassName;
    }
    return false;
}

Status Runtime::coerceValue(const Value& value, const std::string& declaredType, Value& out) {
    const Interpreter coercer(*this);
    return coercer.coerce(value, declaredType, out);
}

void Runtime::commitInstances(std::vector<InstanceRestore>& plan) noexcept {
    for (InstanceRestore& entry : plan) {
        Instance* found = findInstance(entry.id);
        if (found == nullptr) continue;
        found->state = std::move(entry.state);
        found->variables = std::move(entry.variables);
    }
}

std::string InstanceStateReport::toReport() const {
    return "instances=" + std::to_string(instances) + " variables=" + std::to_string(variables) +
           " skippedInstances=" + std::to_string(skippedInstances) +
           " skippedVariables=" + std::to_string(skippedVariables) +
           " unboundReferences=" + std::to_string(unboundReferences) + "\n";
}

InstanceStateResult captureInstanceState(const Runtime& runtime, InstanceStateCapture& out) {
    out = InstanceStateCapture{};
    std::vector<InstanceSnapshot> snapshots = runtime.snapshotInstances();
    for (InstanceSnapshot& snapshot : snapshots) {
        for (auto it = snapshot.variables.begin(); it != snapshot.variables.end();) {
            it = runtime.declaredVariableType(snapshot.script, it->first).has_value() ? std::next(it) : snapshot.variables.erase(it);
        }
    }
    std::stable_sort(snapshots.begin(), snapshots.end(), [](const InstanceSnapshot& a, const InstanceSnapshot& b) {
        return identityOf(a.owner, a.script) < identityOf(b.owner, b.script);
    });
    for (std::size_t i = 1; i < snapshots.size(); ++i) {
        if (identityOf(snapshots[i - 1].owner, snapshots[i - 1].script) == identityOf(snapshots[i].owner, snapshots[i].script)) {
            return refuse(Refusal::Ambiguous, "two live instances share the identity " + describe(snapshots[i].owner, snapshots[i].script));
        }
    }
    Encoder encoder;
    for (std::size_t i = 0; i < snapshots.size(); ++i) {
        encoder.index.emplace(snapshots[i].id, i);
    }
    std::string instances;
    for (std::size_t i = 0; i < snapshots.size(); ++i) {
        const InstanceSnapshot& snapshot = snapshots[i];
        instances += i == 0 ? "\n" : ",\n";
        instances += "    {\"owner\":{\"kind\":" + std::to_string(static_cast<int>(snapshot.owner.kind)) + ",\"base\":";
        putString(instances, snapshot.owner.baseFormKey);
        instances += ",\"disc\":";
        putString(instances, snapshot.owner.discriminator);
        instances += "},\"script\":";
        putString(instances, snapshot.script);
        instances += ",\"state\":";
        putString(instances, snapshot.state);
        instances += ",\"variables\":[";
        bool first = true;
        for (const auto& [name, value] : snapshot.variables) {
            if (!first) instances += ",";
            first = false;
            instances += "[";
            putString(instances, name);
            instances += "," + encoder.value(value) + "]";
        }
        instances += "]}";
    }
    const std::string pool = encoder.poolEntries();
    std::string blob = "{\n  \"format\":\"" + std::string(kFormat) + "\",\n  \"version\":" + std::to_string(kVersion) + ",\n";
    blob += "  \"instances\":[" + instances + (snapshots.empty() ? "" : "\n  ") + "],\n";
    blob += "  \"pool\":[" + pool + (pool.empty() ? "" : "\n  ") + "],\n";
    blob += "  \"engine\":[";
    bool first = true;
    for (const std::uint64_t handle : encoder.engine) {
        if (!first) blob += ",";
        first = false;
        blob += "\"" + std::to_string(handle) + "\"";
        out.engineHandles.push_back(handle);
    }
    blob += "]\n}";
    out.digest = sha256Hex(blob);
    out.blob = std::move(blob);
    return {};
}

InstanceStateResult stageInstanceState(Runtime& runtime, std::string_view blob, const InstanceStateOptions& options,
                                       InstanceStatePlan& staged, InstanceStateReport& report) {
    report = InstanceStateReport{};
    staged.entries.clear();
    if (runtime.stepping()) return refuse(Refusal::Busy, "the runtime is stepping; restore after the step returns");
    Json root;
    std::string error;
    if (!Parser(blob).parse(root, error)) return refuse(Refusal::Malformed, error);
    const Json* format = root.kind == Json::Kind::Object ? root.get("format") : nullptr;
    if (format == nullptr || format->kind != Json::Kind::String || format->text != kFormat) {
        return refuse(Refusal::Malformed, "the blob is not a Radicle Papyrus instance state");
    }
    std::int64_t version = 0;
    const Json* versionNode = root.get("version");
    if (versionNode == nullptr || !toInt(*versionNode, std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::max(), version)) {
        return refuse(Refusal::Malformed, "the instance state needs an integer version");
    }
    if (version != kVersion) return refuse(Refusal::Malformed, "unsupported instance state version " + std::to_string(version));
    if (!onlyMembers(root, {"format", "version", "instances", "pool", "engine"})) {
        return refuse(Refusal::Malformed, "the instance state has missing or unknown members");
    }
    const Json& instancesNode = *root.get("instances");
    const Json& poolNode = *root.get("pool");
    const Json& engineNode = *root.get("engine");
    if (instancesNode.kind != Json::Kind::Array || poolNode.kind != Json::Kind::Array || engineNode.kind != Json::Kind::Array) {
        return refuse(Refusal::Malformed, "instances, pool and engine must be arrays");
    }

    Decoder decoder;
    decoder.options = &options;
    for (const Json& handle : engineNode.items) {
        std::uint64_t parsed = 0;
        if (!toHandle(handle, parsed) || (parsed & kEngineHandleBit) == 0) return refuse(Refusal::Malformed, "an engine handle entry is not a bit-63 handle");
        decoder.engineListed.insert(parsed);
    }

    std::vector<SavedInstance> saved;
    std::set<Identity> savedIdentities;
    for (const Json& node : instancesNode.items) {
        if (!onlyMembers(node, {"owner", "script", "state", "variables"})) return refuse(Refusal::Malformed, "an instance entry has missing or unknown members");
        const Json& owner = *node.get("owner");
        std::int64_t kind = 0;
        SavedInstance entry;
        if (!onlyMembers(owner, {"kind", "base", "disc"}) || !toInt(*owner.get("kind"), 0, static_cast<std::int64_t>(OwnerKind::RuntimeInstance), kind)) {
            return refuse(Refusal::Malformed, "an instance owner is malformed");
        }
        entry.owner.kind = static_cast<OwnerKind>(kind);
        if (const InstanceStateResult text = needText(owner, "base", entry.owner.baseFormKey); !text.ok()) return text;
        if (const InstanceStateResult text = needText(owner, "disc", entry.owner.discriminator); !text.ok()) return text;
        if (const InstanceStateResult text = needText(node, "script", entry.script); !text.ok()) return text;
        if (const InstanceStateResult text = needText(node, "state", entry.state); !text.ok()) return text;
        entry.variables = node.get("variables");
        if (entry.variables->kind != Json::Kind::Array) return refuse(Refusal::Malformed, "an instance needs a variables array");
        if (!savedIdentities.insert(identityOf(entry.owner, entry.script)).second) {
            return refuse(Refusal::Malformed, "the blob lists " + describe(entry.owner, entry.script) + " twice");
        }
        saved.push_back(std::move(entry));
    }

    std::map<Identity, InstanceId> liveByIdentity;
    for (const InstanceSnapshot& snapshot : runtime.snapshotInstances()) {
        if (!liveByIdentity.emplace(identityOf(snapshot.owner, snapshot.script), snapshot.id).second) {
            return refuse(Refusal::Ambiguous, "two live instances share the identity " + describe(snapshot.owner, snapshot.script));
        }
    }
    std::set<Identity> chosen;
    if (options.only.has_value()) {
        for (const InstanceIdentity& identity : *options.only) chosen.insert(identityOf(identity.owner, identity.script));
    }
    std::vector<bool> usable(saved.size(), false);
    std::vector<bool> picked(saved.size(), true);
    decoder.live.assign(saved.size(), std::nullopt);
    for (std::size_t i = 0; i < saved.size(); ++i) {
        const auto found = liveByIdentity.find(identityOf(saved[i].owner, saved[i].script));
        const std::string who = describe(saved[i].owner, saved[i].script);
        picked[i] = !options.only.has_value() || chosen.count(identityOf(saved[i].owner, saved[i].script)) != 0;
        if (found == liveByIdentity.end()) {
            if (!picked[i]) continue;
            if (!options.skipUnknown) return refuse(Refusal::NotFound, "no live instance of " + who);
            ++report.skippedInstances;
            continue;
        }
        if (!runtime.declaresState(saved[i].script, saved[i].state)) {
            if (!picked[i]) continue;
            if (!options.skipUnknown) return refuse(Refusal::NotFound, who + " does not declare the state '" + saved[i].state + "'");
            ++report.skippedInstances;
            continue;
        }
        decoder.live[i] = found->second;
        usable[i] = picked[i];
    }

    if (const InstanceStateResult pool = readPool(poolNode, decoder); !pool.ok()) return pool;

    std::vector<InstanceRestore>& plan = staged.entries;
    std::map<const Array*, std::string> typedArrays;
    for (std::size_t i = 0; i < saved.size(); ++i) {
        if (!picked[i]) continue;
        decoder.counting = usable[i];
        InstanceRestore restore;
        if (usable[i]) {
            restore.id = *decoder.live[i];
            restore.state = saved[i].state;
        }
        std::int32_t skippedHere = 0;
        for (const Json& pair : saved[i].variables->items) {
            if (pair.kind != Json::Kind::Array || pair.items.size() != 2 || pair.items[0].kind != Json::Kind::String) {
                return refuse(Refusal::Malformed, "a variable must be a [name, value] pair");
            }
            Value value;
            if (const InstanceStateResult decoded = decoder.value(pair.items[1], value); !decoded.ok()) return decoded;
            if (!usable[i]) continue;
            const std::string name = fold(pair.items[0].text);
            const std::string who = describe(saved[i].owner, saved[i].script);
            const std::optional<std::string> type = runtime.declaredVariableType(saved[i].script, name);
            if (!type.has_value()) {
                if (!options.skipUnknown) return refuse(Refusal::NotFound, who + " does not declare the variable " + pair.items[0].text);
                ++skippedHere;
                continue;
            }
            Value coerced;
            if (runtime.coerceValue(value, *type, coerced) != Status::Ok) {
                return refuse(Refusal::TypeMismatch, "the variable " + pair.items[0].text + " of " + who + " does not hold a " + *type);
            }
            if (isArrayTypeName(*type) && coerced.type == ValueType::Array && coerced.array != nullptr) {
                const std::string elementType = arrayElementTypeName(*type);
                const auto [typed, fresh] = typedArrays.emplace(coerced.array.get(), fold(elementType));
                if (!fresh && typed->second != fold(elementType)) {
                    return refuse(Refusal::TypeMismatch, "the array " + pair.items[0].text + " of " + who + " is shared with a variable of another element type");
                }
                if (fresh) {
                    for (Value& element : coerced.array->elements) {
                        Value converted;
                        if (runtime.coerceValue(element, elementType, converted) != Status::Ok) {
                            return refuse(Refusal::TypeMismatch, "an element of " + pair.items[0].text + " of " + who + " does not hold a " + elementType);
                        }
                        element = std::move(converted);
                    }
                    coerced.array->elementType = declaredValueType(elementType);
                    coerced.array->elementTypeName = elementType;
                }
            }
            if (!restore.variables.emplace(name, std::move(coerced)).second) {
                return refuse(Refusal::Malformed, who + " repeats the variable " + pair.items[0].text);
            }
        }
        if (!usable[i]) continue;
        report.skippedVariables += skippedHere;
        report.variables += static_cast<std::int32_t>(restore.variables.size());
        plan.push_back(std::move(restore));
    }
    report.instances = static_cast<std::int32_t>(plan.size());
    report.unboundReferences = decoder.unbound;
    return {};
}

void commitInstanceState(Runtime& runtime, InstanceStatePlan& staged) noexcept {
    runtime.commitInstances(staged.entries);
}

InstanceStateResult restoreInstanceState(Runtime& runtime, std::string_view blob, const InstanceStateOptions& options,
                                         InstanceStateReport& report) {
    InstanceStatePlan staged;
    if (const InstanceStateResult result = stageInstanceState(runtime, blob, options, staged, report); !result.ok()) return result;
    commitInstanceState(runtime, staged);
    return {};
}

}  // namespace fo4::papyrus
