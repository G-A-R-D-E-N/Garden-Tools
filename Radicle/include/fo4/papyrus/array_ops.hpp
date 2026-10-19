#pragma once

#include "fo4/papyrus/status.hpp"
#include "fo4/papyrus/value.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace fo4::papyrus {

Status arrayCreate(const Value& count, ValueType elementType, std::string elementTypeName,
                   std::size_t maxLength, Value& out);
Status arrayLength(const Value& array, Value& out);
Status arrayGetElement(const Value& array, const Value& index, Value& out);
Status arraySetElement(Value& array, const Value& index, const Value& value);
Status arrayFindElement(const Value& array, const Value& value, const Value& start, bool reverse,
                        Value& out);
Status arrayAdd(Value& array, const Value& value, const Value& count, std::size_t maxLength);
Status arrayInsert(Value& array, const Value& value, const Value& index, std::size_t maxLength);
Status arrayRemoveLast(Value& array);
Status arrayRemove(Value& array, const Value& index, const Value& count);
Status arrayClear(Value& array);

}
