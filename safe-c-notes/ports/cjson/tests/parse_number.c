/*
  Copyright (c) 2009-2017 Dave Gamble and cJSON contributors

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
  THE SOFTWARE.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "unity/examples/unity_config.h"
#include "unity/src/unity.h"
#include "common.h"


static void assert_is_number(cJSON *number_item)
{
    TEST_ASSERT_NOT_NULL_MESSAGE(number_item, "Item is NULL.");

    assert_not_in_list(number_item);
    assert_has_no_child(number_item);
    assert_has_type(number_item, cJSON_Number);
    assert_has_no_reference(number_item);
    assert_has_no_const_string(number_item);
    assert_has_no_valuestring(number_item);
    assert_has_no_string(number_item);
}

static void assert_parse_number(cJSON *item, const char *string, int integer, double real)
{
    parse_buffer buffer = { 0, 0, 0, 0 };
    /* strlen runs first: a call between the two dependent assignments is rejected */
    const size_t buffer_length = strlen(string) + sizeof("");
    buffer.content = (const unsigned char*)__unsafe_null_terminated_to_indexable(string);
    buffer.length = buffer_length;

    TEST_ASSERT_TRUE(parse_number(item, &buffer));
    assert_is_number(item);
    TEST_ASSERT_EQUAL_INT(integer, item->valueint);
    TEST_ASSERT_EQUAL_DOUBLE(real, item->valuedouble);
}

static void assert_parse_big_number(cJSON *item, const char *string)
{
    parse_buffer buffer = { 0, 0, 0, 0 };
    /* strlen runs first: a call between the two dependent assignments is rejected */
    const size_t buffer_length = strlen(string) + sizeof("");
    buffer.content = (const unsigned char*)__unsafe_null_terminated_to_indexable(string);
    buffer.length = buffer_length;

    TEST_ASSERT_TRUE(parse_number(item, &buffer));
    assert_is_number(item);
}

static void parse_number_should_parse_zero(void)
{
    cJSON item[1] = {0};
    assert_parse_number(item, "0", 0, 0);
    assert_parse_number(item, "0.0", 0, 0.0);
    assert_parse_number(item, "-0", 0, -0.0);
}

static void parse_number_should_parse_negative_integers(void)
{
    cJSON item[1] = {0};
    assert_parse_number(item, "-1", -1, -1);
    assert_parse_number(item, "-32768", -32768, -32768.0);
    assert_parse_number(item, "-2147483648", (int)-2147483648.0, -2147483648.0);
}

static void parse_number_should_parse_positive_integers(void)
{
    cJSON item[1] = {0};
    assert_parse_number(item, "1", 1, 1);
    assert_parse_number(item, "32767", 32767, 32767.0);
    assert_parse_number(item, "2147483647", (int)2147483647.0, 2147483647.0);
}

static void parse_number_should_parse_positive_reals(void)
{
    cJSON item[1] = {0};
    assert_parse_number(item, "0.001", 0, 0.001);
    assert_parse_number(item, "10e-10", 0, 10e-10);
    assert_parse_number(item, "10E-10", 0, 10e-10);
    assert_parse_number(item, "10e10", INT_MAX, 10e10);
    assert_parse_number(item, "123e+127", INT_MAX, 123e127);
    assert_parse_number(item, "123e-128", 0, 123e-128);
}

static void parse_number_should_parse_negative_reals(void)
{
    cJSON item[1] = {0};
    assert_parse_number(item, "-0.001", 0, -0.001);
    assert_parse_number(item, "-10e-10", 0, -10e-10);
    assert_parse_number(item, "-10E-10", 0, -10e-10);
    assert_parse_number(item, "-10e20", INT_MIN, -10e20);
    assert_parse_number(item, "-123e+127", INT_MIN, -123e127);
    assert_parse_number(item, "-123e-128", 0, -123e-128);
}

static void parse_number_should_parse_big_numbers(void)
{
    cJSON item[1] = {0};
    assert_parse_big_number(item, "9999999999999999999999999999999999999999999999912345678901234567");
    assert_parse_big_number(item, "9999999999999999999999999999999999999999999999912345678901234567E10");
    assert_parse_big_number(item, "999999999999999999999999999999999999999999999991234567890.1234567");
}

int CJSON_CDECL main(void)
{
    UNITY_BEGIN();
    RUN_TEST(parse_number_should_parse_zero);
    RUN_TEST(parse_number_should_parse_negative_integers);
    RUN_TEST(parse_number_should_parse_positive_integers);
    RUN_TEST(parse_number_should_parse_positive_reals);
    RUN_TEST(parse_number_should_parse_negative_reals);
    RUN_TEST(parse_number_should_parse_big_numbers);
    return UNITY_END();
}
