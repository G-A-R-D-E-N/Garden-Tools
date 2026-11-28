#include <fo4/avm2/xml.hpp>

#include <fo4/avm2/class.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::avm2 {

namespace {

std::string escape(std::string_view text, bool attribute) {
  std::string out;
  out.reserve(text.size());
  for (const char character : text) {
    if (character == '&') {
      out += "&amp;";
    } else if (character == '<') {
      out += "&lt;";
    } else if (character == '>' && !attribute) {
      out += "&gt;";
    } else if (character == '"' && attribute) {
      out += "&quot;";
    } else if (character == '\r') {
      out += "&#13;";
    } else if (attribute && character == '\n') {
      out += "&#10;";
    } else if (attribute && character == '\t') {
      out += "&#9;";
    } else {
      out += character;
    }
  }
  return out;
}

void append_markup(const XmlNode& node, std::string& out) {
  if (node.kind == XmlNode::Kind::Text) {
    out += escape(node.text, false);
    return;
  }
  if (node.kind == XmlNode::Kind::Attribute) {
    out += node.name + "=\"" + escape(node.text, true) + "\"";
    return;
  }
  out += "<" + node.name;
  for (const auto& attribute : node.attributes) {
    out += " ";
    append_markup(*attribute, out);
  }
  if (node.children.empty()) {
    out += "/>";
    return;
  }
  out += ">";
  for (const auto& child : node.children) {
    append_markup(*child, out);
  }
  out += "</" + node.name + ">";
}

std::shared_ptr<XmlNode> copy_at_depth(const XmlNode& node,
                                       const std::shared_ptr<std::size_t>& document_nodes,
                                       std::size_t depth) {
  auto copy = std::make_shared<XmlNode>();
  copy->kind = node.kind;
  copy->name = node.name;
  copy->text = node.text;
  copy->depth = depth;
  copy->document_nodes = document_nodes;
  ++*document_nodes;
  for (const auto& attribute : node.attributes) {
    copy->attributes.push_back(copy_at_depth(*attribute, document_nodes, depth));
  }
  for (const auto& child : node.children) {
    copy->children.push_back(copy_at_depth(*child, document_nodes, depth + 1));
  }
  return copy;
}

bool simple_content(const XmlNode& node) {
  for (const auto& child : node.children) {
    if (child->kind == XmlNode::Kind::Element) {
      return false;
    }
  }
  return true;
}

}  // namespace

XmlObject::XmlObject(Class* klass, std::vector<std::shared_ptr<XmlNode>> nodes, bool list)
    : Object(ObjectKind::Xml), nodes_(std::move(nodes)), list_(list) {
  if (klass != nullptr) {
    set_instance_class(klass);
    if (klass->prototype_object != nullptr) {
      set_prototype(klass->prototype_object);
    }
  }
}

std::shared_ptr<XmlNode> copy_xml_node(const XmlNode& node,
                                       const std::shared_ptr<std::size_t>& document_nodes) {
  return copy_at_depth(node, document_nodes, 0);
}

std::string xml_markup(const XmlNode& node) {
  std::string out;
  append_markup(node, out);
  return out;
}

std::string xml_text(const XmlNode& node) {
  if (node.kind != XmlNode::Kind::Element) {
    return node.text;
  }
  if (!simple_content(node)) {
    return xml_markup(node);
  }
  std::string text;
  for (const auto& child : node.children) {
    text += child->text;
  }
  return text;
}

std::string xml_default_string(const XmlObject& object) {
  std::string text;
  for (std::size_t index = 0; index < object.nodes().size(); ++index) {
    if (index != 0) {
      text += "\n";
    }
    text += xml_text(*object.nodes()[index]);
  }
  return text;
}

}  // namespace fo4::avm2
