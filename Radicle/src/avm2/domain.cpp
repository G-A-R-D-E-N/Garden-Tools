#include <fo4/avm2/domain.hpp>
#include <fo4/avm2/name.hpp>
#include <fo4/avm2/runtime.hpp>
#include <fo4/avm2/value.hpp>

#include <memory>
#include <string_view>
#include <utility>

namespace fo4::avm2 {

ApplicationDomainObject::ApplicationDomainObject(Class* domain_class,
                                                 std::shared_ptr<ApplicationDomainObject> parent)
    : Object(ObjectKind::Object), parent_(std::move(parent)) {
  set_instance_class(domain_class);
  if (domain_class->prototype_object != nullptr) {
    set_prototype(domain_class->prototype_object);
  }
  Multiname multiname;
  multiname.name = "parentDomain";
  multiname.namespaces.push_back(Namespace{});
  Property& property = define_own_property(primary_key(multiname));
  property.kind = PropertyKind::Value;
  property.value = parent_ == nullptr ? Value::null() : Value::object(parent_);
}

std::shared_ptr<ApplicationDomainObject> Runtime::current_domain() {
  if (current_domain_ == nullptr) {
    Class* domain_class = const_cast<Class*>(find_class("flash.system::ApplicationDomain"));
    current_domain_ = std::make_shared<ApplicationDomainObject>(domain_class, nullptr);
  }
  return current_domain_;
}

const Class* Runtime::find_domain_definition(const ApplicationDomainObject& domain,
                                             std::string_view name) const {
  const Class* klass = find_class(name);
  if (klass == nullptr || klass->file == nullptr) {
    return klass;
  }
  const ApplicationDomainObject* owner = nullptr;
  for (const FileState& state : files_) {
    if (state.file == klass->file) {
      owner = state.domain.get();
    }
  }
  for (const ApplicationDomainObject* scope = &domain; scope != nullptr;
       scope = scope->parent().get()) {
    if (owner == scope || (owner == nullptr && scope->parent() == nullptr)) {
      return klass;
    }
  }
  return nullptr;
}

}  // namespace fo4::avm2
