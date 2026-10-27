      case op::k_instanceof: {
        Value type;
        pop_value(type);
        Value value;
        if (status == InterpreterStatus::Ok) {
          pop_value(value);
        }
        if (status == InterpreterStatus::Ok) {
          push_value(Value::boolean(is_instance_of(value, type)));
        }
        break;
      }
      case op::k_in: {
        Value object;
        pop_value(object);
        Value name;
        if (status == InterpreterStatus::Ok) {
          pop_value(name);
        }
        if (status != InterpreterStatus::Ok) {
          break;
        }
        bool present = false;
        if (object.is_object()) {
          Multiname multiname;
          multiname.kind = MultinameKind::QName;
          multiname.name = to_string(name);
          multiname.namespaces.push_back(Namespace{});
          present = object.as_object()->has_property(multiname);
        }
        push_value(Value::boolean(present));
        break;
      }
      case op::k_getproperty: {
        const Multiname* multiname = resolve_multiname(*frame.file, multiname_operand(instruction));
        if (multiname == nullptr) {
          return fail(InterpreterStatus::TypeError, "getproperty has an invalid multiname");
        }
        Value object;
        pop_value(object);
        if (status != InterpreterStatus::Ok) {
          break;
        }
        if (!object.is_object()) {
          if (limits_.lenient_builtins) {
            push_value(Value::undefined());
            break;
          }
          return fail(InterpreterStatus::NullReference,
                      "cannot read " + property_text(*multiname) + " from " +
                          value_kind_name(object.kind()));
        }
        ExecutionResult nested = get_property(frame, *object.as_object(), *multiname, depth);
        if (!nested.ok()) {
          return nested;
        }
        ExecutionResult initialized = initialize_class_value(nested.value, depth);
        if (!initialized.ok()) {
          return initialized;
        }
        push_value(nested.value);
        break;
      }
      case op::k_setproperty:
      case op::k_initproperty: {
        const Multiname* multiname = resolve_multiname(*frame.file, multiname_operand(instruction));
        if (multiname == nullptr) {
          return fail(InterpreterStatus::TypeError, "setproperty has an invalid multiname");
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
            set_property(frame, *object.as_object(), *multiname, value,
                         instruction.opcode == op::k_initproperty, depth);
        if (!nested.ok()) {
          return nested;
        }
        break;
      }
      case op::k_getlex: {
        const Multiname* multiname = resolve_multiname(*frame.file, multiname_operand(instruction));
        if (multiname == nullptr) {
          return fail(InterpreterStatus::TypeError, "getlex has an invalid multiname");
        }
        Object* owner = nullptr;
        bool found = false;
        status = find_scope_binding(frame, *multiname, false, owner, found, message);
        if (status != InterpreterStatus::Ok) {
          break;
        }
        if (!found) {
          push_value(Value::undefined());
          break;
        }
        ExecutionResult nested = get_property(frame, *owner, *multiname, depth);
        if (!nested.ok()) {
          return nested;
        }
        ExecutionResult initialized = initialize_class_value(nested.value, depth);
        if (!initialized.ok()) {
          return initialized;
        }
        push_value(nested.value);
        break;
      }
      case op::k_findpropstrict:
      case op::k_findproperty: {
        const Multiname* multiname = resolve_multiname(*frame.file, multiname_operand(instruction));
        if (multiname == nullptr) {
          return fail(InterpreterStatus::TypeError, "findproperty has an invalid multiname");
        }
        Object* owner = nullptr;
        bool found = false;
        status = find_scope_binding(frame, *multiname,
                                    instruction.opcode == op::k_findpropstrict, owner, found,
                                    message);
        if (status != InterpreterStatus::Ok) {
          break;
        }
        if (found) {
          push_value(borrowed_object(*owner));
        } else {
          push_value(Value::null());
        }
        break;
      }
      case op::k_getslot: {
        const std::uint32_t slot = count_operand(instruction);
        Value object;
        pop_value(object);
        if (status != InterpreterStatus::Ok) {
          break;
        }
        if (!object.is_object()) {
          return fail(InterpreterStatus::NullReference, "getslot needs an object");
        }
        Object& target = *object.as_object();
        const std::uint32_t index = slot;
        if (index >= target.slots().size()) {
          push_value(Value::undefined());
        } else {
          push_value(target.slots()[index]);
        }
        break;
      }
      case op::k_setslot: {
        const std::uint32_t slot = count_operand(instruction);
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
          return fail(InterpreterStatus::NullReference, "setslot needs an object");
        }
        Object& target = *object.as_object();
        if (slot >= target.slots().size()) {
          target.resize_slots(slot + 1);
        }
        target.slots()[slot] = value;
        break;
      }
      case op::k_deleteproperty: {
        const Multiname* multiname = resolve_multiname(*frame.file, multiname_operand(instruction));
        if (multiname == nullptr) {
          return fail(InterpreterStatus::TypeError, "deleteproperty has an invalid multiname");
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
      case op::k_getsuper:
      case op::k_setsuper: {
        const Multiname* multiname = resolve_multiname(*frame.file, multiname_operand(instruction));
        if (multiname == nullptr) {
          return fail(InterpreterStatus::TypeError, "super access has an invalid multiname");
        }
        if (frame.owner_class == nullptr || frame.owner_class->super == nullptr) {
          return fail(InterpreterStatus::TypeError, "super access outside a class hierarchy");
        }
        if (instruction.opcode == op::k_setsuper) {
          Value value;
          pop_value(value);
          Value object;
          if (status == InterpreterStatus::Ok) {
            pop_value(object);
          }
          if (status != InterpreterStatus::Ok || !object.is_object()) {
            break;
          }
          const Trait* trait =
              frame.owner_class->super->resolve_instance_trait(*multiname);
          if (trait == nullptr || !trait->is_slot_like()) {
            return fail(InterpreterStatus::NoSuchProperty,
                        "super has no slot " + property_text(*multiname));
          }
          Object& target = *object.as_object();
          if (trait->slot_id >= target.slots().size()) {
            target.resize_slots(trait->slot_id + 1);
          }
          target.slots()[trait->slot_id] = value;
          break;
        }
        Value object;
        pop_value(object);
        if (status != InterpreterStatus::Ok) {
          break;
        }
        if (!object.is_object()) {
          return fail(InterpreterStatus::NullReference, "getsuper needs an object");
        }
        const Trait* trait = frame.owner_class->super->resolve_instance_trait(*multiname);
        if (trait == nullptr) {
          push_value(Value::undefined());
          break;
        }
        if (trait->is_slot_like()) {
          Object& target = *object.as_object();
          push_value(trait->slot_id < target.slots().size()
                         ? target.slots()[trait->slot_id]
                         : Value::undefined());
        } else if (trait->function != nullptr) {
          push_value(Value::object(trait->function));
        } else {
          push_value(Value::undefined());
        }
        break;
      }
      case op::k_newobject: {
        const std::uint32_t count = count_operand(instruction);
        auto object = std::make_shared<Object>();
        for (std::uint32_t index = 0; index < count; ++index) {
          Value value;
          pop_value(value);
          Value name;
          if (status == InterpreterStatus::Ok) {
            pop_value(name);
          }
          if (status != InterpreterStatus::Ok) {
            break;
          }
          Key key;
          key.name = to_string(name);
          Property& property = object->define_own_property(key);
          property.kind = PropertyKind::Value;
          property.value = std::move(value);
        }
        if (status == InterpreterStatus::Ok) {
          push_value(Value::object(std::move(object)));
        }
        break;
      }
      case op::k_newarray: {
        const std::uint32_t count = count_operand(instruction);
        std::vector<Value> elements;
        status = pop_arguments(frame, count, elements, message);
        if (status == InterpreterStatus::Ok) {
          auto array = std::make_shared<ArrayObject>();
          array->elements() = std::move(elements);
          push_value(Value::object(std::move(array)));
        }
        break;
      }
      case op::k_newfunction: {
        const std::uint32_t method = operand_at(instruction, 0);
        auto function = std::make_shared<FunctionObject>(frame.file->method_name(method),
                                                         frame.file, method);
        function->captured_scope() = frame.scopes;
        push_value(Value::object(std::move(function)));
        break;
      }
      case op::k_newclass: {
        const std::uint32_t index = operand_at(instruction, 0);
        const Class* klass = class_at(*frame.file, index);
        if (klass == nullptr || klass->class_object == nullptr) {
          return fail(InterpreterStatus::ClassIndexOutOfRange,
                      "class index " + std::to_string(index) + " has no linked class");
        }
        ExecutionResult initialized =
            initialize_class_value(Value::object(klass->class_object), depth);
        if (!initialized.ok()) {
          return initialized;
        }
        push_value(Value::object(klass->class_object));
        break;
      }
      case op::k_call: {
        const std::uint32_t count = count_operand(instruction);
        std::vector<Value> arguments;
        status = pop_arguments(frame, count, arguments, message);
        Value function;
        if (status == InterpreterStatus::Ok) {
          pop_value(function);
        }
        Value receiver;
        if (status == InterpreterStatus::Ok) {
          pop_value(receiver);
        }
        if (status != InterpreterStatus::Ok) {
          break;
        }
        if (!function.is_object() || function.as_object()->kind() != ObjectKind::Function) {
          if (limits_.lenient_builtins) {
            push_value(Value::undefined());
            break;
          }
          return fail(InterpreterStatus::CannotCall, "call target is not a function");
        }
        ExecutionResult nested = invoke(static_cast<const FunctionObject&>(*function.as_object()),
                                        receiver, std::move(arguments), {}, depth);
        if (!nested.ok()) {
          return nested;
        }
        push_value(nested.value);
        break;
      }
      case op::k_construct: {
        const std::uint32_t count = count_operand(instruction);
        std::vector<Value> arguments;
        status = pop_arguments(frame, count, arguments, message);
        Value target;
        if (status == InterpreterStatus::Ok) {
          pop_value(target);
        }
        if (status != InterpreterStatus::Ok) {
          break;
        }
        if (!target.is_object() || target.as_object()->kind() != ObjectKind::Class) {
          if (limits_.lenient_builtins) {
            push_value(Value::undefined());
            break;
          }
          return fail(InterpreterStatus::CannotConstruct, "construct target is not a class");
        }
        ExecutionResult nested = construct(
            frame, static_cast<const ClassObject&>(*target.as_object()), arguments, depth);
        if (!nested.ok()) {
          return nested;
        }
        push_value(nested.value);
        break;
      }
      case op::k_constructsuper: {
        const std::uint32_t count = count_operand(instruction);
        std::vector<Value> arguments;
        status = pop_arguments(frame, count, arguments, message);
        if (status != InterpreterStatus::Ok) {
          break;
        }
        if (frame.owner_class == nullptr || frame.owner_class->super == nullptr) {
          return fail(InterpreterStatus::TypeError,
                      "constructsuper outside a class with a superclass");
        }
        Class* super = frame.owner_class->super;
        if (super->iinit == kNoMethod || super->file == nullptr) {
          break;
        }
        auto initializer =
            std::make_shared<FunctionObject>(super->file->method_name(super->iinit), super->file,
                                             super->iinit);
        initializer->set_owner_class(super);
        ExecutionResult nested = invoke(*initializer, frame.locals.empty() ? Value::undefined()
                                                                         : frame.locals[0],
                                        std::move(arguments), {}, depth);
        if (!nested.ok()) {
          return nested;
        }
        break;
      }
      case op::k_constructprop: {
        const Multiname* multiname = resolve_multiname(*frame.file, multiname_operand(instruction));
        if (multiname == nullptr) {
          return fail(InterpreterStatus::TypeError, "constructprop has an invalid multiname");
        }
        const std::uint32_t count = count_operand(instruction);
        std::vector<Value> arguments;
        status = pop_arguments(frame, count, arguments, message);
        Value target;
        if (status == InterpreterStatus::Ok) {
          pop_value(target);
        }
        if (status != InterpreterStatus::Ok) {
          break;
        }
        if (!target.is_object()) {
          return fail(InterpreterStatus::CannotConstruct,
                      "constructprop needs an object holding a class");
        }
        ExecutionResult class_value = get_property(frame, *target.as_object(), *multiname, depth);
        if (!class_value.ok()) {
          return class_value;
        }
        if (!class_value.value.is_object() ||
            class_value.value.as_object()->kind() != ObjectKind::Class) {
          return fail(InterpreterStatus::CannotConstruct,
                      property_text(*multiname) + " is not a class");
        }
        ExecutionResult nested =
            construct(frame, static_cast<const ClassObject&>(*class_value.value.as_object()),
                      arguments, depth);
        if (!nested.ok()) {
          return nested;
        }
        push_value(nested.value);
        break;
      }
      case op::k_callproperty:
      case op::k_callpropvoid:
      case op::k_callproplex: {
        const Multiname* multiname = resolve_multiname(*frame.file, multiname_operand(instruction));
        if (multiname == nullptr) {
          return fail(InterpreterStatus::TypeError, "callproperty has an invalid multiname");
        }
        const std::uint32_t count = count_operand(instruction);
        std::vector<Value> arguments;
        status = pop_arguments(frame, count, arguments, message);
        Value receiver;
        if (status == InterpreterStatus::Ok) {
          pop_value(receiver);
        }
        if (status != InterpreterStatus::Ok) {
          break;
        }
        if (!receiver.is_object()) {
          if (limits_.lenient_builtins || instruction.opcode == op::k_callproplex) {
            if (instruction.opcode != op::k_callpropvoid) {
              push_value(Value::undefined());
            }
            break;
          }
          return fail(InterpreterStatus::NullReference,
                      "cannot call " + property_text(*multiname) + " on " +
                          value_kind_name(receiver.kind()));
        }
        ExecutionResult nested = call_property(frame, *receiver.as_object(), *multiname, arguments,
                                               depth);
        if (!nested.ok()) {
          return nested;
        }
        if (instruction.opcode != op::k_callpropvoid) {
          push_value(nested.value);
        }
        break;
      }
      case op::k_callsuper:
      case op::k_callsupervoid: {
        const Multiname* multiname = resolve_multiname(*frame.file, multiname_operand(instruction));
        if (multiname == nullptr) {
          return fail(InterpreterStatus::TypeError, "callsuper has an invalid multiname");
        }
        if (frame.owner_class == nullptr || frame.owner_class->super == nullptr) {
          return fail(InterpreterStatus::TypeError, "callsuper outside a class hierarchy");
        }
        const std::uint32_t count = count_operand(instruction);
        std::vector<Value> arguments;
        status = pop_arguments(frame, count, arguments, message);
        Value receiver;
        if (status == InterpreterStatus::Ok) {
          pop_value(receiver);
        }
        if (status != InterpreterStatus::Ok) {
          break;
        }
        if (!receiver.is_object()) {
          return fail(InterpreterStatus::NullReference, "callsuper needs an object receiver");
        }
        const Trait* trait = frame.owner_class->super->resolve_instance_trait(*multiname);
        if (trait == nullptr || trait->function == nullptr) {
          return fail(InterpreterStatus::CannotCall,
                      "super has no method " + property_text(*multiname));
        }
        ExecutionResult nested = invoke(*trait->function, receiver, std::move(arguments),
                                        frame.scopes, depth);
        if (!nested.ok()) {
          return nested;
        }
        if (instruction.opcode == op::k_callsuper) {
          push_value(nested.value);
        }
        break;
      }
      case op::k_callmethod:
      case op::k_callstatic:
        return fail(InterpreterStatus::NotImplemented,
                    std::string{"opcode "} + instruction.mnemonic + " is not implemented yet");
      case op::k_applytype: {
        const std::uint32_t count = count_operand(instruction);
        std::vector<Value> arguments;
        status = pop_arguments(frame, count, arguments, message);
        Value type;
        if (status == InterpreterStatus::Ok) {
          pop_value(type);
        }
        if (status != InterpreterStatus::Ok) {
          break;
        }
        auto vector = std::make_shared<ArrayObject>();
        vector->elements() = std::move(arguments);
        push_value(Value::object(std::move(vector)));
        break;
      }
      case op::k_hasnext2: {
        const std::uint32_t object_register = count_operand(instruction);
        const std::uint32_t index_register =
            instruction.operands.size() > 1 ? instruction.operands[1].value : 0;
        if (object_register >= frame.locals.size() || index_register >= frame.locals.size()) {
          return fail(InterpreterStatus::LocalOutOfRange, "hasnext2 register is out of range");
        }
        Value object = frame.locals[object_register];
        if (!object.is_object()) {
          push_value(Value::boolean(false));
          break;
        }
        std::vector<Key>& keys = frame.iterator_keys[object.as_object().get()];
        if (keys.empty()) {
          if (object.as_object()->kind() == ObjectKind::Array) {
            const auto& array = static_cast<const ArrayObject&>(*object.as_object());
            for (std::size_t index = 0; index < array.elements().size(); ++index) {
              Key key;
              key.name = std::to_string(index);
              keys.push_back(std::move(key));
            }
          } else {
            for (const auto& entry : object.as_object()->own_properties()) {
              keys.push_back(entry.first);
            }
            for (const Class* klass =
                     object.as_object()->instance_class();
                 klass != nullptr; klass = klass->super) {
              for (const Trait& trait : klass->instance_traits) {
                if (!trait.is_slot_like() || !trait.multiname.has_fixed_name()) {
                  continue;
                }
                Key key;
                key.name = trait.multiname.name;
                if (!trait.multiname.namespaces.empty()) {
                  key.name_space = trait.multiname.namespaces.front();
                }
                keys.push_back(std::move(key));
              }
            }
          }
        }
        const std::uint32_t cursor = to_uint32(frame.locals[index_register]);
        if (cursor < keys.size()) {
          frame.locals[object_register] = object;
          frame.locals[index_register] = Value::uint32(cursor + 1);
          push_value(Value::boolean(true));
        } else {
          frame.locals[index_register] = Value::uint32(0);
          push_value(Value::boolean(false));
        }
        break;
      }
      case op::k_nextname:
      case op::k_nextvalue: {
        Value index;
        pop_value(index);
        Value object;
        if (status == InterpreterStatus::Ok) {
          pop_value(object);
        }
        if (status != InterpreterStatus::Ok) {
          break;
        }
        const std::uint32_t cursor = to_uint32(index);
        if (cursor == 0 || !object.is_object()) {
          push_value(Value::undefined());
          break;
        }
        const auto found = frame.iterator_keys.find(object.as_object().get());
        if (found == frame.iterator_keys.end() || cursor > found->second.size()) {
          push_value(Value::undefined());
          break;
        }
        const Key& key = found->second[cursor - 1];
        if (instruction.opcode == op::k_nextname) {
          push_value(Value::string(key.name));
        } else {
          Multiname multiname;
          multiname.kind = MultinameKind::QName;
          multiname.name = key.name;
          multiname.namespaces.push_back(key.name_space);
          ExecutionResult nested = get_property(frame, *object.as_object(), multiname, depth);
          if (!nested.ok()) {
            return nested;
          }
          push_value(nested.value);
        }
        break;
      }
      case op::k_returnvalue: {
        Value value;
        pop_value(value);
        if (status != InterpreterStatus::Ok) {
          break;
        }
        return finish(std::move(value));
      }
      case op::k_returnvoid:
        return finish(Value::undefined());
      case op::k_throw: {
        Value value;
        pop_value(value);
        if (status != InterpreterStatus::Ok) {
          break;
        }
        const std::uint32_t throw_offset = static_cast<std::uint32_t>(instruction.offset);
        bool caught = false;
        for (const abc::AbcException& exception : frame.body->exceptions) {
          if (throw_offset < exception.from || throw_offset >= exception.to) {
            continue;
          }
          const std::uint32_t target = instruction_index_at(frame, static_cast<std::int32_t>(
                                                                  exception.target));
          if (target >= frame.instructions.size()) {
            continue;
          }
          bool type_matches = exception.exc_type == 0;
          if (!type_matches) {
            const Multiname* catch_type = resolve_multiname(*frame.file, exception.exc_type);
            if (catch_type == nullptr) {
              type_matches = true;
            } else {
              const Class* target_class = runtime_.find_class(*catch_type);
              if (target_class == nullptr || target_class->class_object == nullptr) {
                type_matches = true;
              } else {
                type_matches = is_instance_of(value, borrowed_object(*target_class->class_object));
              }
            }
          }
          if (!type_matches) {
            continue;
          }
          frame.stack.clear();
          frame.stack.push_back(value);
          frame.scopes.resize(frame.entry_scope_count);
          frame.ip = target;
          caught = true;
          break;
        }
        if (!caught) {
          return fail(InterpreterStatus::UncaughtException,
                      "uncaught exception: " + to_string(value));
        }
        break;
      }
      case op::k_newcatch: {
        auto catch_scope = std::make_shared<Object>();
        push_value(Value::object(std::move(catch_scope)));
        break;
      }
      case op::k_getouterscope:
        return fail(InterpreterStatus::NotImplemented,
                    std::string{"opcode "} + instruction.mnemonic + " is not implemented yet");
