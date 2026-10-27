      case op::k_bkpt:
      case op::k_nop:
      case op::k_label:
      case op::k_debug:
      case op::k_debugline:
      case op::k_debugfile:
      case op::k_bkptline:
      case op::k_timestamp:
        break;
      case op::k_pushnull:
        push_value(Value::null());
        break;
      case op::k_pushundefined:
        push_value(Value::undefined());
        break;
      case op::k_pushtrue:
        push_value(Value::boolean(true));
        break;
      case op::k_pushfalse:
        push_value(Value::boolean(false));
        break;
      case op::k_pushnan:
        push_value(Value::number(std::numeric_limits<double>::quiet_NaN()));
        break;
      case op::k_pushbyte:
      case op::k_pushshort:
        push_value(Value::int32(immediate_at(instruction, 0)));
        break;
      case op::k_pushstring:
        push_value(Value::string(frame.file->string_at(string_operand(instruction))));
        break;
      case op::k_pushint: {
        const std::uint32_t index = int_operand(instruction);
        if (index >= frame.file->pool.ints.size()) {
          return fail(InterpreterStatus::TypeError, "int pool index out of range");
        }
        push_value(Value::int32(frame.file->pool.ints[index]));
        break;
      }
      case op::k_pushuint: {
        const std::uint32_t index = uint_operand(instruction);
        if (index >= frame.file->pool.uints.size()) {
          return fail(InterpreterStatus::TypeError, "uint pool index out of range");
        }
        push_value(Value::uint32(frame.file->pool.uints[index]));
        break;
      }
      case op::k_pushdouble: {
        const std::uint32_t index = double_operand(instruction);
        if (index >= frame.file->pool.doubles.size()) {
          return fail(InterpreterStatus::TypeError, "double pool index out of range");
        }
        push_value(Value::number(frame.file->pool.doubles[index]));
        break;
      }
      case op::k_pop: {
        Value value;
        pop_value(value);
        break;
      }
      case op::k_dup: {
        Value value;
        pop_value(value);
        if (status == InterpreterStatus::Ok) {
          push_value(value);
        }
        if (status == InterpreterStatus::Ok) {
          push_value(value);
        }
        break;
      }
      case op::k_swap: {
        Value top;
        pop_value(top);
        Value below;
        if (status == InterpreterStatus::Ok) {
          pop_value(below);
        }
        if (status == InterpreterStatus::Ok) {
          push_value(top);
        }
        if (status == InterpreterStatus::Ok) {
          push_value(below);
        }
        break;
      }
      case op::k_getlocal0:
      case op::k_getlocal1:
      case op::k_getlocal2:
      case op::k_getlocal3: {
        const std::size_t index = instruction.opcode - op::k_getlocal0;
        if (index >= frame.locals.size()) {
          return fail(InterpreterStatus::LocalOutOfRange, "local " + std::to_string(index) +
                                                              " is out of range");
        }
        push_value(frame.locals[index]);
        break;
      }
      case op::k_setlocal0:
      case op::k_setlocal1:
      case op::k_setlocal2:
      case op::k_setlocal3: {
        const std::size_t index = instruction.opcode - op::k_setlocal0;
        Value value;
        pop_value(value);
        if (status == InterpreterStatus::Ok) {
          if (index >= frame.locals.size()) {
            return fail(InterpreterStatus::LocalOutOfRange, "local " + std::to_string(index) +
                                                                " is out of range");
          }
          frame.locals[index] = std::move(value);
        }
        break;
      }
      case op::k_getlocal: {
        const std::uint32_t index = count_operand(instruction);
        if (index >= frame.locals.size()) {
          return fail(InterpreterStatus::LocalOutOfRange,
                      "local " + std::to_string(index) + " is out of range");
        }
        push_value(frame.locals[index]);
        break;
      }
      case op::k_setlocal: {
        const std::uint32_t index = count_operand(instruction);
        Value value;
        pop_value(value);
        if (status == InterpreterStatus::Ok) {
          if (index >= frame.locals.size()) {
            return fail(InterpreterStatus::LocalOutOfRange,
                        "local " + std::to_string(index) + " is out of range");
          }
          frame.locals[index] = std::move(value);
        }
        break;
      }
      case op::k_kill: {
        const std::uint32_t index = count_operand(instruction);
        if (index < frame.locals.size()) {
          frame.locals[index] = Value::undefined();
        }
        break;
      }
      case op::k_inclocal_i:
      case op::k_declocal_i: {
        const std::uint32_t index = count_operand(instruction);
        if (index >= frame.locals.size()) {
          return fail(InterpreterStatus::LocalOutOfRange,
                      "local " + std::to_string(index) + " is out of range");
        }
        frame.locals[index] =
            Value::int32(to_int32(frame.locals[index]) + (instruction.opcode == op::k_inclocal_i
                                                              ? 1
                                                              : -1));
        break;
      }
      case op::k_getglobalscope:
        if (frame.scopes.empty()) {
          return fail(InterpreterStatus::ScopeUnderflow, "no global scope");
        }
        push_value(borrowed_object(*frame.scopes.front()));
        break;
      case op::k_getscopeobject: {
        const std::uint32_t index = count_operand(instruction);
        if (index >= frame.scopes.size()) {
          return fail(InterpreterStatus::ScopeUnderflow, "scope " + std::to_string(index) +
                                                             " is out of range");
        }
        push_value(borrowed_object(*frame.scopes[index]));
        break;
      }
      case op::k_pushscope:
      case op::k_pushwith: {
        Value value;
        pop_value(value);
        if (status == InterpreterStatus::Ok) {
          if (!value.is_object()) {
            return fail(InterpreterStatus::TypeError, "a scope must be an object");
          }
          if (frame.scopes.size() + frame.body->init_scope_depth >=
              frame.body->max_scope_depth + frame.entry_scope_count) {
            return fail(InterpreterStatus::ScopeDepthExceeded,
                        "scope depth would exceed max_scope_depth " +
                            std::to_string(frame.body->max_scope_depth));
          }
          frame.scopes.push_back(value.as_object().get());
        }
        break;
      }
      case op::k_popscope:
        if (frame.scopes.size() <= 1) {
          return fail(InterpreterStatus::ScopeUnderflow, "cannot pop the global scope");
        }
        frame.scopes.pop_back();
        break;
      case op::k_newactivation: {
        auto activation = std::make_shared<Object>();
        prepare_plain_object(*activation);
        push_value(Value::object(std::move(activation)));
        break;
      }
      case op::k_jump:
        status = jump_to_offset(frame, offset_operand(instruction), message);
        break;
      case op::k_iftrue:
      case op::k_iffalse:
      case op::k_ifeq:
      case op::k_ifne:
      case op::k_iflt:
      case op::k_ifle:
      case op::k_ifgt:
      case op::k_ifge:
      case op::k_ifstricteq:
      case op::k_ifstrictne:
      case op::k_ifnlt:
      case op::k_ifnle:
      case op::k_ifngt:
      case op::k_ifnge: {
        Value right;
        pop_value(right);
        if (status != InterpreterStatus::Ok) {
          break;
        }
        Value left = right;
        bool both = false;
        switch (instruction.opcode) {
          case op::k_ifeq:
          case op::k_ifne:
          case op::k_iflt:
          case op::k_ifle:
          case op::k_ifgt:
          case op::k_ifge:
          case op::k_ifstricteq:
          case op::k_ifstrictne:
          case op::k_ifnlt:
          case op::k_ifnle:
          case op::k_ifngt:
          case op::k_ifnge:
            both = true;
            break;
          default:
            break;
        }
        if (both) {
          Value second;
          pop_value(second);
          if (status != InterpreterStatus::Ok) {
            break;
          }
          left = std::move(second);
        }
        bool take = false;
        switch (instruction.opcode) {
          case op::k_iftrue:
            take = to_boolean(left);
            break;
          case op::k_iffalse:
            take = !to_boolean(left);
            break;
          case op::k_ifeq:
            take = loose_equals(left, right);
            break;
          case op::k_ifne:
            take = !loose_equals(left, right);
            break;
          case op::k_iflt:
            take = less_than(left, right);
            break;
          case op::k_ifle:
            take = less_equals(left, right);
            break;
          case op::k_ifgt:
            take = greater_than(left, right);
            break;
          case op::k_ifge:
            take = greater_equals(left, right);
            break;
          case op::k_ifstricteq:
            take = strict_equals(left, right);
            break;
          case op::k_ifstrictne:
            take = !strict_equals(left, right);
            break;
          case op::k_ifnlt:
            take = !less_than(left, right);
            break;
          case op::k_ifnle:
            take = !less_equals(left, right);
            break;
          case op::k_ifngt:
            take = !greater_than(left, right);
            break;
          case op::k_ifnge:
            take = !greater_equals(left, right);
            break;
          default:
            break;
        }
        if (take) {
          status = jump_to_offset(frame, offset_operand(instruction), message);
        }
        break;
      }
      case op::k_lookupswitch: {
        const abc::AbcDecodedOperand* operand = switch_operand(instruction);
        Value value;
        pop_value(value);
        if (status != InterpreterStatus::Ok || operand == nullptr) {
          if (operand == nullptr) {
            return fail(InterpreterStatus::TypeError, "lookupswitch has no switch operand");
          }
          break;
        }
        const std::int32_t index = to_int32(value);
        std::int32_t target = operand->target;
        if (index >= 0 && static_cast<std::size_t>(index) < operand->case_targets.size()) {
          target = operand->case_targets[static_cast<std::size_t>(index)];
        }
        status = jump_to_offset(frame, target, message);
        break;
      }
      case op::k_add: {
        Value right;
        pop_value(right);
        Value left;
        if (status == InterpreterStatus::Ok) {
          pop_value(left);
        }
        if (status == InterpreterStatus::Ok) {
          push_value(add_values(left, right));
        }
        break;
      }
      case op::k_subtract: {
        Value right;
        pop_value(right);
        Value left;
        if (status == InterpreterStatus::Ok) {
          pop_value(left);
        }
        if (status == InterpreterStatus::Ok) {
          push_value(subtract_values(left, right));
        }
        break;
      }
      case op::k_multiply: {
        Value right;
        pop_value(right);
        Value left;
        if (status == InterpreterStatus::Ok) {
          pop_value(left);
        }
        if (status == InterpreterStatus::Ok) {
          push_value(multiply_values(left, right));
        }
        break;
      }
      case op::k_divide: {
        Value right;
        pop_value(right);
        Value left;
        if (status == InterpreterStatus::Ok) {
          pop_value(left);
        }
        if (status == InterpreterStatus::Ok) {
          push_value(divide_values(left, right));
        }
        break;
      }
      case op::k_modulo: {
        Value right;
        pop_value(right);
        Value left;
        if (status == InterpreterStatus::Ok) {
          pop_value(left);
        }
        if (status == InterpreterStatus::Ok) {
          push_value(modulo_values(left, right));
        }
        break;
      }
      case op::k_negate: {
        Value value;
        pop_value(value);
        if (status == InterpreterStatus::Ok) {
          push_value(negate_value(value));
        }
        break;
      }
      case op::k_increment:
      case op::k_decrement: {
        Value value;
        pop_value(value);
        if (status == InterpreterStatus::Ok) {
          push_value(instruction.opcode == op::k_increment ? increment_value(value)
                                                           : decrement_value(value));
        }
        break;
      }
      case op::k_increment_i:
      case op::k_decrement_i: {
        Value value;
        pop_value(value);
        if (status == InterpreterStatus::Ok) {
          const std::int32_t delta = instruction.opcode == op::k_increment_i ? 1 : -1;
          push_value(Value::int32(static_cast<std::int32_t>(
              static_cast<std::uint32_t>(to_int32(value)) + static_cast<std::uint32_t>(delta))));
        }
        break;
      }
      case op::k_add_i:
      case op::k_subtract_i:
      case op::k_multiply_i: {
        Value right;
        pop_value(right);
        Value left;
        if (status == InterpreterStatus::Ok) {
          pop_value(left);
        }
        if (status == InterpreterStatus::Ok) {
          const std::uint32_t l = static_cast<std::uint32_t>(to_int32(left));
          const std::uint32_t r = static_cast<std::uint32_t>(to_int32(right));
          std::uint32_t computed = 0;
          if (instruction.opcode == op::k_add_i) {
            computed = l + r;
          } else if (instruction.opcode == op::k_subtract_i) {
            computed = l - r;
          } else {
            computed = l * r;
          }
          push_value(Value::int32(static_cast<std::int32_t>(computed)));
        }
        break;
      }
      case op::k_bitand:
      case op::k_bitor:
      case op::k_bitxor:
      case op::k_lshift:
      case op::k_rshift:
      case op::k_urshift: {
        Value right;
        pop_value(right);
        Value left;
        if (status == InterpreterStatus::Ok) {
          pop_value(left);
        }
        if (status == InterpreterStatus::Ok) {
          switch (instruction.opcode) {
            case op::k_bitand:
              push_value(bitwise_and(left, right));
              break;
            case op::k_bitor:
              push_value(bitwise_or(left, right));
              break;
            case op::k_bitxor:
              push_value(bitwise_xor(left, right));
              break;
            case op::k_lshift:
              push_value(shift_left(left, right));
              break;
            case op::k_rshift:
              push_value(shift_right(left, right));
              break;
            default:
              push_value(shift_unsigned(left, right));
              break;
          }
        }
        break;
      }
      case op::k_bitnot: {
        Value value;
        pop_value(value);
        if (status == InterpreterStatus::Ok) {
          push_value(Value::int32(~to_int32(value)));
        }
        break;
      }
      case op::k_not: {
        Value value;
        pop_value(value);
        if (status == InterpreterStatus::Ok) {
          push_value(Value::boolean(!to_boolean(value)));
        }
        break;
      }
      case op::k_typeof: {
        Value value;
        pop_value(value);
        if (status == InterpreterStatus::Ok) {
          push_value(Value::string(typeof_name(value)));
        }
        break;
      }
      case op::k_equals:
      case op::k_strictequals: {
        Value right;
        pop_value(right);
        Value left;
        if (status == InterpreterStatus::Ok) {
          pop_value(left);
        }
        if (status == InterpreterStatus::Ok) {
          const bool equal = instruction.opcode == op::k_equals ? loose_equals(left, right)
                                                                : strict_equals(left, right);
          push_value(Value::boolean(equal));
        }
        break;
      }
      case op::k_lessthan:
      case op::k_lessequals:
      case op::k_greaterthan:
      case op::k_greaterequals: {
        Value right;
        pop_value(right);
        Value left;
        if (status == InterpreterStatus::Ok) {
          pop_value(left);
        }
        if (status == InterpreterStatus::Ok) {
          bool outcome = false;
          switch (instruction.opcode) {
            case op::k_lessthan:
              outcome = less_than(left, right);
              break;
            case op::k_lessequals:
              outcome = less_equals(left, right);
              break;
            case op::k_greaterthan:
              outcome = greater_than(left, right);
              break;
            default:
              outcome = greater_equals(left, right);
              break;
          }
          push_value(Value::boolean(outcome));
        }
        break;
      }
      case op::k_convert_i:
      case op::k_convert_u:
      case op::k_convert_d:
      case op::k_convert_b:
      case op::k_convert_s:
      case op::k_convert_o: {
        Value value;
        pop_value(value);
        if (status != InterpreterStatus::Ok) {
          break;
        }
        switch (instruction.opcode) {
          case op::k_convert_i:
            push_value(Value::int32(to_int32(value)));
            break;
          case op::k_convert_u:
            push_value(Value::uint32(to_uint32(value)));
            break;
          case op::k_convert_d:
            push_value(Value::number(to_number(value)));
            break;
          case op::k_convert_b:
            push_value(Value::boolean(to_boolean(value)));
            break;
          case op::k_convert_s:
            push_value(Value::string(to_string(value)));
            break;
          default:
            push_value(value);
            break;
        }
        break;
      }
      case op::k_coerce_a:
        break;
      case op::k_coerce:
      case op::k_coerce_s: {
        Value value;
        pop_value(value);
        if (status != InterpreterStatus::Ok) {
          break;
        }
        if (instruction.opcode == op::k_coerce_s) {
          push_value(Value::string(to_string(value)));
          break;
        }
        const Multiname* multiname = resolve_multiname(*frame.file, multiname_operand(instruction));
        if (multiname == nullptr) {
          return fail(InterpreterStatus::TypeError, "coerce has an invalid multiname");
        }
        status = coerce_value(*multiname, value, message);
        if (status == InterpreterStatus::Ok) {
          push_value(value);
        }
        break;
      }
      case op::k_astype:
      case op::k_istype: {
        Value value;
        pop_value(value);
        if (status != InterpreterStatus::Ok) {
          break;
        }
        const Multiname* multiname = resolve_multiname(*frame.file, multiname_operand(instruction));
        if (multiname == nullptr) {
          return fail(InterpreterStatus::TypeError, "type test has an invalid multiname");
        }
        Value type;
        const Class* target = runtime_.find_class(*multiname);
        if (target != nullptr) {
          type = borrowed_object(*target->class_object);
        }
        bool matches = false;
        if (!target) {
          const std::string name = property_text(*multiname);
          if (name == "Object" || name == "*") {
            matches = true;
          } else if (name == "int") {
            matches = value.is_int32();
          } else if (name == "uint") {
            matches = value.is_uint32();
          } else if (name == "Number" || name == "double") {
            matches = value.is_number_like();
          } else if (name == "Boolean") {
            matches = value.is_boolean();
          } else if (name == "String") {
            matches = value.is_string();
          }
        } else {
          matches = is_instance_of(value, type);
        }
        if (instruction.opcode == op::k_istype) {
          push_value(Value::boolean(matches));
        } else if (matches) {
          push_value(value);
        } else {
          push_value(Value::null());
        }
        break;
      }
      case op::k_astypelate:
      case op::k_istypelate: {
        Value type;
        pop_value(type);
        Value value;
        if (status == InterpreterStatus::Ok) {
          pop_value(value);
        }
        if (status == InterpreterStatus::Ok) {
          const bool matches = is_instance_of(value, type);
          if (instruction.opcode == op::k_istypelate) {
            push_value(Value::boolean(matches));
          } else {
            push_value(matches ? value : Value::null());
          }
        }
        break;
      }
