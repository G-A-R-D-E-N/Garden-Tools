#pragma once

#include <fo4/avm2/object.hpp>

#include <map>
#include <memory>
#include <string>
#include <utility>

namespace fo4::avm2 {

class ByteArrayObject;

/* A domain must keep its parent alive, since definitions visible from the parent stay resolvable. */
class ApplicationDomainObject : public Object {
 public:
  ApplicationDomainObject(Class* domain_class, std::shared_ptr<ApplicationDomainObject> parent);

  const std::shared_ptr<ApplicationDomainObject>& parent() const noexcept { return parent_; }

  /* A domain must keep its own class table, so sibling movies never see each other's classes. */
  const Class* find_local_definition(const std::string& name) const {
    const auto found = local_definitions_.find(name);
    return found == local_definitions_.end() ? nullptr : found->second;
  }
  void define_local_definition(const std::string& name, const Class* defined) {
    local_definitions_.emplace(name, defined);
  }

  const std::shared_ptr<ByteArrayObject>& domain_memory() const noexcept { return domain_memory_; }
  void set_domain_memory(std::shared_ptr<ByteArrayObject> memory) noexcept {
    domain_memory_ = std::move(memory);
  }

 private:
  std::shared_ptr<ApplicationDomainObject> parent_;
  std::map<std::string, const Class*> local_definitions_;
  std::shared_ptr<ByteArrayObject> domain_memory_;
};

}  // namespace fo4::avm2
