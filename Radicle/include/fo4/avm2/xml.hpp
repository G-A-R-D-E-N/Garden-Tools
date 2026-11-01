#pragma once

#include <fo4/avm2/object.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::avm2 {

inline constexpr std::size_t kMaxXmlDepth = 64;
inline constexpr std::size_t kMaxXmlBytes = std::size_t{16} * 1024 * 1024;
inline constexpr std::size_t kMaxXmlNodes = std::size_t{1} << 18;

struct XmlNode {
  enum class Kind : std::uint8_t {
    Element = 0,
    Text,
    Attribute,
  };

  Kind kind{Kind::Element};
  std::string name;
  std::string text;
  std::vector<std::shared_ptr<XmlNode>> attributes;
  std::vector<std::shared_ptr<XmlNode>> children;
  /* Invariant: all nodes of one document share this counter of its nodes. */
  std::shared_ptr<std::size_t> document_nodes;
  /* Invariant: depth is never below the nesting level (root 0); detached nodes keep their depth. */
  std::size_t depth{0};
};

class XmlObject : public Object {
 public:
  XmlObject(Class* klass, std::vector<std::shared_ptr<XmlNode>> nodes, bool list);

  const std::vector<std::shared_ptr<XmlNode>>& nodes() const noexcept { return nodes_; }
  bool is_list() const noexcept { return list_; }

 private:
  std::vector<std::shared_ptr<XmlNode>> nodes_;
  bool list_{false};
};

bool parse_xml(std::string_view text, bool fragment, std::vector<std::shared_ptr<XmlNode>>& out,
               std::string& message);
bool is_xml_name(std::string_view name);
std::shared_ptr<XmlNode> copy_xml_node(const XmlNode& node,
                                       const std::shared_ptr<std::size_t>& document_nodes);
std::string xml_markup(const XmlNode& node);
std::string xml_text(const XmlNode& node);
std::string xml_default_string(const XmlObject& object);

}  // namespace fo4::avm2
