#include "fo4/c_api_papyrus.h"

#include <gtest/gtest.h>

TEST(CApiPapyrusTest, ValueConstructorsRoundTrip) {
  radicle_papyrus_value* value = nullptr;
  ASSERT_EQ(radicle_papyrus_value_string("text", 4, &value), RADICLE_STATUS_OK);
  ASSERT_NE(value, nullptr);
  EXPECT_EQ(radicle_papyrus_value_kind_of(value), RADICLE_PAPYRUS_VALUE_STRING);
  EXPECT_STREQ(radicle_papyrus_value_text(value), "text");
  radicle_papyrus_value_destroy(value);

  ASSERT_EQ(radicle_papyrus_value_int(-3, &value), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_papyrus_value_int_of(value), -3);
  radicle_papyrus_value_destroy(value);

  ASSERT_EQ(radicle_papyrus_value_float(1.5F, &value), RADICLE_STATUS_OK);
  EXPECT_FLOAT_EQ(radicle_papyrus_value_float_of(value), 1.5F);
  radicle_papyrus_value_destroy(value);

  ASSERT_EQ(radicle_papyrus_value_bool(1, &value), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_papyrus_value_bool_of(value), 1);
  radicle_papyrus_value_destroy(value);

  ASSERT_EQ(radicle_papyrus_value_object(9, "Actor", "Alert", &value), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_papyrus_value_object_handle(value), 9u);
  EXPECT_STREQ(radicle_papyrus_value_object_type(value), "Actor");
  EXPECT_STREQ(radicle_papyrus_value_object_state(value), "Alert");
  radicle_papyrus_value_destroy(value);

  radicle_papyrus_value* array = nullptr;
  ASSERT_EQ(radicle_papyrus_value_array(2, &array), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_papyrus_value_array_count(array), 2u);
  radicle_papyrus_value* element = nullptr;
  ASSERT_EQ(radicle_papyrus_value_int(4, &element), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_papyrus_value_array_set(array, 0, element), RADICLE_STATUS_OK);
  radicle_papyrus_value_destroy(element);
  ASSERT_EQ(radicle_papyrus_value_array_get(array, 0, &element), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_papyrus_value_int_of(element), 4);
  radicle_papyrus_value_destroy(element);
  radicle_papyrus_value_destroy(array);

  radicle_papyrus_value* point = nullptr;
  ASSERT_EQ(radicle_papyrus_value_struct("Point", &point), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_papyrus_value_int(8, &element), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_papyrus_value_struct_set(point, "X", element), RADICLE_STATUS_OK);
  radicle_papyrus_value_destroy(element);
  ASSERT_EQ(radicle_papyrus_value_struct_get(point, "x", &element), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_papyrus_value_int_of(element), 8);
  radicle_papyrus_value_destroy(element);
  EXPECT_EQ(radicle_papyrus_value_struct_get(point, "Missing", &element), RADICLE_STATUS_NOT_FOUND);
  radicle_papyrus_value_destroy(point);
}

TEST(CApiPapyrusTest, ValueNoneIsTheNoneKindAndRefusesANullOutput) {
  radicle_papyrus_value* value = nullptr;
  ASSERT_EQ(radicle_papyrus_value_none(&value), RADICLE_STATUS_OK);
  ASSERT_NE(value, nullptr);
  EXPECT_EQ(radicle_papyrus_value_kind_of(value), RADICLE_PAPYRUS_VALUE_NONE);
  radicle_papyrus_value_destroy(value);
  EXPECT_EQ(radicle_papyrus_value_none(nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST(CApiPapyrusTest, ObjectReadersAnswerNullForANullObject) {
  EXPECT_EQ(radicle_papyrus_object_handle(nullptr), 0u);
  EXPECT_EQ(radicle_papyrus_object_type(nullptr), nullptr);
  EXPECT_EQ(radicle_papyrus_object_state(nullptr), nullptr);
}
