#include "fo4/papyrus/host.hpp"

namespace fo4::papyrus {

bool Host::gate(std::string_view key) {
    (void)key;
    return true;
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
