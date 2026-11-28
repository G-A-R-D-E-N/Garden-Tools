#include "fo4/papyrus/array_ops.hpp"

#include <memory>
#include <utility>

namespace fo4::papyrus {
namespace {

Status openArray(const Value& value, std::shared_ptr<Array>& out) {
    if (isNoneLike(value)) return Status::NullArray;
    if (value.type != ValueType::Array) return Status::TypeMismatch;
    if (value.array == nullptr) return Status::NullArray;
    out = value.array;
    return Status::Ok;
}

Status readIndex(const Value& index, std::int32_t& out) {
    if (index.type != ValueType::Integer) return Status::TypeMismatch;
    out = index.integer;
    return Status::Ok;
}

Status coerceElement(const Array& array, const Value& value, Value& out) {
    if (value.type == ValueType::Array && value.array != nullptr) return Status::TypeMismatch;
    if (value.type == ValueType::Struct && value.structure != nullptr) {
        if (sameName(array.elementTypeName, "Var") ||
            sameName(array.elementTypeName, value.structure->typeName)) {
            out = value;
            return Status::Ok;
        }
        return Status::TypeMismatch;
    }
    if (array.elementType == ValueType::None || isNoneLike(value)) {
        out = value;
        return Status::Ok;
    }
    if (value.type == array.elementType) {
        out = value;
        return Status::Ok;
    }
    if (array.elementType == ValueType::Object || array.elementType == ValueType::Array) {
        return Status::TypeMismatch;
    }
    if (!castScalar(value, array.elementType, out)) return Status::TypeMismatch;
    return Status::Ok;
}

}

Status arrayCreate(const Value& count, ValueType elementType, std::string elementTypeName,
                   std::size_t maxLength, Value& out) {
    std::int32_t length = 0;
    const Status status = readIndex(count, length);
    if (status != Status::Ok) return status;
    if (length < 0) return Status::InvalidCount;
    if (static_cast<std::size_t>(length) > maxLength) return Status::ArrayTooLong;

    auto array = std::make_shared<Array>();
    array->elementType = elementType;
    array->elementTypeName = std::move(elementTypeName);
    array->elements.reserve(static_cast<std::size_t>(length));
    for (std::int32_t i = 0; i < length; ++i) {
        array->elements.push_back(defaultValueFor(array->elementTypeName));
    }
    out = arrayValue(std::move(array));
    return Status::Ok;
}

Status arrayLength(const Value& array, Value& out) {
    std::shared_ptr<Array> target;
    const Status status = openArray(array, target);
    if (status != Status::Ok) return status;
    out = integerValue(static_cast<std::int32_t>(target->elements.size()));
    return Status::Ok;
}

Status arrayGetElement(const Value& array, const Value& index, Value& out) {
    std::shared_ptr<Array> target;
    Status status = openArray(array, target);
    if (status != Status::Ok) return status;
    std::int32_t position = 0;
    status = readIndex(index, position);
    if (status != Status::Ok) return status;
    if (position < 0 || static_cast<std::size_t>(position) >= target->elements.size()) {
        return Status::IndexOutOfRange;
    }
    out = target->elements[static_cast<std::size_t>(position)];
    return Status::Ok;
}

Status arraySetElement(Value& array, const Value& index, const Value& value) {
    std::shared_ptr<Array> target;
    Status status = openArray(array, target);
    if (status != Status::Ok) return status;
    std::int32_t position = 0;
    status = readIndex(index, position);
    if (status != Status::Ok) return status;
    if (position < 0 || static_cast<std::size_t>(position) >= target->elements.size()) {
        return Status::IndexOutOfRange;
    }
    Value element;
    status = coerceElement(*target, value, element);
    if (status != Status::Ok) return status;
    target->elements[static_cast<std::size_t>(position)] = element;
    return Status::Ok;
}

Status arrayFindElement(const Value& array, const Value& value, const Value& start, bool reverse,
                        Value& out) {
    std::shared_ptr<Array> target;
    Status status = openArray(array, target);
    if (status != Status::Ok) return status;

    const std::int32_t size = static_cast<std::int32_t>(target->elements.size());
    std::int32_t position = reverse ? size - 1 : 0;
    if (!isNoneLike(start)) {
        status = readIndex(start, position);
        if (status != Status::Ok) return status;
        if (reverse && position < 0) position += size;
    }

    if (reverse) {
        if (position >= size) position = size - 1;
        for (std::int32_t i = position; i >= 0; --i) {
            if (equalValues(target->elements[static_cast<std::size_t>(i)], value)) {
                out = integerValue(i);
                return Status::Ok;
            }
        }
    } else {
        if (position < 0) position = 0;
        for (std::int32_t i = position; i < size; ++i) {
            if (equalValues(target->elements[static_cast<std::size_t>(i)], value)) {
                out = integerValue(i);
                return Status::Ok;
            }
        }
    }
    out = integerValue(-1);
    return Status::Ok;
}

Status arrayAdd(Value& array, const Value& value, const Value& count, std::size_t maxLength) {
    std::shared_ptr<Array> target;
    Status status = openArray(array, target);
    if (status != Status::Ok) return status;

    std::int32_t repeats = 1;
    if (!isNoneLike(count)) {
        status = readIndex(count, repeats);
        if (status != Status::Ok) return status;
    }
    if (repeats < 0) return Status::InvalidCount;
    if (target->elements.size() + static_cast<std::size_t>(repeats) > maxLength) {
        return Status::ArrayTooLong;
    }

    Value element;
    status = coerceElement(*target, value, element);
    if (status != Status::Ok) return status;
    for (std::int32_t i = 0; i < repeats; ++i) target->elements.push_back(element);
    return Status::Ok;
}

Status arrayInsert(Value& array, const Value& value, const Value& index, std::size_t maxLength) {
    std::shared_ptr<Array> target;
    Status status = openArray(array, target);
    if (status != Status::Ok) return status;

    std::int32_t position = 0;
    status = readIndex(index, position);
    if (status != Status::Ok) return status;
    if (position < 0 || static_cast<std::size_t>(position) > target->elements.size()) {
        return Status::IndexOutOfRange;
    }
    if (target->elements.size() + 1 > maxLength) return Status::ArrayTooLong;

    Value element;
    status = coerceElement(*target, value, element);
    if (status != Status::Ok) return status;
    target->elements.insert(target->elements.begin() + position, element);
    return Status::Ok;
}

Status arrayRemoveLast(Value& array) {
    std::shared_ptr<Array> target;
    const Status status = openArray(array, target);
    if (status != Status::Ok) return status;
    if (target->elements.empty()) return Status::IndexOutOfRange;
    target->elements.pop_back();
    return Status::Ok;
}

Status arrayRemove(Value& array, const Value& index, const Value& count) {
    std::shared_ptr<Array> target;
    Status status = openArray(array, target);
    if (status != Status::Ok) return status;

    std::int32_t position = 0;
    status = readIndex(index, position);
    if (status != Status::Ok) return status;
    std::int32_t repeats = 1;
    if (!isNoneLike(count)) {
        status = readIndex(count, repeats);
        if (status != Status::Ok) return status;
    }
    if (repeats < 0) return Status::InvalidCount;

    const std::size_t size = target->elements.size();
    if (position < 0 || static_cast<std::size_t>(position) >= size) {
        return Status::IndexOutOfRange;
    }
    const std::size_t first = static_cast<std::size_t>(position);
    const std::size_t last = first + static_cast<std::size_t>(repeats);
    if (last > size) return Status::IndexOutOfRange;

    target->elements.erase(target->elements.begin() + static_cast<std::ptrdiff_t>(first),
                           target->elements.begin() + static_cast<std::ptrdiff_t>(last));
    return Status::Ok;
}

Status arrayClear(Value& array) {
    std::shared_ptr<Array> target;
    const Status status = openArray(array, target);
    if (status != Status::Ok) return status;
    target->elements.clear();
    return Status::Ok;
}

}
