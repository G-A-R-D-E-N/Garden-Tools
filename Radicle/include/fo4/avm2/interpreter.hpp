#pragma once

#include <fo4/abc/abc.hpp>
#include <fo4/abc/opcodes.hpp>
#include <fo4/avm2/class.hpp>
#include <fo4/avm2/object.hpp>
#include <fo4/avm2/runtime.hpp>
#include <fo4/avm2/value.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace fo4::avm2 {

enum class InterpreterStatus : std::uint8_t {
  Ok = 0,
  NotImplemented,
  NotLinked,
  InvalidArgument,
  StackUnderflow,
  StackOverflow,
  LocalOutOfRange,
  ScopeUnderflow,
  NullReference,
  NoSuchProperty,
  CannotCall,
  CannotConstruct,
  TypeError,
  UncaughtException,
  InstructionBudget,
  CallDepthExceeded,
  InstructionIndexOutOfRange,
  MethodIndexOutOfRange,
  ClassIndexOutOfRange,
};

const char* interpreter_status_name(InterpreterStatus status) noexcept;

struct InterpreterLimits {
  std::size_t max_instructions{1000000};
  std::size_t max_call_depth{128};
  std::size_t max_stack{65536};
  /* A probe may refuse an unbound builtin instead of stopping; the default must refuse. */
  bool lenient_builtins{false};
};

/* Invariant: a failing result names the opcode and offset that refused when one was executing. */
struct ExecutionResult {
  InterpreterStatus status{InterpreterStatus::Ok};
  Value value;
  std::string message;
  std::size_t instructions{0};
  std::size_t frames{0};
  std::uint8_t opcode{0};
  std::uint32_t offset{0};

  bool ok() const noexcept { return status == InterpreterStatus::Ok; }
};

/* The interpreter must stay single threaded and clock free, so equal inputs give equal results. */
class Interpreter {
 public:
  explicit Interpreter(Runtime& runtime, InterpreterLimits limits = {});

  Interpreter(const Interpreter&) = delete;
  Interpreter& operator=(const Interpreter&) = delete;

  /* Requires a linked runtime; runs one script initializer from Runtime::scripts by index. */
  ExecutionResult run_script(std::size_t script_index);

  /* Requires a linked runtime and a function with a body. */
  ExecutionResult run_function(const FunctionObject& function, Value this_value,
                               std::vector<Value> arguments);

  const InterpreterLimits& limits() const noexcept { return limits_; }

  Runtime& runtime() noexcept { return runtime_; }
  const Runtime& runtime() const noexcept { return runtime_; }

  /* Builtin natives must share this surface; depth is the caller's call depth. */
  ExecutionResult call_function(const FunctionObject& function, Value this_value,
                                std::vector<Value> arguments, std::size_t depth);
  ExecutionResult read_property(Object& object, const Multiname& multiname, std::size_t depth);
  ExecutionResult write_property(Object& object, const Multiname& multiname, const Value& value,
                                 std::size_t depth);
  std::shared_ptr<Object> make_instance(Class& klass);
  std::shared_ptr<ArrayObject> make_array(std::vector<Value> elements);
  Value box(const Value& value);
  static Value unbox(const Value& value);
  Value global_this();
  /* Requires a linked runtime; a plain object gets the Object builtin class so its methods resolve. */
  void prepare_plain_object(Object& object);
  /* The random source must depend only on this interpreter's seed. */
  double next_random();
  ExecutionResult call_native_id(std::uint32_t native_id, const Value& self,
                                 const std::vector<Value>& arguments, std::size_t depth);

 private:
  struct Frame {
    const abc::AbcFile* file{nullptr};
    const abc::AbcMethodBody* body{nullptr};
    std::uint32_t method_index{0};
    std::vector<abc::AbcInstruction> instructions;
    std::vector<std::uint32_t> instruction_offsets;
    std::vector<Value> locals;
    std::vector<Value> stack;
    std::vector<Object*> scopes;
    Value this_value;
    Class* owner_class{nullptr};
    std::size_t entry_scope_count{0};
    std::size_t ip{0};
    std::map<const Object*, std::vector<Key>> iterator_keys;
  };

  ExecutionResult execute_frame(Frame& frame, std::size_t depth);
  ExecutionResult invoke(const FunctionObject& function, Value this_value,
                         std::vector<Value> arguments, const std::vector<Object*>& scopes,
                         std::size_t depth);
  ExecutionResult fail(InterpreterStatus status, std::string message) const;

  const Multiname* resolve_multiname(const abc::AbcFile& file, std::uint32_t index);
  const Class* class_at(const abc::AbcFile& file, std::uint32_t instance_index);

  InterpreterStatus push(Frame& frame, Value value, std::string& message);
  InterpreterStatus pop(Frame& frame, Value& value, std::string& message);
  InterpreterStatus pop_arguments(Frame& frame, std::size_t count, std::vector<Value>& out,
                                  std::string& message);
  InterpreterStatus jump_to_offset(Frame& frame, std::int32_t offset, std::string& message);
  std::uint32_t instruction_index_at(const Frame& frame, std::int32_t offset) const;

  ExecutionResult get_property(Frame& frame, Object& object, const Multiname& multiname,
                               std::size_t depth);
  ExecutionResult set_property(Frame& frame, Object& object, const Multiname& multiname,
                               const Value& value, bool define, std::size_t depth);
  InterpreterStatus find_scope_binding(Frame& frame, const Multiname& multiname,
                                       bool create_on_global, Object*& owner, bool& found,
                                       std::string& message);
  ExecutionResult call_property(Frame& frame, Object& receiver, const Multiname& multiname,
                                const std::vector<Value>& arguments, std::size_t depth);
  ExecutionResult construct(Frame& frame, const ClassObject& class_object,
                            const std::vector<Value>& arguments, std::size_t depth);
  ExecutionResult ensure_class_initialized(Class& klass, std::size_t depth);
  ExecutionResult initialize_class_value(const Value& value, std::size_t depth);
  bool is_instance_of(const Value& value, const Value& type) const;
  InterpreterStatus coerce_value(const Multiname& multiname, Value& value,
                                 std::string& message);
  InterpreterStatus array_property(Object& object, const Multiname& multiname, bool write,
                                   Value& value, bool& handled, std::string& message);

  Runtime& runtime_;
  InterpreterLimits limits_;
  std::size_t instructions_{0};
  std::size_t frames_{0};
  std::uint64_t random_state_{0x9E3779B97F4A7C15ULL};
  std::uint8_t current_opcode_{0};
  std::uint32_t current_offset_{0};
  std::map<std::pair<const abc::AbcFile*, std::uint32_t>, Multiname> multinames_;
};

}  // namespace fo4::avm2
