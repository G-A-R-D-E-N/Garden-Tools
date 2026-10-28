#include <fo4/avm2/coercion.hpp>
#include <fo4/avm2/object.hpp>
#include <fo4/avm2/value.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <memory>
#include <string>

namespace {

using fo4::avm2::ArrayObject;
using fo4::avm2::ErrorObject;
using fo4::avm2::FunctionObject;
using fo4::avm2::PrimitiveHint;
using fo4::avm2::Value;

const double k_nan = std::numeric_limits<double>::quiet_NaN();
const double k_inf = std::numeric_limits<double>::infinity();

TEST(Avm2Value, CarriesOneKindPerStorage) {
  EXPECT_TRUE(Value::undefined().is_undefined());
  EXPECT_TRUE(Value::null().is_null());
  EXPECT_TRUE(Value::undefined().is_nullish());
  EXPECT_TRUE(Value::null().is_nullish());
  EXPECT_FALSE(Value::boolean(true).is_nullish());
  EXPECT_TRUE(Value::boolean(true).is_boolean());
  EXPECT_TRUE(Value::int32(-1).is_int32());
  EXPECT_TRUE(Value::uint32(1).is_uint32());
  EXPECT_TRUE(Value::number(1.5).is_number_like());
  EXPECT_TRUE(Value::int32(1).is_number_like());
  EXPECT_TRUE(Value::string("x").is_string());
  EXPECT_TRUE(Value::object(std::make_shared<fo4::avm2::Object>()).is_object());
  EXPECT_TRUE(Value::int32(1).is_primitive());
  EXPECT_FALSE(Value::object(std::make_shared<fo4::avm2::Object>()).is_primitive());
}

TEST(Avm2Value, ConvertsToBooleanTheAvm2Way) {
  EXPECT_FALSE(fo4::avm2::to_boolean(Value::undefined()));
  EXPECT_FALSE(fo4::avm2::to_boolean(Value::null()));
  EXPECT_FALSE(fo4::avm2::to_boolean(Value::boolean(false)));
  EXPECT_TRUE(fo4::avm2::to_boolean(Value::boolean(true)));
  EXPECT_FALSE(fo4::avm2::to_boolean(Value::int32(0)));
  EXPECT_TRUE(fo4::avm2::to_boolean(Value::int32(-1)));
  EXPECT_FALSE(fo4::avm2::to_boolean(Value::uint32(0)));
  EXPECT_TRUE(fo4::avm2::to_boolean(Value::uint32(1)));
  EXPECT_FALSE(fo4::avm2::to_boolean(Value::number(0.0)));
  EXPECT_FALSE(fo4::avm2::to_boolean(Value::number(k_nan)));
  EXPECT_TRUE(fo4::avm2::to_boolean(Value::number(-0.5)));
  EXPECT_FALSE(fo4::avm2::to_boolean(Value::string("")));
  EXPECT_TRUE(fo4::avm2::to_boolean(Value::string("0")));
  EXPECT_TRUE(fo4::avm2::to_boolean(Value::object(std::make_shared<fo4::avm2::Object>())));
}

TEST(Avm2Value, ConvertsNumbersWithTheInt32AndUint32Wrap) {
  EXPECT_EQ(fo4::avm2::to_int32(Value::number(3.9)), 3);
  EXPECT_EQ(fo4::avm2::to_int32(Value::number(-3.9)), -3);
  EXPECT_EQ(fo4::avm2::to_int32(Value::number(4294967295.0)), -1);
  EXPECT_EQ(fo4::avm2::to_int32(Value::number(4294967296.0)), 0);
  EXPECT_EQ(fo4::avm2::to_int32(Value::number(4294967301.0)), 5);
  EXPECT_EQ(fo4::avm2::to_int32(Value::number(k_nan)), 0);
  EXPECT_EQ(fo4::avm2::to_int32(Value::number(k_inf)), 0);
  EXPECT_EQ(fo4::avm2::to_int32(Value::uint32(4294967295u)), -1);
  EXPECT_EQ(fo4::avm2::to_int32(Value::null()), 0);
  EXPECT_EQ(fo4::avm2::to_int32(Value::boolean(true)), 1);

  EXPECT_EQ(fo4::avm2::to_uint32(Value::number(-1.0)), 4294967295u);
  EXPECT_EQ(fo4::avm2::to_uint32(Value::number(-1.5)), 4294967295u);
  EXPECT_EQ(fo4::avm2::to_uint32(Value::number(4294967296.0)), 0u);
  EXPECT_EQ(fo4::avm2::to_uint32(Value::number(k_nan)), 0u);
  EXPECT_EQ(fo4::avm2::to_uint32(Value::int32(-1)), 4294967295u);
}

TEST(Avm2Value, ParsesStringNumbers) {
  EXPECT_EQ(fo4::avm2::to_number(Value::string("")), 0.0);
  EXPECT_EQ(fo4::avm2::to_number(Value::string("   ")), 0.0);
  EXPECT_EQ(fo4::avm2::to_number(Value::string(" 12 ")), 12.0);
  EXPECT_EQ(fo4::avm2::to_number(Value::string("0x10")), 16.0);
  EXPECT_EQ(fo4::avm2::to_number(Value::string("0.5")), 0.5);
  EXPECT_EQ(fo4::avm2::to_number(Value::string("-2.5e2")), -250.0);
  EXPECT_EQ(fo4::avm2::to_number(Value::string("Infinity")), k_inf);
  EXPECT_EQ(fo4::avm2::to_number(Value::string("-Infinity")), -k_inf);
  EXPECT_TRUE(std::isnan(fo4::avm2::to_number(Value::string("12abc"))));
  EXPECT_TRUE(std::isnan(fo4::avm2::to_number(Value::string("abc"))));
  EXPECT_TRUE(std::isnan(fo4::avm2::to_number(Value::undefined())));
  EXPECT_EQ(fo4::avm2::to_number(Value::null()), 0.0);
}

// Detects: from_chars accepting inf/infinity or a doubled sign; such text must be NaN.
TEST(Avm2Value, RejectsNonEcmaSignedAndInfinityText) {
  EXPECT_TRUE(std::isnan(fo4::avm2::to_number(Value::string("inf"))));
  EXPECT_TRUE(std::isnan(fo4::avm2::to_number(Value::string("infinity"))));
  EXPECT_TRUE(std::isnan(fo4::avm2::to_number(Value::string("+-5"))));
  EXPECT_TRUE(std::isnan(fo4::avm2::to_number(Value::string("--5"))));
  EXPECT_EQ(fo4::avm2::to_number(Value::string(".5")), 0.5);
}

TEST(Avm2Value, FormatsNumbersWithoutATrailingZero) {
  EXPECT_EQ(fo4::avm2::to_string(Value::number(0.0)), "0");
  EXPECT_EQ(fo4::avm2::to_string(Value::number(-0.0)), "0");
  EXPECT_EQ(fo4::avm2::to_string(Value::number(1.0)), "1");
  EXPECT_EQ(fo4::avm2::to_string(Value::number(1.5)), "1.5");
  EXPECT_EQ(fo4::avm2::to_string(Value::number(-2.25)), "-2.25");
  EXPECT_EQ(fo4::avm2::to_string(Value::number(k_nan)), "NaN");
  EXPECT_EQ(fo4::avm2::to_string(Value::number(k_inf)), "Infinity");
  EXPECT_EQ(fo4::avm2::to_string(Value::int32(-7)), "-7");
  EXPECT_EQ(fo4::avm2::to_string(Value::uint32(7)), "7");
  EXPECT_EQ(fo4::avm2::to_string(Value::boolean(true)), "true");
  EXPECT_EQ(fo4::avm2::to_string(Value::undefined()), "undefined");
  EXPECT_EQ(fo4::avm2::to_string(Value::null()), "null");
}

TEST(Avm2Value, StrictEqualityKeepsNumericTypesTogether) {
  EXPECT_TRUE(fo4::avm2::strict_equals(Value::int32(1), Value::number(1.0)));
  EXPECT_TRUE(fo4::avm2::strict_equals(Value::uint32(1), Value::int32(1)));
  EXPECT_FALSE(fo4::avm2::strict_equals(Value::number(k_nan), Value::number(k_nan)));
  EXPECT_TRUE(fo4::avm2::strict_equals(Value::number(0.0), Value::number(-0.0)));
  EXPECT_TRUE(fo4::avm2::strict_equals(Value::undefined(), Value::undefined()));
  EXPECT_TRUE(fo4::avm2::strict_equals(Value::null(), Value::null()));
  EXPECT_FALSE(fo4::avm2::strict_equals(Value::undefined(), Value::null()));
  EXPECT_FALSE(fo4::avm2::strict_equals(Value::string("1"), Value::int32(1)));
  const auto object = std::make_shared<fo4::avm2::Object>();
  EXPECT_TRUE(fo4::avm2::strict_equals(Value::object(object), Value::object(object)));
  EXPECT_FALSE(fo4::avm2::strict_equals(Value::object(object),
                                        Value::object(std::make_shared<fo4::avm2::Object>())));
}

TEST(Avm2Value, LooseEqualityCoercesThePrimitiveSide) {
  EXPECT_TRUE(fo4::avm2::loose_equals(Value::string("1"), Value::int32(1)));
  EXPECT_TRUE(fo4::avm2::loose_equals(Value::boolean(true), Value::int32(1)));
  EXPECT_TRUE(fo4::avm2::loose_equals(Value::boolean(false), Value::string("0")));
  EXPECT_TRUE(fo4::avm2::loose_equals(Value::null(), Value::undefined()));
  EXPECT_FALSE(fo4::avm2::loose_equals(Value::null(), Value::int32(0)));
  EXPECT_FALSE(fo4::avm2::loose_equals(Value::undefined(), Value::int32(0)));
  EXPECT_FALSE(fo4::avm2::loose_equals(Value::number(k_nan), Value::number(k_nan)));
  EXPECT_TRUE(fo4::avm2::loose_equals(Value::string("a"), Value::string("a")));
  EXPECT_FALSE(fo4::avm2::loose_equals(Value::string("a"), Value::string("b")));
}

TEST(Avm2Value, RelationalOperatorsAreFalseOnNan) {
  EXPECT_TRUE(fo4::avm2::less_than(Value::int32(1), Value::int32(2)));
  EXPECT_FALSE(fo4::avm2::less_than(Value::int32(2), Value::int32(1)));
  EXPECT_TRUE(fo4::avm2::less_equals(Value::int32(2), Value::int32(2)));
  EXPECT_TRUE(fo4::avm2::greater_than(Value::number(2.5), Value::string("2")));
  EXPECT_TRUE(fo4::avm2::greater_equals(Value::string("b"), Value::string("a")));
  EXPECT_TRUE(fo4::avm2::less_than(Value::string("a"), Value::string("b")));
  EXPECT_FALSE(fo4::avm2::less_than(Value::string("b"), Value::string("a")));
  const double nan = k_nan;
  EXPECT_FALSE(fo4::avm2::less_than(Value::number(nan), Value::number(1.0)));
  EXPECT_FALSE(fo4::avm2::less_equals(Value::number(nan), Value::number(1.0)));
  EXPECT_FALSE(fo4::avm2::greater_than(Value::number(nan), Value::number(1.0)));
  EXPECT_FALSE(fo4::avm2::greater_equals(Value::number(nan), Value::number(1.0)));
}

TEST(Avm2Value, DefaultsObjectConversionsFromItsKind) {
  auto plain = std::make_shared<fo4::avm2::Object>();
  EXPECT_EQ(fo4::avm2::to_string(Value::object(plain)), "[object Object]");

  auto array = std::make_shared<ArrayObject>();
  array->elements().push_back(Value::int32(1));
  array->elements().push_back(Value::null());
  array->elements().push_back(Value::string("x"));
  EXPECT_EQ(fo4::avm2::to_string(Value::object(array)), "1,,x");
  EXPECT_TRUE(std::isnan(fo4::avm2::to_primitive(Value::object(array), PrimitiveHint::Number).as_number()));

  auto error = std::make_shared<ErrorObject>("TypeError", "boom");
  EXPECT_EQ(fo4::avm2::to_string(Value::object(error)), "TypeError: boom");
  auto bare_error = std::make_shared<ErrorObject>("Error", "");
  EXPECT_EQ(fo4::avm2::to_string(Value::object(bare_error)), "Error");

  auto function = std::make_shared<FunctionObject>("tick", nullptr, 0xFFFFFFFFu);
  EXPECT_EQ(fo4::avm2::to_string(Value::object(function)), "function tick() {}");
  EXPECT_EQ(fo4::avm2::typeof_name(Value::object(function)), "function");
  EXPECT_EQ(fo4::avm2::typeof_name(Value::object(plain)), "object");
  EXPECT_EQ(fo4::avm2::typeof_name(Value::undefined()), "undefined");
  EXPECT_EQ(fo4::avm2::typeof_name(Value::null()), "object");
  EXPECT_EQ(fo4::avm2::typeof_name(Value::number(1.0)), "number");
}

}  // namespace
