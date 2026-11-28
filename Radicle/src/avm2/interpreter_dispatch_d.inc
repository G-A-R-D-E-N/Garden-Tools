      case op::k_li8:
      case op::k_li16:
      case op::k_li32:
      case op::k_lf32:
      case op::k_lf64: {
        Value address;
        pop_value(address);
        if (status != InterpreterStatus::Ok) {
          break;
        }
        Value value;
        status = domain_memory_load(instruction.opcode, address, value, message);
        if (status == InterpreterStatus::Ok) {
          push_value(std::move(value));
        }
        break;
      }
      case op::k_si8:
      case op::k_si16:
      case op::k_si32:
      case op::k_sf32:
      case op::k_sf64: {
        Value address;
        pop_value(address);
        Value value;
        if (status == InterpreterStatus::Ok) {
          pop_value(value);
        }
        if (status != InterpreterStatus::Ok) {
          break;
        }
        status = domain_memory_store(instruction.opcode, address, value, message);
        break;
      }
