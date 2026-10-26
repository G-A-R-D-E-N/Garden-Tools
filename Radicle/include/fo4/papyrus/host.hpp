#pragma once

#include "fo4/papyrus/status.hpp"
#include "fo4/papyrus/value.hpp"
#include "fo4/pex/pex.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace fo4::papyrus {

class Host {
public:
    virtual ~Host() = default;

    virtual const pex::PexObject* findObject(const std::string& name) const = 0;
    virtual bool getProperty(const ObjectRef& object, const std::string& name, Value& out,
                             Status& status) = 0;
    virtual bool setProperty(const ObjectRef& object, const std::string& name, const Value& value,
                             Status& status) = 0;
    virtual bool callNative(const std::string& script, const std::string& function,
                            const std::vector<Value>& args, Value& out, Status& status) = 0;

    // Invariant: gate is default-allow; a Host may refuse a semantic key.
    virtual bool gate(std::string_view key);

    virtual bool getVariable(const ObjectRef& object, const std::string& name, Value& out,
                             Status& status);
    virtual bool setVariable(const ObjectRef& object, const std::string& name, const Value& value,
                             Status& status);
};

}
