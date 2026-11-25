/*
 SPDX-License-Identifier: MIT

 Parson 1.5.4 (https://github.com/kgabis/parson)
 Copyright (c) 2012 - 2026 Krzysztof Gabis

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

#ifndef parson_parson_h
#define parson_parson_h

#ifdef __cplusplus
extern "C"
{
#endif
#if 0
} /* unconfuse xcode */
#endif

#define PARSON_VERSION_MAJOR 1
#define PARSON_VERSION_MINOR 5
#define PARSON_VERSION_PATCH 4

#define PARSON_VERSION_STRING "1.5.4"

#include <stddef.h>   /* size_t */
#include "safec.h"    /* Safe C annotations; they expand to nothing on other compilers */

/* Types and enums */
typedef struct json_object_t JSON_Object;
typedef struct json_array_t  JSON_Array;
typedef struct json_value_t  JSON_Value;

enum json_value_type {
    JSONError   = -1,
    JSONNull    = 1,
    JSONString  = 2,
    JSONNumber  = 3,
    JSONObject  = 4,
    JSONArray   = 5,
    JSONBoolean = 6
};
typedef int JSON_Value_Type;

enum json_result_t {
    JSONSuccess = 0,
    JSONFailure = -1
};
typedef int JSON_Status;

typedef _Returns_owned void *_Nullable __sized_by_or_null(size) (*JSON_Malloc_Function)(size_t size);
typedef void   (*JSON_Free_Function)(void *_Nullable _Owned __unsafe_indexable ptr);

/* A function used for serializing numbers (see json_set_number_serialization_function).
   If 'buf' is null then it should return number of bytes that would've been written 
   (but not more than PARSON_NUM_BUF_SIZE).
   buf has PARSON_NUM_BUF_SIZE (64 by default) bytes.
*/
typedef int (*JSON_Number_Serialization_Function)(double num, char *_Nullable __counted_by_or_null(64) buf);

/* Call only once, before calling any other function from parson API. If not called, malloc and free
   from stdlib will be used for all allocations */
void json_set_allocation_functions(JSON_Malloc_Function malloc_fun, JSON_Free_Function free_fun _Takes(1));

/* Sets if slashes should be escaped or not when serializing JSON. By default slashes are escaped.
 This function sets a global setting and is not thread safe. */
void json_set_escape_slashes(int escape_slashes);

/* Sets float format used for serialization of numbers.
   Make sure it can't serialize to a string longer than PARSON_NUM_BUF_SIZE.
   If format is null then the default format is used. */
void json_set_float_serialization_format(const char *_Nullable format _Noescape);

/* Sets a function that will be used for serialization of numbers.
   If function is null then the default serialization function is used. */
void json_set_number_serialization_function(JSON_Number_Serialization_Function _Nullable fun);

/* Parses first JSON value in a file, returns NULL in case of error */
_Returns_owned JSON_Value *_Nullable json_parse_file(const char *filename _Noescape);

/* Parses first JSON value in a file and ignores comments (/ * * / and //),
   returns NULL in case of error */
_Returns_owned JSON_Value *_Nullable json_parse_file_with_comments(const char *filename _Noescape);

/*  Parses first JSON value in a string, returns NULL in case of error */
_Returns_owned JSON_Value *_Nullable json_parse_string(const char *_Nullable string _Noescape);

/*  Parses first JSON value in a string and ignores comments (/ * * / and //),
    returns NULL in case of error */
_Returns_owned JSON_Value *_Nullable json_parse_string_with_comments(const char *_Nullable string _Noescape);

/* Serialization */
size_t      json_serialization_size(const JSON_Value *_Nullable value); /* returns 0 on fail */
JSON_Status json_serialize_to_buffer(const JSON_Value *_Nullable value, char *__counted_by(buf_size_in_bytes) buf, size_t buf_size_in_bytes);
JSON_Status json_serialize_to_file(const JSON_Value *_Nullable value, const char *filename _Noescape);
_Returns_owned char *_Nullable __null_terminated json_serialize_to_string(const JSON_Value *_Nullable value);

/* Pretty serialization */
size_t      json_serialization_size_pretty(const JSON_Value *_Nullable value); /* returns 0 on fail */
JSON_Status json_serialize_to_buffer_pretty(const JSON_Value *_Nullable value, char *__counted_by(buf_size_in_bytes) buf, size_t buf_size_in_bytes);
JSON_Status json_serialize_to_file_pretty(const JSON_Value *_Nullable value, const char *filename _Noescape);
_Returns_owned char *_Nullable __null_terminated json_serialize_to_string_pretty(const JSON_Value *_Nullable value);

void        json_free_serialized_string(char *_Nullable _Owned __null_terminated string); /* frees string from json_serialize_to_string and json_serialize_to_string_pretty */

/* Comparing */
int  json_value_equals(const JSON_Value *_Nullable a, const JSON_Value *_Nullable b);

/* Validation
   This is *NOT* JSON Schema. It validates json by checking if object have identically
   named fields with matching types.
   For example schema {"name":"", "age":0} will validate
   {"name":"Joe", "age":25} and {"name":"Joe", "age":25, "gender":"m"},
   but not {"name":"Joe"} or {"name":"Joe", "age":"Cucumber"}.
   In case of arrays, only first value in schema is checked against all values in tested array.
   Empty objects ({}) validate all objects, empty arrays ([]) validate all arrays,
   null validates values of every type.
 */
JSON_Status json_validate(const JSON_Value *_Nullable schema, const JSON_Value *_Nullable value);

/*
 * JSON Object
 */
JSON_Value  *_Nullable json_object_get_value  (const JSON_Object *_Nullable object _Lifetimebound, const char *_Nullable name _Noescape);
const char  *_Nullable json_object_get_string (const JSON_Object *_Nullable object _Lifetimebound, const char *_Nullable name _Noescape);
size_t        json_object_get_string_len(const JSON_Object *_Nullable object, const char *_Nullable name _Noescape); /* doesn't account for last null character */
JSON_Object *_Nullable json_object_get_object (const JSON_Object *_Nullable object _Lifetimebound, const char *_Nullable name _Noescape);
JSON_Array  *_Nullable json_object_get_array  (const JSON_Object *_Nullable object _Lifetimebound, const char *_Nullable name _Noescape);
double        json_object_get_number (const JSON_Object *_Nullable object, const char *_Nullable name _Noescape); /* returns 0 on fail */
int           json_object_get_boolean(const JSON_Object *_Nullable object, const char *_Nullable name _Noescape); /* returns -1 on fail */

/* dotget functions enable addressing values with dot notation in nested objects,
 just like in structs or c++/java/c# objects (e.g. objectA.objectB.value).
 Because valid names in JSON can contain dots, some values may be inaccessible
 this way. */
JSON_Value  *_Nullable json_object_dotget_value  (const JSON_Object *_Nullable object _Lifetimebound, const char *name _Noescape);
const char  *_Nullable json_object_dotget_string (const JSON_Object *_Nullable object _Lifetimebound, const char *name _Noescape);
size_t        json_object_dotget_string_len(const JSON_Object *_Nullable object, const char *name _Noescape); /* doesn't account for last null character */
JSON_Object *_Nullable json_object_dotget_object (const JSON_Object *_Nullable object _Lifetimebound, const char *name _Noescape);
JSON_Array  *_Nullable json_object_dotget_array  (const JSON_Object *_Nullable object _Lifetimebound, const char *name _Noescape);
double        json_object_dotget_number (const JSON_Object *_Nullable object, const char *name _Noescape); /* returns 0 on fail */
int           json_object_dotget_boolean(const JSON_Object *_Nullable object, const char *name _Noescape); /* returns -1 on fail */

/* Functions to get available names */
size_t        json_object_get_count   (const JSON_Object *_Nullable object);
const char  *_Nullable json_object_get_name    (const JSON_Object *_Nullable object, size_t index);
JSON_Value  *_Nullable json_object_get_value_at(const JSON_Object *_Nullable object, size_t index);
JSON_Value  *_Nullable json_object_get_wrapping_value(const JSON_Object *_Nullable object);

/* Functions to check if object has a value with a specific name. Returned value is 1 if object has
 * a value and 0 if it doesn't. dothas functions behave exactly like dotget functions. */
int json_object_has_value        (const JSON_Object *_Nullable object, const char *_Nullable name _Noescape);
int json_object_has_value_of_type(const JSON_Object *_Nullable object, const char *_Nullable name _Noescape, JSON_Value_Type type);

int json_object_dothas_value        (const JSON_Object *_Nullable object, const char *name _Noescape);
int json_object_dothas_value_of_type(const JSON_Object *_Nullable object, const char *name _Noescape, JSON_Value_Type type);

/* Creates new name-value pair or frees and replaces old value with a new one.
 * json_object_set_value does not copy passed value so it shouldn't be freed afterwards. */
JSON_Status json_object_set_value(JSON_Object *_Exclusive _Nullable object, const char *_Nullable name _Noescape, JSON_Value *_Nullable value);
JSON_Status json_object_set_string(JSON_Object *_Exclusive _Nullable object, const char *_Nullable name _Noescape, const char *_Nullable string _Noescape);
JSON_Status json_object_set_string_with_len(JSON_Object *_Exclusive _Nullable object, const char *_Nullable name _Noescape, const char *_Nullable __counted_by_or_null(len) string, size_t len);  /* length shouldn't include last null character */
JSON_Status json_object_set_number(JSON_Object *_Exclusive _Nullable object, const char *_Nullable name _Noescape, double number);
JSON_Status json_object_set_boolean(JSON_Object *_Exclusive _Nullable object, const char *_Nullable name _Noescape, int boolean);
JSON_Status json_object_set_null(JSON_Object *_Exclusive _Nullable object, const char *_Nullable name _Noescape);

/* Works like dotget functions, but creates whole hierarchy if necessary.
 * json_object_dotset_value does not copy passed value so it shouldn't be freed afterwards. */
JSON_Status json_object_dotset_value(JSON_Object *_Exclusive _Nullable object, const char *_Nullable name _Noescape, JSON_Value *_Nullable value);
JSON_Status json_object_dotset_string(JSON_Object *_Exclusive _Nullable object, const char *_Nullable name _Noescape, const char *_Nullable string _Noescape);
JSON_Status json_object_dotset_string_with_len(JSON_Object *_Exclusive _Nullable object, const char *_Nullable name _Noescape, const char *_Nullable __counted_by_or_null(len) string, size_t len); /* length shouldn't include last null character */
JSON_Status json_object_dotset_number(JSON_Object *_Exclusive _Nullable object, const char *_Nullable name _Noescape, double number);
JSON_Status json_object_dotset_boolean(JSON_Object *_Exclusive _Nullable object, const char *_Nullable name _Noescape, int boolean);
JSON_Status json_object_dotset_null(JSON_Object *_Exclusive _Nullable object, const char *_Nullable name _Noescape);

/* Frees and removes name-value pair */
JSON_Status json_object_remove(JSON_Object *_Exclusive _Nullable object, const char *name _Noescape);

/* Works like dotget function, but removes name-value pair only on exact match. */
JSON_Status json_object_dotremove(JSON_Object *_Exclusive _Nullable object, const char *key _Noescape);

/* Removes all name-value pairs in object */
JSON_Status json_object_clear(JSON_Object *_Exclusive _Nullable object);

/*
 *JSON Array
 */
JSON_Value  *_Nullable json_array_get_value  (const JSON_Array *_Nullable array, size_t index);
const char  *_Nullable json_array_get_string (const JSON_Array *_Nullable array, size_t index);
size_t        json_array_get_string_len(const JSON_Array *_Nullable array, size_t index); /* doesn't account for last null character */
JSON_Object *_Nullable json_array_get_object (const JSON_Array *_Nullable array, size_t index);
JSON_Array  *_Nullable json_array_get_array  (const JSON_Array *_Nullable array, size_t index);
double        json_array_get_number (const JSON_Array *_Nullable array, size_t index); /* returns 0 on fail */
int           json_array_get_boolean(const JSON_Array *_Nullable array, size_t index); /* returns -1 on fail */
size_t        json_array_get_count  (const JSON_Array *_Nullable array);
JSON_Value  *_Nullable json_array_get_wrapping_value(const JSON_Array *_Nullable array);

/* Frees and removes value at given index, does nothing and returns JSONFailure if index doesn't exist.
 * Order of values in array may change during execution.  */
JSON_Status json_array_remove(JSON_Array *_Exclusive _Nullable array, size_t i);

/* Frees and removes from array value at given index and replaces it with given one.
 * Does nothing and returns JSONFailure if index doesn't exist.
 * json_array_replace_value does not copy passed value so it shouldn't be freed afterwards. */
JSON_Status json_array_replace_value(JSON_Array *_Exclusive _Nullable array, size_t i, JSON_Value *_Nullable value);
JSON_Status json_array_replace_string(JSON_Array *_Exclusive _Nullable array, size_t i, const char *_Nullable string _Noescape);
JSON_Status json_array_replace_string_with_len(JSON_Array *_Exclusive _Nullable array, size_t i, const char *_Nullable __counted_by_or_null(len) string, size_t len); /* length shouldn't include last null character */
JSON_Status json_array_replace_number(JSON_Array *_Exclusive _Nullable array, size_t i, double number);
JSON_Status json_array_replace_boolean(JSON_Array *_Exclusive _Nullable array, size_t i, int boolean);
JSON_Status json_array_replace_null(JSON_Array *_Exclusive _Nullable array, size_t i);

/* Frees and removes all values from array */
JSON_Status json_array_clear(JSON_Array *_Exclusive _Nullable array);

/* Appends new value at the end of array.
 * json_array_append_value does not copy passed value so it shouldn't be freed afterwards. */
JSON_Status json_array_append_value(JSON_Array *_Exclusive _Nullable array, JSON_Value *_Nullable value);
JSON_Status json_array_append_string(JSON_Array *_Exclusive _Nullable array, const char *_Nullable string _Noescape);
JSON_Status json_array_append_string_with_len(JSON_Array *_Exclusive _Nullable array, const char *_Nullable __counted_by_or_null(len) string, size_t len); /* length shouldn't include last null character */
JSON_Status json_array_append_number(JSON_Array *_Exclusive _Nullable array, double number);
JSON_Status json_array_append_boolean(JSON_Array *_Exclusive _Nullable array, int boolean);
JSON_Status json_array_append_null(JSON_Array *_Exclusive _Nullable array);

/*
 *JSON Value
 */
_Returns_owned JSON_Value *_Nullable json_value_init_object (void);
_Returns_owned JSON_Value *_Nullable json_value_init_array  (void);
_Returns_owned JSON_Value *_Nullable json_value_init_string (const char *_Nullable string _Noescape); /* copies passed string */
_Returns_owned JSON_Value *_Nullable json_value_init_string_with_len(const char *_Nullable __counted_by_or_null(length) string, size_t length); /* copies passed string, length shouldn't include last null character */
_Returns_owned JSON_Value *_Nullable json_value_init_number (double number);
_Returns_owned JSON_Value *_Nullable json_value_init_boolean(int boolean);
_Returns_owned JSON_Value *_Nullable json_value_init_null   (void);
_Returns_owned JSON_Value *_Nullable json_value_deep_copy   (const JSON_Value *_Nullable value);
void         json_value_free        (JSON_Value *_Nullable _Owned value);

JSON_Value_Type json_value_get_type   (const JSON_Value *_Nullable value);
JSON_Object *_Nullable json_value_get_object (const JSON_Value *_Nullable value);
JSON_Array  *_Nullable json_value_get_array  (const JSON_Value *_Nullable value);
const char  *_Nullable json_value_get_string (const JSON_Value *_Nullable value);
size_t          json_value_get_string_len(const JSON_Value *_Nullable value); /* doesn't account for last null character */
double          json_value_get_number (const JSON_Value *_Nullable value);
int             json_value_get_boolean(const JSON_Value *_Nullable value);
JSON_Value  *_Nullable json_value_get_parent (const JSON_Value *_Nullable value);

/* Same as above, but shorter */
JSON_Value_Type json_type   (const JSON_Value *_Nullable value);
JSON_Object *_Nullable json_object (const JSON_Value *_Nullable value);
JSON_Array  *_Nullable json_array  (const JSON_Value *_Nullable value);
const char  *_Nullable json_string (const JSON_Value *_Nullable value);
size_t          json_string_len(const JSON_Value *_Nullable value); /* doesn't account for last null character */
double          json_number (const JSON_Value *_Nullable value);
int             json_boolean(const JSON_Value *_Nullable value);

#ifdef __cplusplus
}
#endif

#endif
