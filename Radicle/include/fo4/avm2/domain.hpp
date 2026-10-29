#pragma once

#include <fo4/avm2/object.hpp>

#include <memory>
#include <utility>

namespace fo4::avm2 {

class ByteArrayObject;

/* A domain must keep its parent alive, since definitions visible from the parent stay resolvable. */
class ApplicationDomainObject : public Object {
 public:
  ApplicationDomainObject(Class* domain_class, std::shared_ptr<ApplicationDomainObject> parent);

  const std::shared_ptr<ApplicationDomainObject>& parent() const noexcept { return parent_; }

  const std::shared_ptr<ByteArrayObject>& domain_memory() const noexcept { return domain_memory_; }
  void set_domain_memory(std::shared_ptr<ByteArrayObject> memory) noexcept {
    domain_memory_ = std::move(memory);
  }

 private:
  std::shared_ptr<ApplicationDomainObject> parent_;
  std::shared_ptr<ByteArrayObject> domain_memory_;
};

}  // namespace fo4::avm2
