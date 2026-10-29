      case op::k_hasnext: {
        Value index;
        pop_value(index);
        Value object;
        if (status == InterpreterStatus::Ok) {
          pop_value(object);
        }
        if (status != InterpreterStatus::Ok) {
          break;
        }
        if (!object.is_object()) {
          push_value(Value::uint32(0u));
          break;
        }
        const std::vector<Key>& keys = enumeration_keys(frame, *object.as_object());
        const std::uint32_t cursor = to_uint32(index);
        push_value(Value::uint32(cursor < keys.size() ? cursor + 1 : 0u));
        break;
      }
      case op::k_sxi1:
      case op::k_sxi8:
      case op::k_sxi16: {
        Value value;
        pop_value(value);
        if (status != InterpreterStatus::Ok) {
          break;
        }
        const std::uint32_t bits = static_cast<std::uint32_t>(to_int32(value));
        std::int32_t extended = 0;
        switch (instruction.opcode) {
          case op::k_sxi1:
            extended = (bits & 1u) != 0 ? -1 : 0;
            break;
          case op::k_sxi8:
            extended = static_cast<std::int8_t>(bits & 0xFFu);
            break;
          default:
            extended = static_cast<std::int16_t>(bits & 0xFFFFu);
            break;
        }
        push_value(Value::int32(extended));
        break;
      }
      case op::k_coerce_o: {
        Value value;
        pop_value(value);
        if (status == InterpreterStatus::Ok) {
          push_value(value.is_undefined() ? Value::null() : value);
        }
        break;
      }
