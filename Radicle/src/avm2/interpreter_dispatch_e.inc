      case op::k_dxns:
        frame.default_xml_namespace = frame.file->string_at(string_operand(instruction));
        break;
      case op::k_dxnslate: {
        Value uri;
        pop_value(uri);
        if (status == InterpreterStatus::Ok) {
          frame.default_xml_namespace = to_string(uri);
        }
        break;
      }
      case op::k_pushnamespace:
        push_value(Value::string(frame.file->namespace_at(operand_at(instruction, 0))));
        break;
      case op::k_setpropertylate: {
        Multiname late_name;
        late_name.kind = MultinameKind::RuntimeName;
        late_name.namespaces.push_back(Namespace{});
        const Multiname* multiname = &late_name;
        status = resolve_runtime_multiname(frame, 1, multiname, message);
        if (status != InterpreterStatus::Ok) {
          break;
        }
        Value value;
        pop_value(value);
        Value object;
        if (status == InterpreterStatus::Ok) {
          pop_value(object);
        }
        if (status != InterpreterStatus::Ok) {
          break;
        }
        if (!object.is_object()) {
          if (limits_.lenient_builtins) {
            break;
          }
          return fail(InterpreterStatus::NullReference,
                      "cannot write " + property_text(*multiname) + " on " +
                          value_kind_name(object.kind()));
        }
        ExecutionResult nested =
            set_property(frame, *object.as_object(), *multiname, value, false, depth);
        if (!nested.ok()) {
          return nested;
        }
        break;
      }
      case op::k_deletepropertylate: {
        Multiname late_name;
        late_name.kind = MultinameKind::RuntimeName;
        late_name.namespaces.push_back(Namespace{});
        const Multiname* multiname = &late_name;
        status = resolve_runtime_multiname(frame, 0, multiname, message);
        if (status != InterpreterStatus::Ok) {
          break;
        }
        Value object;
        pop_value(object);
        if (status != InterpreterStatus::Ok) {
          break;
        }
        const bool deleted = object.is_object() && object.as_object()->delete_property(*multiname);
        push_value(Value::boolean(deleted));
        break;
      }
      case op::k_getglobalslot: {
        const std::uint32_t slot = count_operand(instruction);
        if (frame.scopes.empty()) {
          return fail(InterpreterStatus::ScopeUnderflow, "no global scope");
        }
        const std::vector<Value>& slots = frame.scopes.front()->slots();
        push_value(slot < slots.size() ? slots[slot] : Value::undefined());
        break;
      }
      case op::k_setglobalslot: {
        const std::uint32_t slot = count_operand(instruction);
        Value value;
        pop_value(value);
        if (status != InterpreterStatus::Ok) {
          break;
        }
        if (frame.scopes.empty()) {
          return fail(InterpreterStatus::ScopeUnderflow, "no global scope");
        }
        Object& global = *frame.scopes.front();
        if (slot >= global.slots().size()) {
          global.resize_slots(slot + 1);
        }
        global.slots()[slot] = value;
        break;
      }
      case op::k_esc_xelem:
      case op::k_esc_xattr: {
        Value value;
        pop_value(value);
        if (status == InterpreterStatus::Ok) {
          push_value(Value::string(
              escape_xml_markup(to_string(value), instruction.opcode == op::k_esc_xattr)));
        }
        break;
      }
      case op::k_checkfilter: {
        Value value;
        pop_value(value);
        if (status != InterpreterStatus::Ok) {
          break;
        }
        if (value.is_nullish()) {
          return fail(InterpreterStatus::TypeError,
                      "checkfilter needs a value that is not null or undefined");
        }
        push_value(value);
        break;
      }
