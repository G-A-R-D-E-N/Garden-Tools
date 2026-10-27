#pragma once

#include <fo4/avm2/interpreter.hpp>
#include <fo4/avm2/name.hpp>
#include <fo4/avm2/value.hpp>

#include "native_ids.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::avm2 {

/* Each builtin family must report whether it handled the call. */
bool try_object_native(Interpreter& interpreter, native::Id id, const Value& self,
                       const std::vector<Value>& arguments, std::size_t depth,
                       ExecutionResult& out);
bool try_function_native(Interpreter& interpreter, native::Id id, const Value& self,
                         const std::vector<Value>& arguments, std::size_t depth,
                         ExecutionResult& out);
bool try_primitive_native(Interpreter& interpreter, native::Id id, const Value& self,
                          const std::vector<Value>& arguments, std::size_t depth,
                          ExecutionResult& out);
bool try_math_native(Interpreter& interpreter, native::Id id, const Value& self,
                     const std::vector<Value>& arguments, std::size_t depth,
                     ExecutionResult& out);
bool try_top_native(Interpreter& interpreter, native::Id id, const Value& self,
                    const std::vector<Value>& arguments, std::size_t depth,
                    ExecutionResult& out);
bool try_string_native(Interpreter& interpreter, native::Id id, const Value& self,
                       const std::vector<Value>& arguments, std::size_t depth,
                       ExecutionResult& out);
bool try_array_native(Interpreter& interpreter, native::Id id, const Value& self,
                      const std::vector<Value>& arguments, std::size_t depth,
                      ExecutionResult& out);
bool try_error_native(Interpreter& interpreter, native::Id id, const Value& self,
                      const std::vector<Value>& arguments, std::size_t depth,
                      ExecutionResult& out);
bool try_display_native(Interpreter& interpreter, native::Id id, const Value& self,
                        const std::vector<Value>& arguments, std::size_t depth,
                        ExecutionResult& out);
bool try_event_native(Interpreter& interpreter, native::Id id, const Value& self,
                      const std::vector<Value>& arguments, std::size_t depth,
                      ExecutionResult& out);
bool try_text_native(Interpreter& interpreter, native::Id id, const Value& self,
                     const std::vector<Value>& arguments, std::size_t depth,
                     ExecutionResult& out);

/* Requires a named reachable property of the receiver; the name is a public package name. */
Multiname builtin_name(std::string_view name);

/* Requires a wrapper or a primitive; returns the primitive payload. */
Value native_self(const Value& self);

/* Requires a readable argument list; a missing argument is undefined. */
Value argument_at(const std::vector<Value>& arguments, std::size_t index);

/* A top-level conversion must commit its primitive result through this. */
ExecutionResult native_value(Value value);

/* Requires a linked runtime; builds an Array instance carrying the given elements. */
std::shared_ptr<ArrayObject> make_array_value(Interpreter& interpreter,
                                              std::vector<Value> elements);

}  // namespace fo4::avm2
