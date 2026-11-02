#include <fo4/avm2/xml.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fo4::avm2 {

namespace {

constexpr std::string_view kByteOrderMark = "\xEF\xBB\xBF";
constexpr std::size_t kMaxEntityLength = 12;
constexpr std::uint32_t kMaxCodePoint = 0x10FFFF;

bool is_space(char character) noexcept {
  return character == ' ' || character == '\t' || character == '\r' || character == '\n';
}

bool is_name_start(char character) noexcept {
  const auto byte = static_cast<unsigned char>(character);
  return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
         character == '_' || character == ':' || byte >= 0x80;
}

bool is_name_char(char character) noexcept {
  return is_name_start(character) || (character >= '0' && character <= '9') || character == '-' ||
         character == '.';
}

bool is_blank(std::string_view text) noexcept {
  for (const char character : text) {
    if (!is_space(character)) {
      return false;
    }
  }
  return true;
}

void append_utf8(std::uint32_t code, std::string& out) {
  if (code < 0x80) {
    out += static_cast<char>(code);
  } else if (code < 0x800) {
    out += static_cast<char>(0xC0 | (code >> 6));
    out += static_cast<char>(0x80 | (code & 0x3F));
  } else if (code < 0x10000) {
    out += static_cast<char>(0xE0 | (code >> 12));
    out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (code & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (code >> 18));
    out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (code & 0x3F));
  }
}

class Parser {
 public:
  Parser(std::string_view text, bool fragment, std::string& message)
      : text_(text), fragment_(fragment), message_(message) {}

  bool run(std::vector<std::shared_ptr<XmlNode>>& roots) {
    bool element_seen = false;
    while (position_ < text_.size()) {
      if (starts_with("<?")) {
        if (!skip_past("?>", position_ + 2)) {
          return fail("a processing instruction is not closed");
        }
      } else if (starts_with("<!--")) {
        if (!skip_past("-->", position_ + 4)) {
          return fail("a comment is not closed");
        }
      } else if (starts_with("<![CDATA[")) {
        std::string value;
        if (!read_cdata(value) || !add_top_text(value, roots)) {
          return false;
        }
      } else if (starts_with("<!")) {
        return fail("markup declarations are not supported");
      } else if (starts_with("</")) {
        return fail("a closing tag has no opening tag");
      } else if (peek() == '<') {
        if (element_seen && !fragment_) {
          return fail("a document holds only one root element");
        }
        element_seen = true;
        std::shared_ptr<XmlNode> element;
        if (!parse_element(0, element)) {
          return false;
        }
        roots.push_back(std::move(element));
      } else {
        std::string value;
        if (!read_text(value) || !add_top_text(value, roots)) {
          return false;
        }
      }
    }
    return true;
  }

 private:
  bool at_end() const noexcept { return position_ >= text_.size(); }
  char peek() const noexcept { return text_[position_]; }

  bool starts_with(std::string_view prefix) const noexcept {
    return text_.substr(position_, prefix.size()) == prefix;
  }

  bool skip_past(std::string_view terminator, std::size_t from) {
    const std::size_t end = text_.find(terminator, from);
    if (end == std::string_view::npos) {
      return false;
    }
    position_ = end + terminator.size();
    return true;
  }

  void skip_space() noexcept {
    while (!at_end() && is_space(peek())) {
      ++position_;
    }
  }

  bool fail(const char* reason) {
    message_ = std::string{"XML parser failure: "} + reason;
    return false;
  }

  bool new_node(XmlNode::Kind kind, std::shared_ptr<XmlNode>& out) {
    if (++*document_nodes_ > kMaxXmlNodes) {
      return fail("the XML has more nodes than the limit");
    }
    out = std::make_shared<XmlNode>();
    out->kind = kind;
    out->document_nodes = document_nodes_;
    return true;
  }

  bool parse_name(std::string& name) {
    if (at_end() || !is_name_start(peek())) {
      return false;
    }
    const std::size_t start = position_;
    while (!at_end() && is_name_char(peek())) {
      ++position_;
    }
    name.assign(text_.substr(start, position_ - start));
    return true;
  }

  bool read_cdata(std::string& value) {
    position_ += 9;
    const std::size_t end = text_.find("]]>", position_);
    if (end == std::string_view::npos) {
      return fail("a CDATA section is not closed");
    }
    value.assign(text_.substr(position_, end - position_));
    position_ = end + 3;
    return true;
  }

  bool read_text(std::string& value) {
    const std::size_t end = text_.find('<', position_);
    const std::size_t stop = end == std::string_view::npos ? text_.size() : end;
    if (!decode(text_.substr(position_, stop - position_), false, value)) {
      return false;
    }
    position_ = stop;
    return true;
  }

  bool add_top_text(const std::string& value, std::vector<std::shared_ptr<XmlNode>>& roots) {
    if (value.find(kByteOrderMark) != std::string::npos) {
      return fail("a byte order mark is not allowed outside the root element");
    }
    if (is_blank(value)) {
      return true;
    }
    if (!fragment_) {
      return fail("text is outside the root element");
    }
    std::shared_ptr<XmlNode> node;
    if (!new_node(XmlNode::Kind::Text, node)) {
      return false;
    }
    node->text = value;
    roots.push_back(std::move(node));
    return true;
  }

  bool add_child_text(const std::shared_ptr<XmlNode>& element, const std::string& value) {
    if (is_blank(value)) {
      return true;
    }
    std::shared_ptr<XmlNode> node;
    if (!new_node(XmlNode::Kind::Text, node)) {
      return false;
    }
    node->text = value;
    element->children.push_back(std::move(node));
    return true;
  }

  bool decode(std::string_view raw, bool attribute, std::string& out) {
    out.clear();
    out.reserve(raw.size());
    for (std::size_t index = 0; index < raw.size(); ++index) {
      const char character = raw[index];
      if (character == '\r') {
        if (index + 1 < raw.size() && raw[index + 1] == '\n') {
          ++index;
        }
        out += attribute ? ' ' : '\n';
      } else if (attribute && (character == '\n' || character == '\t')) {
        out += ' ';
      } else if (attribute && character == '<') {
        return fail("an attribute value holds a '<'");
      } else if (character == '&') {
        std::size_t end = index + 1;
        while (end < raw.size() && end <= index + kMaxEntityLength && raw[end] != ';') {
          ++end;
        }
        if (end >= raw.size() || raw[end] != ';') {
          return fail("an entity reference is not closed");
        }
        if (!append_entity(raw.substr(index + 1, end - index - 1), out)) {
          return false;
        }
        index = end;
      } else {
        out += character;
      }
    }
    return true;
  }

  bool append_entity(std::string_view entity, std::string& out) {
    if (entity == "lt") {
      out += '<';
    } else if (entity == "gt") {
      out += '>';
    } else if (entity == "amp") {
      out += '&';
    } else if (entity == "quot") {
      out += '"';
    } else if (entity == "apos") {
      out += '\'';
    } else if (entity.size() >= 2 && entity[0] == '#') {
      const bool hex = entity[1] == 'x';
      const std::string_view digits = entity.substr(hex ? 2 : 1);
      if (digits.empty()) {
        return fail("a character reference is empty");
      }
      std::uint32_t code = 0;
      for (const char digit : digits) {
        int value = -1;
        if (digit >= '0' && digit <= '9') {
          value = digit - '0';
        } else if (hex && digit >= 'a' && digit <= 'f') {
          value = digit - 'a' + 10;
        } else if (hex && digit >= 'A' && digit <= 'F') {
          value = digit - 'A' + 10;
        }
        if (value < 0) {
          return fail("a character reference is malformed");
        }
        code = code * (hex ? 16u : 10u) + static_cast<std::uint32_t>(value);
        if (code > kMaxCodePoint) {
          return fail("a character reference is out of range");
        }
      }
      if (code == 0 || (code >= 0xD800 && code <= 0xDFFF)) {
        return fail("a character reference is not a character");
      }
      append_utf8(code, out);
    } else {
      return fail("an entity is not known");
    }
    return true;
  }

  bool parse_attributes(const std::shared_ptr<XmlNode>& element, bool& self_closing) {
    std::unordered_set<std::string> names;
    for (;;) {
      skip_space();
      if (at_end()) {
        return fail("an element is not closed");
      }
      if (starts_with("/>")) {
        position_ += 2;
        self_closing = true;
        return true;
      }
      if (peek() == '>') {
        ++position_;
        self_closing = false;
        return true;
      }
      std::string name;
      if (!parse_name(name)) {
        return fail("an attribute name is malformed");
      }
      skip_space();
      if (at_end() || peek() != '=') {
        return fail("an attribute has no value");
      }
      ++position_;
      skip_space();
      if (at_end() || (peek() != '"' && peek() != '\'')) {
        return fail("an attribute value is not quoted");
      }
      const char quote = text_[position_++];
      const std::size_t end = text_.find(quote, position_);
      if (end == std::string_view::npos) {
        return fail("an attribute value is not closed");
      }
      std::string value;
      if (!decode(text_.substr(position_, end - position_), true, value)) {
        return false;
      }
      position_ = end + 1;
      if (!names.insert(name).second) {
        return fail("an attribute is repeated");
      }
      std::shared_ptr<XmlNode> attribute;
      if (!new_node(XmlNode::Kind::Attribute, attribute)) {
        return false;
      }
      attribute->name = std::move(name);
      attribute->text = std::move(value);
      element->attributes.push_back(std::move(attribute));
    }
  }

  bool parse_element(std::size_t depth, std::shared_ptr<XmlNode>& out) {
    if (depth >= kMaxXmlDepth) {
      return fail("elements are nested deeper than the limit");
    }
    ++position_;
    std::string name;
    if (!parse_name(name)) {
      return fail("an element name is malformed");
    }
    std::shared_ptr<XmlNode> element;
    if (!new_node(XmlNode::Kind::Element, element)) {
      return false;
    }
    element->depth = depth;
    element->name = std::move(name);
    bool self_closing = false;
    if (!parse_attributes(element, self_closing)) {
      return false;
    }
    if (self_closing) {
      out = std::move(element);
      return true;
    }
    for (;;) {
      if (at_end()) {
        return fail("an element is not closed");
      }
      if (starts_with("</")) {
        position_ += 2;
        std::string closing;
        if (!parse_name(closing) || closing != element->name) {
          return fail("a closing tag does not match its element");
        }
        skip_space();
        if (at_end() || peek() != '>') {
          return fail("a closing tag is malformed");
        }
        ++position_;
        break;
      }
      if (starts_with("<!--")) {
        if (!skip_past("-->", position_ + 4)) {
          return fail("a comment is not closed");
        }
      } else if (starts_with("<?")) {
        if (!skip_past("?>", position_ + 2)) {
          return fail("a processing instruction is not closed");
        }
      } else if (starts_with("<![CDATA[")) {
        std::string value;
        if (!read_cdata(value) || !add_child_text(element, value)) {
          return false;
        }
      } else if (starts_with("<!")) {
        return fail("markup declarations are not supported");
      } else if (peek() == '<') {
        std::shared_ptr<XmlNode> child;
        if (!parse_element(depth + 1, child)) {
          return false;
        }
        element->children.push_back(std::move(child));
      } else {
        std::string value;
        if (!read_text(value) || !add_child_text(element, value)) {
          return false;
        }
      }
    }
    out = std::move(element);
    return true;
  }

  std::string_view text_;
  bool fragment_;
  std::string& message_;
  std::size_t position_{0};
  std::shared_ptr<std::size_t> document_nodes_{std::make_shared<std::size_t>(0)};
};

}  // namespace

bool is_xml_name(std::string_view name) {
  if (name.empty() || !is_name_start(name.front())) {
    return false;
  }
  for (const char character : name) {
    if (!is_name_char(character)) {
      return false;
    }
  }
  return true;
}

bool parse_xml(std::string_view text, bool fragment, std::vector<std::shared_ptr<XmlNode>>& out,
               std::string& message) {
  if (text.size() > kMaxXmlBytes) {
    message = "XML parser failure: the text is larger than the limit";
    return false;
  }
  std::vector<std::shared_ptr<XmlNode>> roots;
  Parser parser(text, fragment, message);
  if (!parser.run(roots)) {
    return false;
  }
  out = std::move(roots);
  return true;
}

}  // namespace fo4::avm2
