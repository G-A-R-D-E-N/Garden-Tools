#include "fo4/papyrus/save_state.hpp"

#include "sha256.hpp"

#include "fo4/papyrus/runtime.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::papyrus {
namespace {

std::string jsonEscape(std::string_view text) {
    std::string out;
    out.reserve(text.size() + 2);
    for (const char ch : text) {
        switch (ch) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20) {
                    char buffer[8];
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned>(ch));
                    out += buffer;
                } else {
                    out.push_back(ch);
                }
                break;
        }
    }
    return out;
}

std::string numberText(double value) {
    if (!std::isfinite(value)) return "0";
    char buffer[40];
    std::snprintf(buffer, sizeof(buffer), "%.17g", value);
    return buffer;
}

std::string quote(std::string_view text) { return "\"" + jsonEscape(text) + "\""; }

bool ownerOrder(const std::string& left, const std::string& right) { return left < right; }

struct JsonValue {
    enum class Kind { Null, Bool, Number, String, Array, Object };

    Kind kind{Kind::Null};
    bool boolean{false};
    double number{0.0};
    std::string text;
    std::vector<JsonValue> array;
    std::map<std::string, JsonValue> object;

    const JsonValue* member(const std::string& name) const {
        if (kind != Kind::Object) return nullptr;
        const auto found = object.find(name);
        return found == object.end() ? nullptr : &found->second;
    }
};

class JsonParser {
public:
    explicit JsonParser(std::string_view text) : text_(text) {}

    std::optional<JsonValue> parse(std::string& error) {
        skip();
        std::optional<JsonValue> value = parseValue(0);
        if (!value.has_value()) {
            error = error_.empty() ? "the save state is not valid JSON" : error_;
            return std::nullopt;
        }
        skip();
        if (pos_ != text_.size()) {
            error = "the save state has trailing data";
            return std::nullopt;
        }
        return value;
    }

private:
    std::optional<JsonValue> parseValue(int depth) {
        if (depth > 32) {
            error_ = "the save state is nested too deeply";
            return std::nullopt;
        }
        skip();
        if (pos_ >= text_.size()) return fail("unexpected end of the save state");
        const char ch = text_[pos_];
        if (ch == '{') return parseObject(depth);
        if (ch == '[') return parseArray(depth);
        if (ch == '"') {
            JsonValue value;
            value.kind = JsonValue::Kind::String;
            if (!parseString(value.text)) return std::nullopt;
            return value;
        }
        if (ch == 't' || ch == 'f') {
            JsonValue value;
            value.kind = JsonValue::Kind::Bool;
            if (text_.compare(pos_, 4, "true") == 0) {
                value.boolean = true;
                pos_ += 4;
            } else if (text_.compare(pos_, 5, "false") == 0) {
                value.boolean = false;
                pos_ += 5;
            } else {
                return fail("the save state has an invalid literal");
            }
            return value;
        }
        if (ch == 'n') {
            if (text_.compare(pos_, 4, "null") != 0) return fail("the save state has an invalid literal");
            pos_ += 4;
            return JsonValue{};
        }
        return parseNumber();
    }

    std::optional<JsonValue> parseObject(int depth) {
        JsonValue value;
        value.kind = JsonValue::Kind::Object;
        ++pos_;
        skip();
        if (pos_ < text_.size() && text_[pos_] == '}') {
            ++pos_;
            return value;
        }
        for (;;) {
            skip();
            std::string name;
            if (pos_ >= text_.size() || text_[pos_] != '"') return fail("the save state needs a member name");
            if (!parseString(name)) return std::nullopt;
            skip();
            if (pos_ >= text_.size() || text_[pos_] != ':') return fail("the save state needs a colon");
            ++pos_;
            std::optional<JsonValue> member = parseValue(depth + 1);
            if (!member.has_value()) return std::nullopt;
            value.object.emplace(std::move(name), std::move(*member));
            skip();
            if (pos_ >= text_.size()) return fail("the save state ends inside an object");
            if (text_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (text_[pos_] == '}') {
                ++pos_;
                return value;
            }
            return fail("the save state needs a comma or a closing brace");
        }
    }

    std::optional<JsonValue> parseArray(int depth) {
        JsonValue value;
        value.kind = JsonValue::Kind::Array;
        ++pos_;
        skip();
        if (pos_ < text_.size() && text_[pos_] == ']') {
            ++pos_;
            return value;
        }
        for (;;) {
            std::optional<JsonValue> element = parseValue(depth + 1);
            if (!element.has_value()) return std::nullopt;
            value.array.push_back(std::move(*element));
            skip();
            if (pos_ >= text_.size()) return fail("the save state ends inside an array");
            if (text_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (text_[pos_] == ']') {
                ++pos_;
                return value;
            }
            return fail("the save state needs a comma or a closing bracket");
        }
    }

    bool parseString(std::string& out) {
        if (pos_ >= text_.size() || text_[pos_] != '"') return fail("expected a string") == std::nullopt;
        ++pos_;
        out.clear();
        while (pos_ < text_.size()) {
            const char ch = text_[pos_++];
            if (ch == '"') return true;
            if (ch != '\\') {
                out.push_back(ch);
                continue;
            }
            if (pos_ >= text_.size()) break;
            const char escape = text_[pos_++];
            switch (escape) {
                case '"':
                case '\\':
                case '/':
                    out.push_back(escape);
                    break;
                case 'b':
                    out.push_back('\b');
                    break;
                case 'f':
                    out.push_back('\f');
                    break;
                case 'n':
                    out.push_back('\n');
                    break;
                case 'r':
                    out.push_back('\r');
                    break;
                case 't':
                    out.push_back('\t');
                    break;
                default:
                    return fail("the save state has an invalid string escape") == std::nullopt;
            }
        }
        error_ = "the save state ends inside a string";
        return false;
    }

    std::optional<JsonValue> parseNumber() {
        const std::size_t start = pos_;
        if (pos_ < text_.size() && (text_[pos_] == '-' || text_[pos_] == '+')) ++pos_;
        while (pos_ < text_.size()) {
            const char ch = text_[pos_];
            if ((ch >= '0' && ch <= '9') || ch == '.' || ch == 'e' || ch == 'E' || ch == '-' || ch == '+') {
                ++pos_;
            } else {
                break;
            }
        }
        if (pos_ == start) return fail("the save state has an invalid number");
        const std::string text(text_.substr(start, pos_ - start));
        JsonValue value;
        value.kind = JsonValue::Kind::Number;
        try {
            value.number = std::stod(text);
        } catch (...) {
            return fail("the save state has an invalid number");
        }
        return value;
    }

    void skip() {
        while (pos_ < text_.size()) {
            const char ch = text_[pos_];
            if (ch == ' ' || ch == '\n' || ch == '\r' || ch == '\t') {
                ++pos_;
            } else {
                break;
            }
        }
    }

    std::optional<JsonValue> fail(const char* message) {
        error_ = message;
        return std::nullopt;
    }

    std::string_view text_;
    std::size_t pos_{0};
    std::string error_;
};

const JsonValue* member(const JsonValue& value, const std::string& name) { return value.member(name); }

std::string textMember(const JsonValue& value, const std::string& name) {
    const JsonValue* found = member(value, name);
    return found != nullptr && found->kind == JsonValue::Kind::String ? found->text : std::string();
}

double numberMember(const JsonValue& value, const std::string& name) {
    const JsonValue* found = member(value, name);
    return found != nullptr && found->kind == JsonValue::Kind::Number ? found->number : 0.0;
}

bool hasMember(const JsonValue& value, const std::string& name) { return member(value, name) != nullptr; }

std::vector<std::string> stringsMember(const JsonValue& value, const std::string& name) {
    std::vector<std::string> out;
    const JsonValue* found = member(value, name);
    if (found == nullptr || found->kind != JsonValue::Kind::Array) return out;
    for (const JsonValue& entry : found->array) {
        out.push_back(entry.kind == JsonValue::Kind::String ? entry.text : std::string());
    }
    return out;
}

}  // namespace

std::string SaveState::serialize() const {
    std::string out;
    out += "{\n";
    out += "  \"version\": " + std::to_string(version) + ",\n";
    out += "  \"threads\": [";
    for (std::size_t i = 0; i < threads.size(); ++i) {
        const SavedThread& thread = threads[i];
        out += i == 0 ? "\n" : ",\n";
        out += "    {\n";
        out += "      \"owner\": " + quote(thread.ownerKey) + ",\n";
        out += "      \"script\": " + quote(thread.script) + ",\n";
        out += "      \"fingerprint\": " + quote(thread.fingerprint) + ",\n";
        out += "      \"function\": " + quote(thread.function) + ",\n";
        out += "      \"pc\": " + std::to_string(thread.pc) + ",\n";
        out += "      \"locals\": [";
        for (std::size_t j = 0; j < thread.locals.size(); ++j) {
            out += j == 0 ? "\n" : ",\n";
            out += "        " + quote(thread.locals[j]);
        }
        out += thread.locals.empty() ? "],\n" : "\n      ],\n";
        out += "      \"waitingToken\": " +
               (thread.waitingToken.has_value() ? std::to_string(*thread.waitingToken) : std::string("null")) +
               ",\n";
        out += "      \"waitingNative\": " +
               (thread.waitingNative.has_value() ? quote(*thread.waitingNative) : std::string("null")) + "\n";
        out += "    }";
    }
    out += threads.empty() ? "],\n" : "\n  ],\n";
    out += "  \"registrations\": [";
    for (std::size_t i = 0; i < registrations.size(); ++i) {
        const SavedRegistration& registration = registrations[i];
        out += i == 0 ? "\n" : ",\n";
        out += "    {\n";
        out += "      \"owner\": " + quote(registration.ownerKey) + ",\n";
        out += "      \"kind\": " + quote(registration.kind) + ",\n";
        out += "      \"event\": " + quote(registration.eventName) + ",\n";
        out += "      \"interval\": " + numberText(registration.interval) + ",\n";
        out += "      \"repeats\": " + std::string(registration.repeats ? "true" : "false") + ",\n";
        out += "      \"clock\": " + quote(registration.clock) + "\n";
        out += "    }";
    }
    out += registrations.empty() ? "],\n" : "\n  ],\n";
    out += "  \"timers\": [";
    for (std::size_t i = 0; i < timers.size(); ++i) {
        const SavedTimer& timer = timers[i];
        out += i == 0 ? "\n" : ",\n";
        out += "    {\n";
        out += "      \"owner\": " + quote(timer.ownerKey) + ",\n";
        out += "      \"event\": " + quote(timer.eventName) + ",\n";
        out += "      \"elapsed\": " + numberText(timer.elapsed) + ",\n";
        out += "      \"fires\": " + std::to_string(timer.fires) + "\n";
        out += "    }";
    }
    out += timers.empty() ? "],\n" : "\n  ],\n";
    out += "  \"objectives\": [";
    for (std::size_t i = 0; i < objectives.size(); ++i) {
        const SavedObjective& objective = objectives[i];
        out += i == 0 ? "\n" : ",\n";
        out += "    {\n";
        out += "      \"owner\": " + quote(objective.ownerKey) + ",\n";
        out += "      \"index\": " + std::to_string(objective.index) + ",\n";
        out += "      \"state\": " + std::to_string(objective.state) + "\n";
        out += "    }";
    }
    out += objectives.empty() ? "]\n" : "\n  ]\n";
    out += "}";
    return out;
}

std::string SaveState::digest() const { return sha256Hex(serialize()); }

std::optional<SaveState> SaveState::deserialize(std::string_view json, std::string& error) {
    JsonParser parser(json);
    std::optional<JsonValue> root = parser.parse(error);
    if (!root.has_value()) return std::nullopt;
    if (root->kind != JsonValue::Kind::Object) {
        error = "the save state must be an object";
        return std::nullopt;
    }
    const JsonValue* versionNode = member(*root, "version");
    if (versionNode == nullptr || versionNode->kind != JsonValue::Kind::Number) {
        error = "the save state needs a version";
        return std::nullopt;
    }
    const int version = static_cast<int>(versionNode->number);
    if (version != kVersion) {
        error = "unsupported Papyrus save version " + std::to_string(version);
        return std::nullopt;
    }
    SaveState state;
    state.version = version;

    const JsonValue* threadsNode = member(*root, "threads");
    const JsonValue* registrationsNode = member(*root, "registrations");
    const JsonValue* timersNode = member(*root, "timers");
    const JsonValue* objectivesNode = member(*root, "objectives");
    if (threadsNode == nullptr || registrationsNode == nullptr || timersNode == nullptr ||
        objectivesNode == nullptr || threadsNode->kind != JsonValue::Kind::Array ||
        registrationsNode->kind != JsonValue::Kind::Array || timersNode->kind != JsonValue::Kind::Array ||
        objectivesNode->kind != JsonValue::Kind::Array) {
        error = "the save state must carry threads, registrations, timers and objectives arrays";
        return std::nullopt;
    }
    for (const JsonValue& node : threadsNode->array) {
        SavedThread thread;
        thread.ownerKey = textMember(node, "owner");
        thread.script = textMember(node, "script");
        thread.fingerprint = textMember(node, "fingerprint");
        thread.function = textMember(node, "function");
        thread.pc = static_cast<std::int32_t>(numberMember(node, "pc"));
        thread.locals = stringsMember(node, "locals");
        if (hasMember(node, "waitingToken")) {
            const JsonValue* token = member(node, "waitingToken");
            if (token != nullptr && token->kind == JsonValue::Kind::Number) {
                thread.waitingToken = static_cast<std::int64_t>(token->number);
            }
        }
        if (hasMember(node, "waitingNative")) {
            const JsonValue* native = member(node, "waitingNative");
            if (native != nullptr && native->kind == JsonValue::Kind::String) {
                thread.waitingNative = native->text;
            }
        }
        state.threads.push_back(std::move(thread));
    }
    for (const JsonValue& node : registrationsNode->array) {
        SavedRegistration registration;
        registration.ownerKey = textMember(node, "owner");
        registration.kind = textMember(node, "kind");
        registration.eventName = textMember(node, "event");
        registration.interval = numberMember(node, "interval");
        registration.repeats = member(node, "repeats") != nullptr &&
                               member(node, "repeats")->kind == JsonValue::Kind::Bool &&
                               member(node, "repeats")->boolean;
        registration.clock = textMember(node, "clock");
        state.registrations.push_back(std::move(registration));
    }
    for (const JsonValue& node : timersNode->array) {
        SavedTimer timer;
        timer.ownerKey = textMember(node, "owner");
        timer.eventName = textMember(node, "event");
        timer.elapsed = numberMember(node, "elapsed");
        timer.fires = static_cast<std::int64_t>(numberMember(node, "fires"));
        state.timers.push_back(std::move(timer));
    }
    for (const JsonValue& node : objectivesNode->array) {
        SavedObjective objective;
        objective.ownerKey = textMember(node, "owner");
        objective.index = static_cast<std::int32_t>(numberMember(node, "index"));
        objective.state = static_cast<std::int32_t>(numberMember(node, "state"));
        state.objectives.push_back(std::move(objective));
    }
    return state;
}

SaveState captureSaveState(std::vector<SavedThread> threads,
                           std::vector<SavedRegistration> registrations, std::vector<SavedTimer> timers,
                           std::vector<SavedObjective> objectives) {
    SaveState state;
    state.threads = std::move(threads);
    state.registrations = std::move(registrations);
    state.timers = std::move(timers);
    state.objectives = std::move(objectives);
    std::stable_sort(state.threads.begin(), state.threads.end(),
                     [](const SavedThread& a, const SavedThread& b) { return ownerOrder(a.ownerKey, b.ownerKey); });
    std::stable_sort(state.registrations.begin(), state.registrations.end(),
                     [](const SavedRegistration& a, const SavedRegistration& b) {
                         return ownerOrder(a.ownerKey, b.ownerKey);
                     });
    std::stable_sort(state.timers.begin(), state.timers.end(),
                     [](const SavedTimer& a, const SavedTimer& b) { return ownerOrder(a.ownerKey, b.ownerKey); });
    std::stable_sort(state.objectives.begin(), state.objectives.end(),
                     [](const SavedObjective& a, const SavedObjective& b) {
                         if (a.ownerKey != b.ownerKey) return ownerOrder(a.ownerKey, b.ownerKey);
                         return a.index < b.index;
                     });
    return state;
}

RestoreReport restoreSaveState(const SaveState& state,
                               const std::map<std::string, std::string>& currentFingerprints) {
    RestoreReport report;
    for (const SavedThread& thread : state.threads) {
        ThreadRestore outcome;
        outcome.ownerKey = thread.ownerKey;
        outcome.script = thread.script;
        if (thread.waitingToken.has_value() || thread.waitingNative.has_value()) {
            outcome.state = ThreadRestoreState::RejectedLatentWait;
            outcome.reason = "waiting on " + thread.waitingNative.value_or(std::string()) + " token " +
                             std::to_string(thread.waitingToken.value_or(0)) +
                             "; the external answer cannot be restored";
            report.rejected += 1;
            report.threads.push_back(std::move(outcome));
            continue;
        }
        const auto found = currentFingerprints.find(thread.script);
        if (found == currentFingerprints.end()) {
            outcome.state = ThreadRestoreState::RejectedUnknownScript;
            outcome.reason = "the script is not in the current program set, so the thread was rejected";
            report.rejected += 1;
            report.threads.push_back(std::move(outcome));
            continue;
        }
        if (found->second != thread.fingerprint) {
            outcome.state = ThreadRestoreState::RejectedChangedScript;
            outcome.reason = "the script changed from " + thread.fingerprint + " to " + found->second +
                             ", so the thread was not resumed at a guessed program counter";
            report.rejected += 1;
            report.threads.push_back(std::move(outcome));
            continue;
        }
        outcome.state = ThreadRestoreState::Resumed;
        outcome.reason = "resumed at " + thread.function + "+" + std::to_string(thread.pc);
        report.resumed += 1;
        report.threads.push_back(std::move(outcome));
    }
    report.registrations = state.registrations;
    report.timers = state.timers;
    report.objectives = state.objectives;
    return report;
}

std::string RestoreReport::toReport() const {
    std::string text = "threads=" + std::to_string(threads.size()) + " resumed=" + std::to_string(resumed) +
                       " rejected=" + std::to_string(rejected) +
                       " registrations=" + std::to_string(registrations.size()) +
                       " timers=" + std::to_string(timers.size()) +
                       " objectives=" + std::to_string(objectives.size()) + "\n";
    for (const ThreadRestore& thread : threads) {
        const char* state = "rejectedunknownscript";
        switch (thread.state) {
            case ThreadRestoreState::Resumed:
                state = "resumed";
                break;
            case ThreadRestoreState::RejectedChangedScript:
                state = "rejectedchangedscript";
                break;
            case ThreadRestoreState::RejectedUnknownScript:
                state = "rejectedunknownscript";
                break;
            case ThreadRestoreState::RejectedLatentWait:
                state = "rejectedlatentwait";
                break;
        }
        text += "  " + thread.ownerKey + " " + thread.script + " " + state + " " + thread.reason + "\n";
    }
    return text;
}

std::string RestoreReport::digest() const { return sha256Hex(toReport()); }

std::string scriptFingerprint(const pex::PexObject& object) {
    std::string canonical = object.name + "\n" + object.parentClassName + "\n" + object.autoStateName + "\n";
    for (const pex::PexVariable& variable : object.variables) {
        canonical += "v:" + variable.name + ":" + variable.type + ":" +
                     std::to_string(variable.userFlags) + ":" + (variable.isConst ? "1" : "0") + "\n";
    }
    for (const pex::PexProperty& property : object.properties) {
        canonical += "p:" + property.name + ":" + property.type + ":" + std::to_string(property.flags) +
                     ":" + property.autoVarName + "\n";
    }
    for (const pex::PexStruct& structure : object.structs) {
        canonical += "s:" + structure.name + "\n";
        for (const pex::PexStructMember& member : structure.members) {
            canonical += "m:" + member.name + ":" + member.type + ":" +
                         std::to_string(member.userFlags) + "\n";
        }
    }
    for (const pex::PexState& state : object.states) {
        canonical += "st:" + state.name + "\n";
        for (const pex::PexFunction& function : state.functions) {
            canonical += "f:" + function.name + ":" + function.returnType + ":" +
                         (function.isNative ? "1" : "0") + ":" + (function.isGlobal ? "1" : "0") + "\n";
            for (const pex::PexTypedName& param : function.params) {
                canonical += "a:" + param.name + ":" + param.type + "\n";
            }
            for (const pex::PexTypedName& local : function.locals) {
                canonical += "l:" + local.name + ":" + local.type + "\n";
            }
            for (const pex::PexInstruction& instruction : function.instructions) {
                canonical += "i:" + std::to_string(instruction.opCode) + ":";
                for (const pex::PexValue& argument : instruction.args) {
                    canonical += std::to_string(static_cast<int>(argument.type)) + "," + argument.str + "," +
                                 std::to_string(argument.int_) + ",";
                }
                canonical += "\n";
            }
        }
    }
    return sha256Hex(canonical);
}

std::vector<SavedThread> Runtime::captureThreads() const {
    std::vector<SavedThread> threads;
    for (const auto& [token, waiting] : waiting_) {
        const Instance* instance = findInstance(waiting.execution.instance);
        if (instance == nullptr) continue;
        SavedThread thread;
        thread.ownerKey = instance->owner.key();
        thread.script = instance->script;
        if (const pex::PexObject* object = findObject(instance->script)) {
            thread.fingerprint = scriptFingerprint(*object);
        }
        thread.function = waiting.execution.function;
        if (waiting.execution.interpreter != nullptr) {
            const ThreadSnapshot snapshot = waiting.execution.interpreter->snapshot();
            thread.pc = snapshot.pc;
            thread.locals = snapshot.locals;
            thread.waitingNative = snapshot.latentNative;
        }
        thread.waitingToken = static_cast<std::int64_t>(token);
        threads.push_back(std::move(thread));
    }
    for (const Execution& execution : ready_) {
        const Instance* instance = findInstance(execution.instance);
        if (instance == nullptr || execution.interpreter == nullptr) continue;
        SavedThread thread;
        thread.ownerKey = instance->owner.key();
        thread.script = instance->script;
        if (const pex::PexObject* object = findObject(instance->script)) {
            thread.fingerprint = scriptFingerprint(*object);
        }
        thread.function = execution.function;
        const ThreadSnapshot snapshot = execution.interpreter->snapshot();
        thread.pc = snapshot.pc;
        thread.locals = snapshot.locals;
        threads.push_back(std::move(thread));
    }
    return threads;
}

}  // namespace fo4::papyrus