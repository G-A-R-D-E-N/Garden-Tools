#include "fo4/papyrus/host.hpp"

namespace fo4::papyrus {

bool Host::gate(std::string_view key) {
    (void)key;
    return true;
}

bool Host::setState(ObjectRef& object, const std::string& state, Status& status) {
    object.state = state;
    status = Status::Ok;
    return true;
}

bool Host::callLatent(const std::string& script, const std::string& function,
                      const std::vector<Value>& args, Status& status, std::uint64_t& token) {
    (void)script;
    (void)function;
    (void)args;
    status = Status::Ok;
    token = 0;
    return false;
}

bool Host::getVariable(const ObjectRef& object, const std::string& name, Value& out, Status& status) {
    (void)object;
    (void)name;
    (void)out;
    status = Status::UnknownIdentifier;
    return false;
}

bool Host::setVariable(const ObjectRef& object, const std::string& name, const Value& value,
                       Status& status) {
    (void)object;
    (void)name;
    (void)value;
    status = Status::UnknownIdentifier;
    return false;
}

}
