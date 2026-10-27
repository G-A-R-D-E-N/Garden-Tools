#pragma once

#include <fo4/avm2/object.hpp>

#include <memory>

namespace fo4::avm2 {

/* A domain must keep its parent alive, since definitions visible from the parent stay resolvable. */
class ApplicationDomainObject : public Object {
 public:
  ApplicationDomainObject(Class* domain_class, std::shared_ptr<ApplicationDomainObject> parent);

  const std::shared_ptr<ApplicationDomainObject>& parent() const noexcept { return parent_; }

 private:
  std::shared_ptr<ApplicationDomainObject> parent_;
};

}  // namespace fo4::avm2
