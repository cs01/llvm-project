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


static void assert_is_object(cJSON *object_item)
{
    TEST_ASSERT_NOT_NULL_MESSAGE(object_item, "Item is NULL.");

    assert_not_in_list(object_item);
    assert_has_type(object_item, cJSON_Object);
    assert_has_no_reference(object_item);
    assert_has_no_const_string(object_item);
    assert_has_no_valuestring(object_item);
    assert_has_no_string(object_item);
}

static void assert_is_child(cJSON *child_item, const char *name, int type)
{
    TEST_ASSERT_NOT_NULL_MESSAGE(child_item, "Child item is NULL.");
    TEST_ASSERT_NOT_NULL_MESSAGE(child_item->string, "Child item doesn't have a name.");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(name, child_item->string, "Child item has the wrong name.");
    TEST_ASSERT_BITS(0xFF, type, child_item->type);
}

static void assert_not_object(cJSON *item, const char *json)
{
    parse_buffer parsebuffer = { 0, 0, 0, 0 };
    /* strlen runs first: a call between the two dependent assignments is rejected */
    const size_t parsebuffer_length = strlen(json) + sizeof("");
    parsebuffer.content = (const unsigned char*)__unsafe_null_terminated_to_indexable(json);
    parsebuffer.length = parsebuffer_length;

    TEST_ASSERT_FALSE(parse_object(item, &parsebuffer));
    assert_is_invalid(item);
    reset(item);
}

static void assert_parse_object(cJSON *item, const char *json)
{
    parse_buffer parsebuffer = { 0, 0, 0, 0 };
    /* strlen runs first: a call between the two dependent assignments is rejected */
    const size_t parsebuffer_length = strlen(json) + sizeof("");
    parsebuffer.content = (const unsigned char*)__unsafe_null_terminated_to_indexable(json);
    parsebuffer.length = parsebuffer_length;

    TEST_ASSERT_TRUE(parse_object(item, &parsebuffer));
    assert_is_object(item);
}

static void parse_object_should_parse_empty_objects(void)
{
    cJSON item[1] = {0};
    assert_parse_object(item, "{}");
    assert_has_no_child(item);
    reset(item);

    assert_parse_object(item, "{\n\t}");
    assert_has_no_child(item);
    reset(item);
}

static void parse_object_should_parse_objects_with_one_element(void)
{
    cJSON item[1] = {0};

    assert_parse_object(item, "{\"one\":1}");
    assert_is_child(item->child, "one", cJSON_Number);
    reset(item);

    assert_parse_object(item, "{\"hello\":\"world!\"}");
    assert_is_child(item->child, "hello", cJSON_String);
    reset(item);

    assert_parse_object(item, "{\"array\":[]}");
    assert_is_child(item->child, "array", cJSON_Array);
    reset(item);

    assert_parse_object(item, "{\"null\":null}");
    assert_is_child(item->child, "null", cJSON_NULL);
    reset(item);
}

static void parse_object_should_parse_objects_with_multiple_elements(void)
{
    cJSON item[1] = {0};
    assert_parse_object(item, "{\"one\":1\t,\t\"two\"\n:2, \"three\":3}");
    assert_is_child(item->child, "one", cJSON_Number);
    TEST_ASSERT_NOT_NULL(item->child);
    assert_is_child(item->child->next, "two", cJSON_Number);
    TEST_ASSERT_NOT_NULL(item->child->next);
    assert_is_child(item->child->next->next, "three", cJSON_Number);
    reset(item);

    {
        size_t i = 0;
        cJSON *node = NULL;
        int expected_types[7] =
        {
            cJSON_Number,
            cJSON_NULL,
            cJSON_True,
            cJSON_False,
            cJSON_Array,
            cJSON_String,
            cJSON_Object
        };
        const char *expected_names[7] =
        {
            "one",
            "NULL",
            "TRUE",
            "FALSE",
            "array",
            "world",
            "object"
        };
        assert_parse_object(item, "{\"one\":1, \"NULL\":null, \"TRUE\":true, \"FALSE\":false, \"array\":[], \"world\":\"hello\", \"object\":{}}");

        node = item->child;
        for (
                i = 0;
                (i < (sizeof(expected_types)/sizeof(int)))
                && (node != NULL);
                (void)i++, node = node->next)
        {
            assert_is_child(node, expected_names[i], expected_types[i]);
        }
        TEST_ASSERT_EQUAL_INT(i, 7);
        reset(item);
    }
}

static void parse_object_should_not_parse_non_objects(void)
{
    cJSON item[1] = {0};
    assert_not_object(item, "");
    assert_not_object(item, "{");
    assert_not_object(item, "}");
    assert_not_object(item, "[\"hello\",{}]");
    assert_not_object(item, "42");
    assert_not_object(item, "3.14");
    assert_not_object(item, "\"{}hello world!\n\"");
}

int CJSON_CDECL main(void)
{

    UNITY_BEGIN();
    RUN_TEST(parse_object_should_parse_empty_objects);
    RUN_TEST(parse_object_should_not_parse_non_objects);
    RUN_TEST(parse_object_should_parse_objects_with_multiple_elements);
    RUN_TEST(parse_object_should_parse_objects_with_one_element);
    return UNITY_END();
}
