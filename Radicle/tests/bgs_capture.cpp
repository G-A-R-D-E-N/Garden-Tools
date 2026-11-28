#include "bgs_capture.hpp"

#include <fo4/abc/abc.hpp>
#include <fo4/avm2/coercion.hpp>
#include <fo4/avm2/swf_runtime.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <fstream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace bgs_capture {

namespace {

namespace abc = fo4::abc;
namespace avm2 = fo4::avm2;
using json = nlohmann::json;

constexpr std::string_view kRandomNote =
    " (possibly RNG-driven: the movie references Math.random, which no host hook sees)";

std::string lower(std::string text) {
  for (char& c : text) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return text;
}

avm2::Value typed_value(const json& node) {
  if (!node.is_object() || !node.contains("type") || !node.contains("value") ||
      !node["type"].is_string()) {
    throw std::runtime_error("a value must be an object with type and value");
  }
  const std::string original = node["type"].get<std::string>();
  const std::string type = lower(original);
  const json& value = node["value"];
  if (type == "string" && value.is_string()) return avm2::Value::string(value.get<std::string>());
  if (type == "number" && value.is_number()) return avm2::Value::number(value.get<double>());
  if (type == "boolean" && value.is_boolean()) return avm2::Value::boolean(value.get<bool>());
  if (type == "int" && value.is_number_integer()) {
    const std::int64_t number = value.get<std::int64_t>();
    if (number >= std::numeric_limits<std::int32_t>::min() &&
        number <= std::numeric_limits<std::int32_t>::max()) {
      return avm2::Value::int32(static_cast<std::int32_t>(number));
    }
  }
  if (type == "uint" && value.is_number_integer()) {
    const std::int64_t number = value.get<std::int64_t>();
    if (number >= 0 && number <= std::numeric_limits<std::uint32_t>::max()) {
      return avm2::Value::uint32(static_cast<std::uint32_t>(number));
    }
  }
  if (type == "null" && value.is_null()) return avm2::Value::null();
  if (type == "undefined" && value.is_null()) return avm2::Value::undefined();
  const bool known = type == "string" || type == "number" || type == "boolean" ||
                     type == "int" || type == "uint" || type == "null" || type == "undefined";
  if (!known) {
    throw std::runtime_error("unknown value type '" + original + "'");
  }
  throw std::runtime_error("value does not fit type '" + original + "'");
}

std::vector<avm2::Value> typed_list(const json& node) {
  if (!node.is_array()) {
    throw std::runtime_error("args must be a JSON array");
  }
  std::vector<avm2::Value> values;
  for (const json& item : node) {
    values.push_back(typed_value(item));
  }
  return values;
}

avm2::Value typed_answer(const json& node) {
  return node.is_null() ? avm2::Value::null() : typed_value(node);
}

template <typename Integer>
Integer parse_integer(std::string_view field) {
  Integer value{};
  const char* last = field.data() + field.size();
  const auto [end, error] = std::from_chars(field.data(), last, value);
  if (field.empty() || error != std::errc{} || end != last) {
    throw std::runtime_error("'" + std::string{field} + "' is not a whole number");
  }
  return value;
}

Row parse_row(std::string_view line) {
  std::vector<std::string_view> fields;
  std::size_t from = 0;
  for (;;) {
    const std::size_t tab = line.find('\t', from);
    if (tab == std::string_view::npos) {
      fields.push_back(line.substr(from));
      break;
    }
    fields.push_back(line.substr(from, tab - from));
    from = tab + 1;
  }
  if (fields.size() != 6) {
    throw std::runtime_error("expected 6 tab-separated columns, found " +
                             std::to_string(fields.size()));
  }
  Row row;
  row.seq = parse_integer<int>(fields[0]);
  row.frame = parse_integer<std::size_t>(fields[1]);
  if (fields[2] == "to_swf") {
    row.to_swf = true;
  } else if (fields[2] == "to_host") {
    row.to_swf = false;
  } else {
    throw std::runtime_error("direction must be to_swf or to_host");
  }
  if (fields[3].empty()) {
    throw std::runtime_error("method is empty");
  }
  row.method = std::string{fields[3]};
  row.args = typed_list(json::parse(std::string{fields[4]}));
  row.answer = typed_answer(json::parse(std::string{fields[5]}));
  return row;
}

struct Expectations {
  std::vector<Row> host;
  std::size_t next{0};
  std::size_t compared{0};
  bool random{false};
  std::optional<Difference> first;
};

std::vector<std::string> texts(const std::vector<avm2::Value>& values) {
  std::vector<std::string> out;
  for (const avm2::Value& value : values) {
    out.push_back(avm2::to_string(value));
  }
  return out;
}

std::string describe(std::string_view method, const std::vector<std::string>& arguments) {
  std::string text = "bgs." + std::string{method} + "(";
  for (std::size_t index = 0; index < arguments.size(); ++index) {
    if (index > 0) text += ", ";
    text += "\"" + arguments[index] + "\"";
  }
  return text + ")";
}

void keep_first(Expectations& expect, Difference difference) {
  if (!expect.first.has_value()) {
    expect.first = std::move(difference);
  }
}

void record(Expectations& expect, int seq, std::string reason) {
  if (expect.random) reason += kRandomNote;
  keep_first(expect, Difference{seq, std::move(reason)});
}

std::optional<std::string> observe(Expectations& expect, std::string_view method,
                                   const std::vector<std::string>& arguments) {
  if (method.substr(0, 4) != "bgs.") return std::nullopt;
  const std::string_view name = method.substr(4);
  ++expect.compared;
  if (expect.next >= expect.host.size()) {
    record(expect, expect.host.empty() ? 0 : expect.host.back().seq,
           "the SWF called " + describe(name, arguments) + " after the last captured call");
    return std::nullopt;
  }
  const Row& expected = expect.host[expect.next++];
  const std::vector<std::string> wanted = texts(expected.args);
  if (expected.method != name || wanted != arguments) {
    record(expect, expected.seq,
           "expected " + describe(expected.method, wanted) + " but the SWF called " +
               describe(name, arguments));
  }
  if (expected.answer.is_nullish()) return std::nullopt;
  return avm2::to_string(expected.answer);
}

void finish(Expectations& expect) {
  if (expect.next >= expect.host.size()) return;
  const Row& missing = expect.host[expect.next];
  record(expect, missing.seq,
         "expected " + describe(missing.method, texts(missing.args)) +
             " but the SWF made no further calls");
}

bool references_random(const fo4::swf::SwfFile& file) {
  for (const fo4::swf::SwfAbcBlock& block : fo4::swf::abc_blocks(file)) {
    abc::AbcFile parsed;
    if (abc::AbcFile::parse(reinterpret_cast<const std::uint8_t*>(block.payload.data()),
                            block.payload.size(), parsed) != abc::AbcStatus::Ok) {
      continue;
    }
    const std::vector<std::string>& strings = parsed.pool.strings;
    if (std::find(strings.begin(), strings.end(), "random") != strings.end()) return true;
  }
  return false;
}

}  // namespace

std::vector<Row> parse_capture(std::string_view text) {
  if (text.substr(0, 3) == "\xEF\xBB\xBF") {
    text.remove_prefix(3);
  }
  std::vector<Row> rows;
  std::size_t line_number = 0;
  std::size_t start = 0;
  while (start < text.size()) {
    std::size_t end = text.find('\n', start);
    if (end == std::string_view::npos) end = text.size();
    std::string_view line = text.substr(start, end - start);
    start = end + 1;
    ++line_number;
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    if (line.empty()) continue;
    if (line_number == 1 && line.substr(0, 4) == "seq\t") continue;
    try {
      rows.push_back(parse_row(line));
    } catch (const std::exception& error) {
      throw std::runtime_error("capture line " + std::to_string(line_number) + ": " +
                               error.what());
    }
  }
  std::sort(rows.begin(), rows.end(), [](const Row& left, const Row& right) {
    return left.seq < right.seq;
  });
  for (std::size_t index = 1; index < rows.size(); ++index) {
    if (rows[index].seq == rows[index - 1].seq) {
      throw std::runtime_error("duplicate seq " + std::to_string(rows[index].seq));
    }
  }
  return rows;
}

std::optional<std::string> read_capture_file(const std::filesystem::path& path) {
  std::ifstream in{path, std::ios::binary};
  if (!in) {
    return std::nullopt;
  }
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

Outcome replay_capture(avm2::Runtime& runtime, avm2::Interpreter& interpreter,
                       avm2::HostCallbacks callbacks, const fo4::swf::SwfFile& file,
                       const std::vector<Row>& rows) {
  Outcome outcome;
  auto expect = std::make_shared<Expectations>();
  expect->random = references_random(file);
  for (const Row& row : rows) {
    if (!row.to_swf) expect->host.push_back(row);
  }
  if (expect->host.empty()) {
    outcome.replay_error = "the capture has no to_host rows, so no SWF call was compared";
    return outcome;
  }
  callbacks.host_call = [expect](std::string_view method, std::vector<std::string> arguments) {
    return observe(*expect, method, arguments);
  };
  runtime.host().set_callbacks(std::move(callbacks));
  const avm2::RuntimeLoad linked = runtime.link();
  if (!linked.ok()) {
    outcome.replay_error = "the runtime did not link: " + linked.message;
    return outcome;
  }
  avm2::SwfRuntimeOptions options;
  options.bgs_host = true;
  const std::shared_ptr<avm2::SwfRuntime> movie =
      avm2::SwfRuntime::create(runtime, interpreter, file, options);
  if (movie == nullptr) {
    outcome.replay_error = "the movie could not be created: " + avm2::SwfRuntime::last_error();
    return outcome;
  }
  std::size_t frames = 0;
  for (const Row& row : rows) {
    frames = std::max(frames, row.frame + 1);
  }
  for (std::size_t frame = 0; frame < frames; ++frame) {
    for (const Row& row : rows) {
      if (!row.to_swf || row.frame != frame) continue;
      const avm2::ExecutionResult result = movie->call_root(interpreter, row.method, row.args);
      if (!result.ok()) {
        keep_first(*expect,
                   Difference{row.seq, row.method + " could not run in the SWF: " + result.message});
      }
    }
    const bool playing_before = movie->playing();
    movie->advance(interpreter, 1.0 / 24.0);
    if (playing_before && !movie->playing() && outcome.replay_error.empty()) {
      outcome.replay_error = "the movie stopped during replay frame " + std::to_string(frame) +
                             " on a frame script error: " + movie->frame_error();
    }
  }
  finish(*expect);
  outcome.compared = expect->compared;
  outcome.first = expect->first;
  return outcome;
}

}  // namespace bgs_capture
