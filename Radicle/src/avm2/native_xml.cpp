#include <fo4/avm2/class.hpp>
#include <fo4/avm2/coercion.hpp>
#include <fo4/avm2/interpreter.hpp>
#include <fo4/avm2/object.hpp>
#include <fo4/avm2/runtime.hpp>
#include <fo4/avm2/value.hpp>
#include <fo4/avm2/xml.hpp>

#include "interpreter_internal.hpp"
#include "native_internal.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace fo4::avm2 {

namespace {

XmlObject* xml_self(const Value& self) {
  if (!self.is_object() || self.as_object()->kind() != ObjectKind::Xml) {
    return nullptr;
  }
  return static_cast<XmlObject*>(self.as_object().get());
}

Value make_xml(Interpreter& interpreter, bool list, std::vector<std::shared_ptr<XmlNode>> nodes) {
  Class* klass = const_cast<Class*>(interpreter.runtime().find_class(list ? "XMLList" : "XML"));
  return Value::object(std::make_shared<XmlObject>(klass, std::move(nodes), list));
}

void add_instance_trait(Class& klass, const char* name, TraitKind kind, native::Id id) {
  auto function = std::make_shared<FunctionObject>(name, nullptr, kNoMethod);
  function->set_native_id(static_cast<std::uint32_t>(id));
  function->set_owner_class(&klass);
  Trait trait;
  trait.multiname = builtin_name(name);
  trait.kind = kind;
  trait.function = std::move(function);
  klass.instance_traits.push_back(std::move(trait));
}

ExecutionResult type_failure(std::string message) {
  ExecutionResult failure;
  failure.status = InterpreterStatus::TypeError;
  failure.message = std::move(message);
  return failure;
}

ExecutionResult convert(Interpreter& interpreter, const Value& argument, bool list) {
  if (argument.is_object() && argument.as_object()->kind() == ObjectKind::Xml) {
    const auto& source = static_cast<const XmlObject&>(*argument.as_object());
    if (list) {
      return native_value(make_xml(interpreter, true, source.nodes()));
    }
    if (!source.is_list()) {
      return native_value(argument);
    }
    if (source.nodes().size() != 1) {
      return type_failure("an XMLList of " + std::to_string(source.nodes().size()) +
                          " nodes does not convert to XML");
    }
    return native_value(make_xml(interpreter, false, source.nodes()));
  }
  if (argument.is_nullish()) {
    return native_value(make_xml(interpreter, list, {}));
  }
  std::vector<std::shared_ptr<XmlNode>> nodes;
  std::string message;
  if (!parse_xml(to_string(argument), list, nodes, message)) {
    return type_failure(message);
  }
  return native_value(make_xml(interpreter, list, std::move(nodes)));
}

ExecutionResult children(Interpreter& interpreter, const Value& self) {
  XmlObject* object = xml_self(self);
  if (object == nullptr) {
    return native_value(Value::undefined());
  }
  std::vector<std::shared_ptr<XmlNode>> found;
  for (const auto& node : object->nodes()) {
    if (node->kind == XmlNode::Kind::Element) {
      found.insert(found.end(), node->children.begin(), node->children.end());
    }
  }
  return native_value(make_xml(interpreter, true, std::move(found)));
}

ExecutionResult text_children(Interpreter& interpreter, const Value& self) {
  XmlObject* object = xml_self(self);
  if (object == nullptr) {
    return native_value(Value::undefined());
  }
  std::vector<std::shared_ptr<XmlNode>> found;
  for (const auto& node : object->nodes()) {
    if (node->kind != XmlNode::Kind::Element) {
      continue;
    }
    for (const auto& child : node->children) {
      if (child->kind == XmlNode::Kind::Text) {
        found.push_back(child);
      }
    }
  }
  return native_value(make_xml(interpreter, true, std::move(found)));
}

ExecutionResult length_of(const Value& self) {
  XmlObject* object = xml_self(self);
  if (object == nullptr) {
    return native_value(Value::undefined());
  }
  return native_value(Value::number(static_cast<double>(object->nodes().size())));
}

ExecutionResult text_of(const Value& self) {
  XmlObject* object = xml_self(self);
  if (object == nullptr) {
    return native_value(Value::undefined());
  }
  return native_value(Value::string(xml_default_string(*object)));
}

ExecutionResult markup_of(const Value& self) {
  XmlObject* object = xml_self(self);
  if (object == nullptr) {
    return native_value(Value::undefined());
  }
  std::string markup;
  for (std::size_t index = 0; index < object->nodes().size(); ++index) {
    if (index != 0) {
      markup += "\n";
    }
    markup += xml_markup(*object->nodes()[index]);
  }
  return native_value(Value::string(markup));
}

ExecutionResult copy_of(const Value& self) {
  XmlObject* object = xml_self(self);
  if (object == nullptr) {
    return native_value(Value::undefined());
  }
  std::vector<std::shared_ptr<XmlNode>> copies;
  const auto document_nodes = std::make_shared<std::size_t>(0);
  for (const auto& node : object->nodes()) {
    copies.push_back(copy_xml_node(*node, document_nodes));
  }
  return native_value(Value::object(std::make_shared<XmlObject>(
      object->instance_class(), std::move(copies), object->is_list())));
}

constexpr const char* kXmlNodeLimitMessage = "the XML has more nodes than the limit";
constexpr const char* kXmlDepthLimitMessage = "the XML would nest deeper than the limit";

std::size_t subtree_size(const XmlNode& node) {
  std::size_t count = 1;
  for (const auto& attribute : node.attributes) {
    count += subtree_size(*attribute);
  }
  for (const auto& child : node.children) {
    count += subtree_size(*child);
  }
  return count;
}

std::shared_ptr<XmlNode> new_xml_node(XmlNode::Kind kind, std::string name, std::string text,
                                      const std::shared_ptr<std::size_t>& document_nodes) {
  auto node = std::make_shared<XmlNode>();
  node->kind = kind;
  node->name = std::move(name);
  node->text = std::move(text);
  node->document_nodes = document_nodes;
  ++*document_nodes;
  return node;
}

void move_to_counter(XmlNode& node, const std::shared_ptr<std::size_t>& counter) {
  node.document_nodes = counter;
  ++*counter;
  for (const auto& attribute : node.attributes) {
    move_to_counter(*attribute, counter);
  }
  for (const auto& child : node.children) {
    move_to_counter(*child, counter);
  }
}

}  // namespace

Value read_xml_property(Interpreter& interpreter, const XmlObject& object,
                        const Multiname& multiname) {
  std::vector<std::shared_ptr<XmlNode>> found;
  std::uint32_t index = 0;
  if (multiname.attribute) {
    for (const auto& node : object.nodes()) {
      if (node->kind != XmlNode::Kind::Element) {
        continue;
      }
      for (const auto& attribute : node->attributes) {
        if (attribute->name == multiname.name) {
          found.push_back(attribute);
        }
      }
    }
  } else if (detail::is_array_index(multiname.name, index)) {
    if (index >= object.nodes().size()) {
      return Value::undefined();
    }
    return make_xml(interpreter, false, {object.nodes()[index]});
  } else {
    for (const auto& node : object.nodes()) {
      if (node->kind != XmlNode::Kind::Element) {
        continue;
      }
      for (const auto& child : node->children) {
        if (child->kind == XmlNode::Kind::Element && child->name == multiname.name) {
          found.push_back(child);
        }
      }
    }
  }
  return make_xml(interpreter, true, std::move(found));
}

ExecutionResult write_xml_property(XmlObject& object, const Multiname& multiname,
                                   const Value& value) {
  std::uint32_t index = 0;
  if (!multiname.has_fixed_name() ||
      (!multiname.attribute && detail::is_array_index(multiname.name, index))) {
    return type_failure("writing " + multiname.name + " on XML needs a child or attribute name");
  }
  if (!is_xml_name(multiname.name)) {
    return type_failure("\"" + multiname.name + "\" is not a valid XML name");
  }
  if (object.nodes().size() != 1) {
    return type_failure("writing a property needs one XML node, not " +
                        std::to_string(object.nodes().size()));
  }
  XmlNode& target = *object.nodes()[0];
  if (target.kind != XmlNode::Kind::Element) {
    return type_failure("only an XML element takes a property write");
  }
  const std::shared_ptr<std::size_t>& budget = target.document_nodes;
  const std::string text = to_string(value);
  if (multiname.attribute) {
    for (const auto& attribute : target.attributes) {
      if (attribute->name == multiname.name) {
        attribute->text = text;
        return ExecutionResult{};
      }
    }
    if (*budget + 1 > kMaxXmlNodes) {
      return type_failure(kXmlNodeLimitMessage);
    }
    target.attributes.push_back(
        new_xml_node(XmlNode::Kind::Attribute, multiname.name, text, budget));
    return ExecutionResult{};
  }
  std::shared_ptr<XmlNode> element;
  for (const auto& child : target.children) {
    if (child->kind == XmlNode::Kind::Element && child->name == multiname.name) {
      element = child;
      break;
    }
  }
  if (element != nullptr) {
    std::size_t removed = 0;
    for (const auto& child : element->children) {
      removed += subtree_size(*child);
    }
    const std::size_t added = text.empty() ? 0 : 1;
    if (*budget - removed + added > kMaxXmlNodes) {
      return type_failure(kXmlNodeLimitMessage);
    }
    const auto detached = std::make_shared<std::size_t>(0);
    for (const auto& child : element->children) {
      move_to_counter(*child, detached);
    }
    *budget -= removed;
    element->children.clear();
    if (!text.empty()) {
      element->children.push_back(new_xml_node(XmlNode::Kind::Text, "", text, budget));
    }
    return ExecutionResult{};
  }
  if (target.depth + 1 >= kMaxXmlDepth) {
    return type_failure(kXmlDepthLimitMessage);
  }
  if (*budget + (text.empty() ? 1 : 2) > kMaxXmlNodes) {
    return type_failure(kXmlNodeLimitMessage);
  }
  auto created = new_xml_node(XmlNode::Kind::Element, multiname.name, "", budget);
  created->depth = target.depth + 1;
  if (!text.empty()) {
    created->children.push_back(new_xml_node(XmlNode::Kind::Text, "", text, budget));
  }
  target.children.push_back(created);
  return ExecutionResult{};
}

bool try_xml_native(Interpreter& interpreter, native::Id id, const Value& self,
                    const std::vector<Value>& arguments, std::size_t depth, ExecutionResult& out) {
  (void)depth;
  switch (id) {
    case native::Id::xml_construct:
      out = convert(interpreter, argument_at(arguments, 0), false);
      return true;
    case native::Id::xmllist_construct:
      out = convert(interpreter, argument_at(arguments, 0), true);
      return true;
    case native::Id::xml_children:
      out = children(interpreter, self);
      return true;
    case native::Id::xml_length:
      out = length_of(self);
      return true;
    case native::Id::xml_to_string:
      out = text_of(self);
      return true;
    case native::Id::xml_to_xml_string:
      out = markup_of(self);
      return true;
    case native::Id::xml_text:
      out = text_children(interpreter, self);
      return true;
    case native::Id::xml_copy:
      out = copy_of(self);
      return true;
    default:
      return false;
  }
}

RuntimeStatus Runtime::install_xml_builtins() {
  Class* object_class = const_cast<Class*>(find_class("Object"));
  Class* xml = add_builtin_class("XML", "", object_class);
  Class* xml_list = add_builtin_class("XMLList", "", object_class);
  if (object_class == nullptr || xml == nullptr || xml_list == nullptr) {
    return RuntimeStatus::InvalidTrait;
  }
  xml->native_constructor = static_cast<std::uint32_t>(native::Id::xml_construct);
  xml_list->native_constructor = static_cast<std::uint32_t>(native::Id::xmllist_construct);
  for (Class* klass : {xml, xml_list}) {
    add_instance_trait(*klass, "children", TraitKind::Method, native::Id::xml_children);
    add_instance_trait(*klass, "length", TraitKind::Method, native::Id::xml_length);
    add_instance_trait(*klass, "toString", TraitKind::Method, native::Id::xml_to_string);
    add_instance_trait(*klass, "toXMLString", TraitKind::Method, native::Id::xml_to_xml_string);
    add_instance_trait(*klass, "text", TraitKind::Method, native::Id::xml_text);
    add_instance_trait(*klass, "copy", TraitKind::Method, native::Id::xml_copy);
  }
  return RuntimeStatus::Ok;
}

}  // namespace fo4::avm2
