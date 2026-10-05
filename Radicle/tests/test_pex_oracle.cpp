#include "fo4/pex/pex.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>
#include <vector>

using fo4::pex::PexFile;
using fo4::pex::PexValue;

namespace {

std::filesystem::path fixtureDir() { return std::filesystem::path(FO4_TESTDATA_DIR) / "papyrus"; }

std::vector<std::filesystem::path> fixtures() {
  std::vector<std::filesystem::path> found;
  std::error_code error;
  for (std::filesystem::directory_iterator it(fixtureDir(), error), end; it != end; it.increment(error)) {
    if (error) break;
    if (it->is_regular_file(error) && it->path().extension() == ".psc") found.push_back(it->path());
  }
  std::sort(found.begin(), found.end());
  return found;
}

std::string quote(const std::filesystem::path& path) { return "\"" + path.string() + "\""; }

std::string readText(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) return std::string();
  return std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
}

std::vector<std::uint8_t> readBytes(const std::filesystem::path& path) {
  const std::string text = readText(path);
  return std::vector<std::uint8_t>(text.begin(), text.end());
}

int runIn(const std::filesystem::path& directory, const std::string& command) {
#ifdef _WIN32
  return std::system(("cd /d " + quote(directory) + " && " + command).c_str());
#else
  return std::system(("cd " + quote(directory) + " && " + command).c_str());
#endif
}

std::string optionalArgument(const char* variable, const char* flag) {
  const char* value = std::getenv(variable);
  if (value == nullptr || *value == '\0') return std::string();
  return " " + std::string(flag) + "=" + quote(std::filesystem::path(value));
}

/* The local Bethesda toolchain stays outside the repository: only its paths come from the
   environment, and everything it writes goes to a temporary directory. */
std::filesystem::path tool(const char* variable) {
  const char* value = std::getenv(variable);
  return value == nullptr ? std::filesystem::path() : std::filesystem::path(value);
}

class Workspace {
public:
  explicit Workspace(const std::string& name) {
    std::error_code error;
    root_ = std::filesystem::temp_directory_path() / "radicle_pex_oracle" / name;
    std::filesystem::remove_all(root_, error);
    std::filesystem::create_directories(root_, error);
  }

  ~Workspace() {
    std::error_code error;
    std::filesystem::remove_all(root_, error);
  }

  const std::filesystem::path& root() const { return root_; }

private:
  std::filesystem::path root_;
};

bool compile(const std::filesystem::path& compiler,
             const std::filesystem::path& source,
             const std::filesystem::path& output,
             std::string& log) {
  std::error_code error;
  std::filesystem::copy_file(source, output / source.filename(),
                             std::filesystem::copy_options::overwrite_existing, error);
  const std::string command = quote(compiler) + " " + quote(source.filename().string()) +
                              " -o=" + quote(output) + optionalArgument("FO4_PAPYRUS_IMPORTS", "-i") +
                              optionalArgument("FO4_PAPYRUS_FLAGS", "-f") + " -r -op > " +
                              quote(output / "compiler.log") + " 2>&1";
  const int code = runIn(output, command);
  log = readText(output / "compiler.log");
  return code == 0;
}

std::string describeValue(const PexValue& value) {
  switch (value.type) {
    case fo4::pex::PexValueType::None:
      return "none";
    case fo4::pex::PexValueType::Identifier:
      return "id:" + value.str;
    case fo4::pex::PexValueType::String:
      return "str:" + value.str;
    case fo4::pex::PexValueType::Integer:
      return "int:" + std::to_string(value.int_);
    case fo4::pex::PexValueType::Float:
      return "float:" + std::to_string(value.float_);
    case fo4::pex::PexValueType::Bool:
      return value.bool_ ? "bool:true" : "bool:false";
  }
  return "?";
}

/* The factual instruction stream: opcode, mnemonic and operand values, in order. */
std::vector<std::string> instructionFacts(const PexFile& pex) {
  std::vector<std::string> facts;
  for (const auto& object : pex.objects) {
    for (const auto& state : object.states) {
      for (const auto& function : state.functions) {
        for (const auto& instruction : function.instructions) {
          std::string fact = std::to_string(instruction.opCode) + ":" + instruction.mnemonic;
          for (const auto& argument : instruction.args) fact += "|" + describeValue(argument);
          facts.push_back(fact);
        }
      }
    }
  }
  return facts;
}

void report(const std::string& name, const PexFile& pex, std::size_t bytes) {
  std::size_t instructions = 0;
  for (const auto& object : pex.objects) {
    for (const auto& state : object.states) {
      for (const auto& function : state.functions) instructions += function.instructions.size();
    }
  }
  std::cout << "pex oracle: " << name << " bytes=" << bytes << " objects=" << pex.objects.size()
            << " strings=" << pex.stringTable.size() << " instructions=" << instructions << std::endl;
}

}

TEST(PexOracle, CompiledFixturesRoundTripByteIdentically) {
  const auto compiler = tool("FO4_PAPYRUS_COMPILER");
  if (compiler.empty()) {
    GTEST_SKIP() << "set FO4_PAPYRUS_COMPILER to a local PapyrusCompiler.exe to run the black-box oracle";
  }
  const auto sources = fixtures();
  ASSERT_FALSE(sources.empty()) << "no .psc fixtures under " << fixtureDir().string();

  for (const auto& source : sources) {
    const Workspace workspace(source.stem().string());
    std::string log;
    ASSERT_TRUE(compile(compiler, source, workspace.root(), log))
        << "compiler rejected " << source.filename().string() << ":\n" << log;

    const auto produced = workspace.root() / (source.stem().string() + ".pex");
    ASSERT_TRUE(std::filesystem::exists(produced)) << "no .pex for " << source.filename().string();

    const auto bytes = readBytes(produced);
    const PexFile pex = PexFile::fromBytes(bytes);
    EXPECT_EQ(pex.toBytes(), bytes) << "Radicle did not reproduce " << produced.filename().string();
    EXPECT_EQ(pex.gameId, 2);
    report(source.stem().string(), pex, bytes.size());
  }
}

TEST(PexOracle, AssemblerDisassemblyAgreesWithTheCompiler) {
  const auto compiler = tool("FO4_PAPYRUS_COMPILER");
  const auto assembler = tool("FO4_PAPYRUS_ASSEMBLER");
  if (compiler.empty() || assembler.empty()) {
    GTEST_SKIP() << "set FO4_PAPYRUS_COMPILER and FO4_PAPYRUS_ASSEMBLER to run the disassembly oracle";
  }
  const auto sources = fixtures();
  ASSERT_FALSE(sources.empty()) << "no .psc fixtures under " << fixtureDir().string();

  for (const auto& source : sources) {
    const Workspace workspace(source.stem().string());
    std::string log;
    ASSERT_TRUE(compile(compiler, source, workspace.root(), log))
        << "compiler rejected " << source.filename().string() << ":\n" << log;

    const auto name = source.stem().string();
    const auto compiled = workspace.root() / (name + ".pex");
    ASSERT_TRUE(std::filesystem::exists(compiled));
    const auto compiledBytes = readBytes(compiled);
    const PexFile compiledPex = PexFile::fromBytes(compiledBytes);

    const auto disassembly = workspace.root() / "disassembly";
    std::error_code error;
    std::filesystem::create_directories(disassembly, error);
    std::filesystem::copy_file(compiled, disassembly / (name + ".pex"),
                               std::filesystem::copy_options::overwrite_existing, error);
    ASSERT_EQ(runIn(disassembly, quote(assembler) + " " + name + " -D > disassemble.log 2>&1"), 0)
        << readText(disassembly / "disassemble.log");

    const auto generated = disassembly / (name + ".disassemble.pas");
    ASSERT_TRUE(std::filesystem::exists(generated)) << "no .pas produced for " << name;

    const auto reassembly = workspace.root() / "reassembly";
    std::filesystem::create_directories(reassembly, error);
    std::filesystem::copy_file(generated, reassembly / (name + ".pas"),
                               std::filesystem::copy_options::overwrite_existing, error);
    ASSERT_EQ(runIn(reassembly, quote(assembler) + " " + name + " > assemble.log 2>&1"), 0)
        << readText(reassembly / "assemble.log");

    const auto reassembled = reassembly / (name + ".pex");
    ASSERT_TRUE(std::filesystem::exists(reassembled));
    const auto reassembledBytes = readBytes(reassembled);
    const PexFile reassembledPex = PexFile::fromBytes(reassembledBytes);

    EXPECT_EQ(reassembledPex.toBytes(), reassembledBytes);
    EXPECT_EQ(instructionFacts(reassembledPex), instructionFacts(compiledPex))
        << "the compiler and the assembler disagree on " << name;
    EXPECT_EQ(reassembledPex.objects.size(), compiledPex.objects.size());
    EXPECT_EQ(reassembledPex.objects.front().name, compiledPex.objects.front().name);
  }
}
