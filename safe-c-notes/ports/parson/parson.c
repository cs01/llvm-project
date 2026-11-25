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
#ifdef _MSC_VER
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif /* _CRT_SECURE_NO_WARNINGS */
#endif /* _MSC_VER */

#include "parson.h"

#define PARSON_IMPL_VERSION_MAJOR 1
#define PARSON_IMPL_VERSION_MINOR 5
#define PARSON_IMPL_VERSION_PATCH 4

#if (PARSON_VERSION_MAJOR != PARSON_IMPL_VERSION_MAJOR)\
|| (PARSON_VERSION_MINOR != PARSON_IMPL_VERSION_MINOR)\
|| (PARSON_VERSION_PATCH != PARSON_IMPL_VERSION_PATCH)
#error "parson version mismatch between parson.c and parson.h"
#endif

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <errno.h>

/* Apparently sscanf is not implemented in some "standard" libraries, so don't use it, if you
 * don't have to. */
#ifdef sscanf
#undef sscanf
#define sscanf THINK_TWICE_ABOUT_USING_SSCANF
#endif

/* strcpy is unsafe */
#ifdef strcpy
#undef strcpy
#endif
#define strcpy USE_MEMCPY_INSTEAD_OF_STRCPY

#define STARTING_CAPACITY 16
#define MAX_NESTING       2048

#ifndef PARSON_DEFAULT_FLOAT_FORMAT
#define PARSON_DEFAULT_FLOAT_FORMAT "%1.17g" /* do not increase precision without incresing NUM_BUF_SIZE */
#endif

#ifndef PARSON_NUM_BUF_SIZE
#define PARSON_NUM_BUF_SIZE 64 /* double printed with "%1.17g" shouldn't be longer than 25 bytes so let's be paranoid and use 64 */
#endif

#ifndef PARSON_INDENT_STR
#define PARSON_INDENT_STR "    "
#endif

#define SIZEOF_TOKEN(a)       (sizeof(a) - 1)
#define SKIP_CHAR(str)        ((*str)++)
#define SKIP_WHITESPACES(str) while (isspace((unsigned char)(**str))) { SKIP_CHAR(str); }
#define MAX(a, b)             ((a) > (b) ? (a) : (b))

#undef malloc
#undef free

#if defined(isnan) && defined(isinf)
#define IS_NUMBER_INVALID(x) (isnan((x)) || isinf((x)))
#else
#define IS_NUMBER_INVALID(x) (((x) * 0.0) != 0.0)
#endif

#define OBJECT_INVALID_IX ((size_t)-1)

/* Wrappers, because the hook types carry annotations that libc's own
 * declarations spell differently. */
static void *_Nullable __sized_by_or_null(size) default_malloc(size_t size) {
    return malloc(size);
}

static void default_free(void *_Nullable _Owned __unsafe_indexable ptr) {
    free(ptr);
}

/* These settings are global and, as parson.h says, not thread safe: they are
 * set before any other parson call. */
static JSON_Malloc_Function parson_malloc_hook = default_malloc;
static JSON_Free_Function parson_free = default_free; /* _Takes: the typedef's _Owned is not looked at for the pointer */

static int parson_escape_slashes = 1;

static char *_Owned _Nullable __null_terminated parson_float_format = NULL;

static JSON_Number_Serialization_Function _Nullable parson_number_serialization_function = NULL;

/* unsafe: the one place that trusts a user-supplied malloc to return 'size'
 * fresh bytes. alloc_size lets callers give the result a type. */
__unsafe __attribute__((alloc_size(1)))
static void *_Nullable parson_malloc(size_t size) {
    return parson_malloc_hook(size);
}

#define IS_CONT(b) (((unsigned char)(b) & 0xC0) == 0x80) /* is utf-8 continuation byte */

typedef int parson_bool_t;

#define PARSON_TRUE 1
#define PARSON_FALSE 0

/* size is the length plus one for the terminator, or 0 if there is no string */
typedef struct json_string {
    char *_Owned _Nullable __counted_by_or_null(size) chars;
    size_t size;
} JSON_String;

/* Type definitions */
struct json_value_t {
    JSON_Value      * _Nullable parent; /* back pointer: the parent owns this value */
    JSON_Value_Type  type;
    struct { /* one field per type, the others null: a union of owners is not checked */
        JSON_String  string;
        double       number;
        JSON_Object *_Owned _Nullable object;
        JSON_Array  *_Owned _Nullable array;
        int          boolean;
        int          null;
    } value;
};

struct json_object_t {
    JSON_Value    * _Nullable wrapping_value;
    size_t        *_Owned _Nullable __counted_by_or_null(cell_capacity) cells;
    unsigned long *_Owned _Nullable __counted_by_or_null(item_capacity) hashes;
    char          *_Owned _Nullable __null_terminated *_Owned _Nullable __counted_by_or_null(item_capacity) names;
    JSON_Value    *_Owned _Nullable *_Owned _Nullable __counted_by_or_null(item_capacity) values;
    size_t        *_Owned _Nullable __counted_by_or_null(item_capacity) cell_ixs;
    size_t         count;
    size_t         item_capacity;
    size_t         cell_capacity;
};

struct json_array_t {
    JSON_Value  * _Nullable wrapping_value;
    JSON_Value  *_Owned _Nullable *_Owned _Nullable __counted_by_or_null(capacity) items;
    size_t       count;
    size_t       capacity;
};

/* Output of the serializer. With a null buf it only counts. */
/* The characters of a string value, borrowed from it. p is null if there are none. */
typedef struct json_chars {
    const char *_Nullable __counted_by_or_null(len) p;
    size_t len; /* without the terminator */
} JSON_Chars;

typedef struct json_writer {
    char *_Nullable __counted_by_or_null(cap) buf;
    size_t cap;
    size_t pos;
} JSON_Writer;

/* Various */
static char *_Nullable __null_terminated read_file(const char *filename);
static void   remove_comments(char *__counted_by(length) string, size_t length, const char *start_token, const char *end_token);
static char *_Nullable __null_terminated parson_strndup(const char *__counted_by(n) string, size_t n);
static char *_Nullable __null_terminated parson_strdup(const char *string _Noescape);
static void   skip_chars(const char **string, size_t n);

static int           hex_char_to_int(char c);
static JSON_Status   parse_utf16_hex(const char *string _Noescape, unsigned int *result);
static int           num_bytes_in_utf8_sequence(unsigned char c);
static JSON_Status   verify_utf8_sequence(const unsigned char *__counted_by(string_len) string, size_t string_len, int *out_seq_len);
static parson_bool_t is_valid_utf8(const char *__counted_by(string_len) string, size_t string_len);
static parson_bool_t is_decimal(const char *string _Noescape, size_t length);
static unsigned long hash_string(const char *string _Noescape, size_t n);

/* JSON Object */
static JSON_Object *_Nullable json_object_make(JSON_Value * wrapping_value);
static JSON_Status   json_object_grow_and_rehash(JSON_Object * object);
static size_t        json_object_get_cell_ix(const JSON_Object *object, const char *key _Noescape, size_t key_len, unsigned long hash, parson_bool_t *out_found);
static JSON_Status   json_object_add(JSON_Object * object, char *_Owned __null_terminated name, JSON_Value *_Owned value);
static JSON_Value  *_Nullable json_object_getn_value(const JSON_Object *_Nullable object, const char *name _Noescape, size_t name_len);
static void          json_object_free(JSON_Object *_Nullable _Owned object);

/* JSON Array */
static JSON_Array *_Nullable json_array_make(JSON_Value * wrapping_value);
static JSON_Status  json_array_reserve(JSON_Array * array);
static JSON_Status  json_array_add(JSON_Array * array, JSON_Value *_Owned value);
static JSON_Status  json_array_resize(JSON_Array * array, size_t new_capacity);
static void         json_array_free(JSON_Array *_Nullable _Owned array);

/* JSON Value */
static JSON_Value *_Nullable json_value_init_string_no_copy(char *_Owned __counted_by(size) string, size_t size);
static JSON_Chars json_value_get_chars(const JSON_Value *_Nullable value);

/* Parser */
static JSON_Status   skip_quotes(const char **string, size_t *length _Noescape);
static JSON_Status   parse_utf16(const char **unprocessed, char *__counted_by(output_size) output, size_t output_size, size_t *output_pos);
static char *_Nullable __bidi_indexable process_string(const char *input _Noescape, size_t input_len, size_t *output_len _Noescape);
static char *_Nullable __bidi_indexable get_quoted_string(const char **string, size_t *output_string_len _Noescape);
static char *_Nullable __null_terminated get_quoted_key(const char **string);
static JSON_Value *_Nullable parse_object_value(const char **string, size_t nesting);
static JSON_Value *_Nullable parse_array_value(const char **string, size_t nesting);
static JSON_Value *_Nullable parse_string_value(const char **string);
static JSON_Value *_Nullable parse_boolean_value(const char **string);
static JSON_Value *_Nullable parse_number_value(const char **string);
static JSON_Value *_Nullable parse_null_value(const char **string);
static JSON_Value *_Nullable parse_value(const char **string, size_t nesting);

/* Serialization */
static JSON_Status json_serialize_r(const JSON_Value *_Nullable value, JSON_Writer *writer, int level, parson_bool_t is_pretty);
static void json_serialize_string(const char *__counted_by(len) string _Noescape, size_t len, JSON_Writer *writer);

/* Various */
static char *_Nullable __null_terminated read_file(const char * filename) {
    FILE *_Nullable fp = fopen(filename, "r");
    size_t size_to_read = 0;
    size_t size_read = 0;
    long pos;
    char *_Nullable _Owned file_contents;
    if (!fp) {
        return NULL;
    }
    fseek(fp, 0L, SEEK_END);
    pos = ftell(fp);
    if (pos < 0) {
        fclose(fp);
        return NULL;
    }
    size_to_read = pos;
    rewind(fp);
    file_contents = (char*)parson_malloc(sizeof(char) * (size_to_read + 1));
    if (!file_contents) {
        fclose(fp);
        return NULL;
    }
    size_read = fread(file_contents, 1, size_to_read, fp);
    if (size_read == 0 || ferror(fp)) {
        fclose(fp);
        parson_free(file_contents);
        return NULL;
    }
    fclose(fp);
    file_contents[size_read] = '\0';
    /* unsafe: the terminator was just written at file_contents[size_read] */
    __unsafe { return __unsafe_null_terminated_from_indexable(file_contents, file_contents + size_read); }
}

static parson_bool_t token_at(const char *__counted_by(length) string, size_t length, size_t pos, const char *token) {
    for (; *token != '\0'; token++, pos++) {
        if (pos >= length || string[pos] != *token) {
            return PARSON_FALSE;
        }
    }
    return PARSON_TRUE;
}

static void remove_comments(char *__counted_by(length) string, size_t length, const char *start_token, const char *end_token) {
    parson_bool_t in_string = PARSON_FALSE, escaped = PARSON_FALSE;
    size_t i = 0, end = 0;
    char current_char;
    size_t start_token_len = strlen(start_token);
    size_t end_token_len = strlen(end_token);
    if (start_token_len == 0 || end_token_len == 0) {
        return;
    }
    for (i = 0; i < length; i++) {
        current_char = string[i];
        if (current_char == '\\' && !escaped) {
            escaped = PARSON_TRUE;
            continue;
        } else if (current_char == '\"' && !escaped) {
            in_string = !in_string;
        } else if (!in_string && token_at(string, length, i, start_token)) {
            memset(string + i, ' ', start_token_len);
            for (end = i + start_token_len; !token_at(string, length, end, end_token); end++) {
                if (end >= length) {
                    return;
                }
            }
            end += end_token_len;
            memset(string + i, ' ', end - i);
            i = end - 1;
        }
        escaped = PARSON_FALSE;
    }
}

static char *_Nullable __null_terminated parson_strndup(const char *__counted_by(n) string, size_t n) {
    /* We expect the caller has validated that 'n' fits within the input buffer. */
    char *_Nullable _Owned output_string = (char*)parson_malloc(n + 1);
    if (!output_string) {
        return NULL;
    }
    output_string[n] = '\0';
    memcpy(output_string, string, n);
    /* unsafe: the terminator was just written at output_string[n] */
    __unsafe { return __unsafe_null_terminated_from_indexable(output_string, output_string + n); }
}

static char *_Nullable __null_terminated parson_strdup(const char *string _Noescape) {
    size_t n = strlen(string);
    return parson_strndup(__null_terminated_to_indexable(string), n);
}

/* Terminated strings step one character at a time. */
static void skip_chars(const char **string, size_t n) {
    for (; n > 0; n--) {
        SKIP_CHAR(string);
    }
}


static int hex_char_to_int(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    } else if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    } else if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static JSON_Status parse_utf16_hex(const char *s _Noescape, unsigned int *result) {
    unsigned int cp = 0;
    int i = 0, x = 0;
    for (i = 0; i < 4; i++, s++) {
        x = hex_char_to_int(*s); /* -1 for the terminator too */
        if (x == -1) {
            return JSONFailure;
        }
        cp = (cp << 4) | (unsigned int)x;
    }
    *result = cp;
    return JSONSuccess;
}

static int num_bytes_in_utf8_sequence(unsigned char c) {
    if (c == 0xC0 || c == 0xC1 || c > 0xF4 || IS_CONT(c)) {
        return 0;
    } else if ((c & 0x80) == 0) {    /* 0xxxxxxx */
        return 1;
    } else if ((c & 0xE0) == 0xC0) { /* 110xxxxx */
        return 2;
    } else if ((c & 0xF0) == 0xE0) { /* 1110xxxx */
        return 3;
    } else if ((c & 0xF8) == 0xF0) { /* 11110xxx */
        return 4;
    }
    return 0; /* won't happen */
}

static JSON_Status verify_utf8_sequence(const unsigned char *__counted_by(string_len) string, size_t string_len, int *out_seq_len) {
    unsigned int cp = 0;
    int seq_len = -1;

    *out_seq_len = -1;

    seq_len = num_bytes_in_utf8_sequence(string[0]);

    if ((size_t)seq_len > string_len) {
        return JSONFailure;
    } else if (seq_len == 1) {
        cp = string[0];
    } else if (seq_len == 2 && IS_CONT(string[1])) {
        cp = string[0] & 0x1F;
        cp = (cp << 6) | (string[1] & 0x3F);
    } else if (seq_len == 3 && IS_CONT(string[1]) && IS_CONT(string[2])) {
        cp = ((unsigned char)string[0]) & 0xF;
        cp = (cp << 6) | (string[1] & 0x3F);
        cp = (cp << 6) | (string[2] & 0x3F);
    } else if (seq_len == 4 && IS_CONT(string[1]) && IS_CONT(string[2]) && IS_CONT(string[3])) {
        cp = string[0] & 0x7;
        cp = (cp << 6) | (string[1] & 0x3F);
        cp = (cp << 6) | (string[2] & 0x3F);
        cp = (cp << 6) | (string[3] & 0x3F);
    } else {
        return JSONFailure;
    }

    /* overlong encodings */
    if ((cp < 0x80    && seq_len > 1) ||
        (cp < 0x800   && seq_len > 2) ||
        (cp < 0x10000 && seq_len > 3)) {
        return JSONFailure;
    }

    /* invalid unicode */
    if (cp > 0x10FFFF) {
        return JSONFailure;
    }

    /* surrogate halves */
    if (cp >= 0xD800 && cp <= 0xDFFF) {
        return JSONFailure;
    }

    *out_seq_len = seq_len;

    return JSONSuccess;
}

static parson_bool_t is_valid_utf8(const char *__counted_by(string_len) string, size_t string_len) {
    int seq_len = 0;
    size_t pos = 0;
    while (pos < string_len) {
        if (verify_utf8_sequence((const unsigned char*)string + pos, string_len - pos, &seq_len) != JSONSuccess) {
            return PARSON_FALSE;
        }
        pos += seq_len;
    }
    return PARSON_TRUE;
}

static parson_bool_t is_decimal(const char *string _Noescape, size_t length) {
    const char *digits = string;
    size_t digits_length = length;
    if (length > 0 && *digits == '-') {
        digits++;
        digits_length--;
    }
    if (digits_length > 1 && *digits == '0') {
        digits++;
        if (*digits != '.') {
            return PARSON_FALSE;
        }
    }
    for (; length > 0; length--, string++) {
        if (*string == 'x' || *string == 'X') {
            return PARSON_FALSE;
        }
    }
    return PARSON_TRUE;
}

static unsigned long hash_string(const char *string _Noescape, size_t n) {
#ifdef PARSON_FORCE_HASH_COLLISIONS
    (void)string;
    (void)n;
    return 0;
#else
    unsigned long hash = 5381;
    unsigned char c;
    size_t i = 0;
    for (i = 0; i < n; i++, string++) {
        c = *string;
        if (c == '\0') {
            break;
        }
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
    }
    return hash;
#endif
}

/* JSON Object */
static JSON_Object *_Nullable json_object_make(JSON_Value * wrapping_value) {
    JSON_Object *_Nullable _Owned new_obj = (JSON_Object*)parson_malloc(sizeof(JSON_Object));
    if (new_obj == NULL) {
        return NULL;
    }
    memset(new_obj, 0, sizeof(JSON_Object));
    new_obj->wrapping_value = wrapping_value;
    return new_obj;
}



static JSON_Status json_object_grow_and_rehash(JSON_Object * object) {
    size_t i = 0, cell_ix = 0;
    size_t cell_capacity = MAX(object->cell_capacity * 2, STARTING_CAPACITY);
    size_t item_capacity = (unsigned int)(cell_capacity * 7/10);
    size_t *_Nullable _Owned cells = (size_t*)parson_malloc(cell_capacity * sizeof(size_t));
    unsigned long *_Nullable _Owned hashes = (unsigned long*)parson_malloc(item_capacity * sizeof(unsigned long));
    char *_Owned _Nullable __null_terminated *_Nullable _Owned names =
        (char *_Owned _Nullable __null_terminated *)parson_malloc(item_capacity * sizeof(char*));
    JSON_Value *_Owned _Nullable *_Nullable _Owned values =
        (JSON_Value *_Owned _Nullable *)parson_malloc(item_capacity * sizeof(JSON_Value*));
    size_t *_Nullable _Owned cell_ixs = (size_t*)parson_malloc(item_capacity * sizeof(size_t));
    if (cells == NULL || hashes == NULL || names == NULL || values == NULL || cell_ixs == NULL) {
        parson_free(cells);
        parson_free(hashes);
        parson_free(names);
        parson_free(values);
        parson_free(cell_ixs);
        return JSONFailure;
    }
    for (i = 0; i < cell_capacity; i++) {
        cells[i] = OBJECT_INVALID_IX;
    }
    for (i = 0; i < item_capacity; i++) {
        names[i] = NULL;
        values[i] = NULL;
    }
    if (object->names != NULL && object->values != NULL && object->hashes != NULL) {
        for (i = 0; i < object->count; i++) {
            /* keys are unique, so the first free cell is the one */
            cell_ix = object->hashes[i] & (cell_capacity - 1);
            while (cells[cell_ix] != OBJECT_INVALID_IX) {
                cell_ix = (cell_ix + 1) & (cell_capacity - 1);
            }
            cells[cell_ix] = i;
            cell_ixs[i] = cell_ix;
            hashes[i] = object->hashes[i];
            names[i] = _Take(object->names[i]);
            values[i] = _Take(object->values[i]);
        }
    }
    parson_free(_Take(object->cells));
    parson_free(_Take(object->hashes));
    parson_free(_Take(object->names));
    parson_free(_Take(object->values));
    parson_free(_Take(object->cell_ixs));
    object->cells = cells;
    object->cell_capacity = cell_capacity;
    object->hashes = hashes;
    object->names = names;
    object->values = values;
    object->cell_ixs = cell_ixs;
    object->item_capacity = item_capacity;
    return JSONSuccess;
}

static size_t json_object_get_cell_ix(const JSON_Object *object, const char *key _Noescape, size_t key_len, unsigned long hash, parson_bool_t *out_found) {
    size_t cell_ix = hash & (object->cell_capacity - 1);
    size_t cell = 0;
    size_t ix = 0;
    unsigned int i = 0;
    unsigned long hash_to_check = 0;
    const char *_Nullable __null_terminated key_to_check = NULL;
    size_t key_to_check_len = 0;

    *out_found = PARSON_FALSE;

    if (object->cells == NULL || object->hashes == NULL || object->names == NULL) {
        return OBJECT_INVALID_IX;
    }

    for (i = 0; i < object->cell_capacity; i++) {
        ix = (cell_ix + i) & (object->cell_capacity - 1);
        cell = object->cells[ix];
        if (cell == OBJECT_INVALID_IX) {
            return ix;
        }
        hash_to_check = object->hashes[cell];
        if (hash != hash_to_check) {
            continue;
        }
        key_to_check = object->names[cell];
        if (key_to_check == NULL) {
            continue;
        }
        key_to_check_len = strlen(key_to_check);
        if (key_to_check_len == key_len && strncmp(key, key_to_check, key_len) == 0) {
            *out_found = PARSON_TRUE;
            return ix;
        }
    }
    return OBJECT_INVALID_IX;
}

/* Store an owner in an element of an owned array. The old element must have been taken. */
static void json_object_store_name(JSON_Object * object, size_t ix, char *_Nullable _Owned __null_terminated name) {
    if (object->names != NULL) {
        /* unsafe: still bounds checked; Safe C rejects a[i] = owner when a is an owned field (PORT.md) */
        __unsafe { object->names[ix] = name; }
    }
}

static void json_object_store_value(JSON_Object * object, size_t ix, JSON_Value *_Nullable _Owned value) {
    if (object->values != NULL) {
        __unsafe { object->values[ix] = value; } /* unsafe: as above */
    }
}

/* Takes name and value in every case: they are freed if adding fails. */
static JSON_Status json_object_add(JSON_Object * object, char *_Owned __null_terminated name, JSON_Value *_Owned value) {
    unsigned long hash = 0;
    parson_bool_t found = PARSON_FALSE;
    size_t name_len = strlen(name);
    size_t cell_ix = 0;

    hash = hash_string(name, name_len);
    cell_ix = json_object_get_cell_ix(object, name, name_len, hash, &found);
    if (found || (object->count >= object->item_capacity && json_object_grow_and_rehash(object) != JSONSuccess)) {
        parson_free(name);
        json_value_free(value);
        return JSONFailure;
    }
    cell_ix = json_object_get_cell_ix(object, name, name_len, hash, &found);
    if (object->cells == NULL || object->names == NULL || object->values == NULL
        || object->cell_ixs == NULL || object->hashes == NULL) {
        parson_free(name);
        json_value_free(value);
        return JSONFailure; /* won't happen: the capacity is not 0 here */
    }

    value->parent = object->wrapping_value;
    json_object_store_name(object, object->count, name);
    object->cells[cell_ix] = object->count;
    json_object_store_value(object, object->count, value);
    object->cell_ixs[object->count] = cell_ix;
    object->hashes[object->count] = hash;
    object->count++;

    return JSONSuccess;
}

/* Split from json_object_getn_value: clang crashes on o->cells[i] when o is a _Nullable parameter (PORT.md) */
static JSON_Value *_Nullable json_object_findn_value(const JSON_Object *object, const char *name _Noescape, size_t name_len) {
    parson_bool_t found = PARSON_FALSE;
    unsigned long hash = hash_string(name, name_len);
    size_t cell_ix = json_object_get_cell_ix(object, name, name_len, hash, &found);
    if (!found || object->cells == NULL) {
        return NULL;
    }
    return json_object_get_value_at(object, object->cells[cell_ix]);
}

static JSON_Value *_Nullable json_object_getn_value(const JSON_Object *_Nullable object, const char *name _Noescape, size_t name_len) {
    return object ? json_object_findn_value(object, name, name_len) : NULL;
}

static JSON_Status json_object_remove_internal(JSON_Object * object, const char *name _Noescape) {
    unsigned long hash = 0;
    parson_bool_t found = PARSON_FALSE;
    size_t cell = 0;
    size_t item_ix = 0;
    size_t last_item_ix = 0;
    size_t i = 0;
    size_t j = 0;
    size_t x = 0;
    size_t k = 0;
    char *_Nullable _Owned __null_terminated last_name = NULL;
    JSON_Value *_Nullable _Owned last_value = NULL;

    hash = hash_string(name, strlen(name));
    found = PARSON_FALSE;
    cell = json_object_get_cell_ix(object, name, strlen(name), hash, &found);
    if (!found || object->cells == NULL || object->names == NULL || object->values == NULL
        || object->cell_ixs == NULL || object->hashes == NULL) {
        return JSONFailure;
    }

    item_ix = object->cells[cell];
    json_value_free(_Take(object->values[item_ix]));
    parson_free(_Take(object->names[item_ix]));
    last_item_ix = object->count - 1;
    if (item_ix < last_item_ix) {
        last_name = _Take(object->names[last_item_ix]);
        json_object_store_name(object, item_ix, last_name);
        last_value = _Take(object->values[last_item_ix]);
        json_object_store_value(object, item_ix, last_value);
        object->cell_ixs[item_ix] = object->cell_ixs[last_item_ix];
        object->hashes[item_ix] = object->hashes[last_item_ix];
        object->cells[object->cell_ixs[item_ix]] = item_ix;
    }
    object->count--;

    i = cell;
    j = i;
    for (x = 0; x < (object->cell_capacity - 1); x++) {
        j = (j + 1) & (object->cell_capacity - 1);
        if (object->cells[j] == OBJECT_INVALID_IX) {
            break;
        }
        k = object->hashes[object->cells[j]] & (object->cell_capacity - 1);
        if ((j > i && (k <= i || k > j))
         || (j < i && (k <= i && k > j))) {
            object->cell_ixs[object->cells[j]] = i;
            object->cells[i] = object->cells[j];
            i = j;
        }
    }
    object->cells[i] = OBJECT_INVALID_IX;
    return JSONSuccess;
}

static JSON_Status json_object_dotremove_internal(JSON_Object * _Nullable object, const char *name _Noescape) {
    JSON_Value *_Nullable temp_value = NULL;
    JSON_Object *_Nullable temp_object = NULL;
    const char *_Nullable __null_terminated dot_pos = strchr(name, '.');
    if (object == NULL) {
        return JSONFailure;
    }
    if (!dot_pos) {
        return json_object_remove_internal(object, name);
    }
    temp_value = json_object_getn_value(object, name, dot_pos - name);
    temp_object = json_value_get_object(temp_value);
    return json_object_dotremove_internal(temp_object, dot_pos + 1);
}

static void json_object_free(JSON_Object *_Nullable _Owned object) {
    if (object == NULL) {
        return;
    }
    json_object_clear(object);
    parson_free(_Take(object->cells));
    parson_free(_Take(object->names));
    parson_free(_Take(object->values));
    parson_free(_Take(object->cell_ixs));
    parson_free(_Take(object->hashes));
    parson_free(object);
}

/* JSON Array */
static JSON_Array *_Nullable json_array_make(JSON_Value * wrapping_value) {
    JSON_Array *_Nullable _Owned new_array = (JSON_Array*)parson_malloc(sizeof(JSON_Array));
    if (new_array == NULL) {
        return NULL;
    }
    new_array->wrapping_value = wrapping_value;
    new_array->items = NULL;
    new_array->capacity = 0;
    new_array->count = 0;
    return new_array;
}

/* Stores an owner in an element of an owned array. The old element must have been taken. */
static void json_array_store(JSON_Array * array, size_t ix, JSON_Value *_Nullable _Owned value) {
    if (array->items != NULL) {
        __unsafe { array->items[ix] = value; } /* unsafe: as in json_object_store_name */
    }
}

/* Makes room for one more item. */
static JSON_Status json_array_reserve(JSON_Array * array) {
    if (array->count >= array->capacity) {
        return json_array_resize(array, MAX(array->capacity * 2, STARTING_CAPACITY));
    }
    return JSONSuccess;
}

/* Takes value in every case: it is freed if adding fails. */
static JSON_Status json_array_add(JSON_Array * array, JSON_Value *_Owned value) {
    if (json_array_reserve(array) != JSONSuccess || array->items == NULL) {
        json_value_free(value);
        return JSONFailure;
    }
    value->parent = array->wrapping_value;
    json_array_store(array, array->count, value);
    array->count++;
    return JSONSuccess;
}

static JSON_Status json_array_resize(JSON_Array * array, size_t new_capacity) {
    JSON_Value *_Owned _Nullable *_Nullable _Owned new_items = NULL;
    size_t i = 0;
    if (new_capacity == 0) {
        return JSONFailure;
    }
    new_items = (JSON_Value *_Owned _Nullable *)parson_malloc(new_capacity * sizeof(JSON_Value*));
    if (new_items == NULL) {
        return JSONFailure;
    }
    for (i = 0; i < new_capacity; i++) {
        new_items[i] = NULL;
    }
    if (array->items != NULL) {
        for (i = 0; i < array->count; i++) {
            new_items[i] = _Take(array->items[i]);
        }
    }
    parson_free(_Take(array->items));
    array->items = new_items;
    array->capacity = new_capacity;
    return JSONSuccess;
}

static void json_array_free(JSON_Array *_Nullable _Owned array) {
    if (array == NULL) {
        return;
    }
    json_array_clear(array);
    parson_free(_Take(array->items));
    parson_free(array);
}

/* JSON Value */
static JSON_Value *_Nullable json_value_init_string_no_copy(char *_Owned __counted_by(size) string, size_t size) {
    JSON_Value *_Nullable _Owned new_value = (JSON_Value*)parson_malloc(sizeof(JSON_Value));
    if (!new_value) {
        parson_free(string);
        return NULL;
    }
    new_value->parent = NULL;
    /* unsafe: sets the tag and then the whole member; Safe C has no way to
     * move an owned, counted pointer into a struct inside a tagged union (PORT.md) */
    __unsafe {
        new_value->type = JSONString;
        new_value->value.string.chars = string;
        new_value->value.string.size = size;
    }
    return new_value;
}

/* Parser */
/* length is set to the number of characters between the quotes */
static JSON_Status skip_quotes(const char **string, size_t *length _Noescape) {
    *length = 0;
    if (**string != '\"') {
        return JSONFailure;
    }
    SKIP_CHAR(string);
    while (**string != '\"') {
        if (**string == '\0') {
            return JSONFailure;
        } else if (**string == '\\') {
            SKIP_CHAR(string);
            (*length)++;
            if (**string == '\0') {
                return JSONFailure;
            }
        }
        SKIP_CHAR(string);
        (*length)++;
    }
    SKIP_CHAR(string);
    return JSONSuccess;
}

static JSON_Status parse_utf16(const char **unprocessed, char *__counted_by(output_size) output, size_t output_size, size_t *output_pos) {
    unsigned int cp, lead, trail;
    char *__bidi_indexable processed_ptr = output + *output_pos;
    const char *unprocessed_ptr = *unprocessed;
    JSON_Status status = JSONFailure;
    unprocessed_ptr++; /* skips u */
    status = parse_utf16_hex(unprocessed_ptr, &cp);
    if (status != JSONSuccess) {
        return JSONFailure;
    }
    if (cp < 0x80) {
        processed_ptr[0] = (char)cp; /* 0xxxxxxx */
    } else if (cp < 0x800) {
        processed_ptr[0] = ((cp >> 6) & 0x1F) | 0xC0; /* 110xxxxx */
        processed_ptr[1] = ((cp)      & 0x3F) | 0x80; /* 10xxxxxx */
        *output_pos += 1;
    } else if (cp < 0xD800 || cp > 0xDFFF) {
        processed_ptr[0] = ((cp >> 12) & 0x0F) | 0xE0; /* 1110xxxx */
        processed_ptr[1] = ((cp >> 6)  & 0x3F) | 0x80; /* 10xxxxxx */
        processed_ptr[2] = ((cp)       & 0x3F) | 0x80; /* 10xxxxxx */
        *output_pos += 2;
    } else if (cp >= 0xD800 && cp <= 0xDBFF) { /* lead surrogate (0xD800..0xDBFF) */
        lead = cp;
        skip_chars(&unprocessed_ptr, 4); /* should always be within the buffer, otherwise previous sscanf would fail */
        if (*unprocessed_ptr++ != '\\' || *unprocessed_ptr++ != 'u') {
            return JSONFailure;
        }
        status = parse_utf16_hex(unprocessed_ptr, &trail);
        if (status != JSONSuccess || trail < 0xDC00 || trail > 0xDFFF) { /* valid trail surrogate? (0xDC00..0xDFFF) */
            return JSONFailure;
        }
        cp = ((((lead - 0xD800) & 0x3FF) << 10) | ((trail - 0xDC00) & 0x3FF)) + 0x010000;
        processed_ptr[0] = (((cp >> 18) & 0x07) | 0xF0); /* 11110xxx */
        processed_ptr[1] = (((cp >> 12) & 0x3F) | 0x80); /* 10xxxxxx */
        processed_ptr[2] = (((cp >> 6)  & 0x3F) | 0x80); /* 10xxxxxx */
        processed_ptr[3] = (((cp)       & 0x3F) | 0x80); /* 10xxxxxx */
        *output_pos += 3;
    } else { /* trail surrogate before lead surrogate */
        return JSONFailure;
    }
    skip_chars(&unprocessed_ptr, 3);
    *unprocessed = unprocessed_ptr;
    return JSONSuccess;
}


/* Copies and processes passed string up to supplied length.
Example: "\u006Corem ipsum" -> lorem ipsum */
static char *_Nullable __bidi_indexable process_string(const char *input _Noescape, size_t input_len, size_t *output_len _Noescape) {
    const char *input_ptr = input;
    size_t initial_size = (input_len + 1) * sizeof(char);
    size_t final_size = 0;
    size_t output_pos = 0;
    char *_Nullable _Owned output = NULL;
    char *_Nullable _Owned resized_output = NULL;
    output = (char*)parson_malloc(initial_size);
    if (output == NULL) {
        goto error;
    }
    while ((*input_ptr != '\0') && (size_t)(input_ptr - input) < input_len) {
        if (*input_ptr == '\\') {
            input_ptr++;
            switch (*input_ptr) {
                case '\"': output[output_pos] = '\"'; break;
                case '\\': output[output_pos] = '\\'; break;
                case '/':  output[output_pos] = '/';  break;
                case 'b':  output[output_pos] = '\b'; break;
                case 'f':  output[output_pos] = '\f'; break;
                case 'n':  output[output_pos] = '\n'; break;
                case 'r':  output[output_pos] = '\r'; break;
                case 't':  output[output_pos] = '\t'; break;
                case 'u':
                    if (parse_utf16(&input_ptr, output, initial_size, &output_pos) != JSONSuccess) {
                        goto error;
                    }
                    break;
                default:
                    goto error;
            }
        } else if ((unsigned char)*input_ptr < 0x20) {
            goto error; /* 0x00-0x19 are invalid characters for json string (http://www.ietf.org/rfc/rfc4627.txt) */
        } else {
            output[output_pos] = *input_ptr;
        }
        output_pos++;
        input_ptr++;
    }
    output[output_pos] = '\0';
    /* resize to new length */
    final_size = output_pos + 1;
    /* todo: don't resize if final_size == initial_size */
    resized_output = (char*)parson_malloc(final_size);
    if (resized_output == NULL) {
        goto error;
    }
    memcpy(resized_output, output, final_size);
    *output_len = final_size - 1;
    parson_free(output);
    return resized_output;
error:
    parson_free(output);
    return NULL;
}

/* Return processed contents of a string between quotes and
   skips passed argument to a matching quote. */
static char *_Nullable __bidi_indexable get_quoted_string(const char **string, size_t *output_string_len _Noescape) {
    const char *string_start = *string;
    size_t input_string_len = 0;
    JSON_Status status = skip_quotes(string, &input_string_len);
    if (status != JSONSuccess) {
        return NULL;
    }
    string_start++; /* skips the quote */
    return process_string(string_start, input_string_len, output_string_len);
}

/* Like get_quoted_string, for keys: we do not support key names with embedded \0 chars */
static char *_Nullable __null_terminated get_quoted_key(const char **string) {
    size_t length = 0;
    char *_Nullable _Owned key = get_quoted_string(string, &length);
    if (key == NULL) {
        return NULL;
    }
    if (memchr(key, '\0', length) != NULL) {
        parson_free(key);
        return NULL;
    }
    /* unsafe: process_string put the terminator at key[length] */
    __unsafe { return __unsafe_null_terminated_from_indexable(key, key + length); }
}

static JSON_Value *_Nullable parse_value(const char **string, size_t nesting) {
    if (nesting > MAX_NESTING) {
        return NULL;
    }
    SKIP_WHITESPACES(string);
    switch (**string) {
        case '{':
            return parse_object_value(string, nesting + 1);
        case '[':
            return parse_array_value(string, nesting + 1);
        case '\"':
            return parse_string_value(string);
        case 'f': case 't':
            return parse_boolean_value(string);
        case '-':
        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9':
            return parse_number_value(string);
        case 'n':
            return parse_null_value(string);
        default:
            return NULL;
    }
}

static JSON_Value *_Nullable parse_object_value(const char **string, size_t nesting) {
    JSON_Value *_Nullable _Owned output_value = NULL;
    JSON_Object *_Nullable output_object = NULL;

    output_value = json_value_init_object();
    output_object = json_value_get_object(output_value);
    if (output_object == NULL) {
        json_value_free(output_value);
        return NULL;
    }
    if (**string != '{') {
        json_value_free(output_value);
        return NULL;
    }
    SKIP_CHAR(string);
    SKIP_WHITESPACES(string);
    if (**string == '}') { /* empty object */
        SKIP_CHAR(string);
        return output_value;
    }
    while (**string != '\0') {
        char *_Nullable _Owned __null_terminated new_key = get_quoted_key(string);
        JSON_Value *_Nullable _Owned new_value = NULL;
        if (!new_key) {
            json_value_free(output_value);
            return NULL;
        }
        SKIP_WHITESPACES(string);
        if (**string != ':') {
            parson_free(new_key);
            json_value_free(output_value);
            return NULL;
        }
        SKIP_CHAR(string);
        new_value = parse_value(string, nesting);
        if (new_value == NULL) {
            parson_free(new_key);
            json_value_free(output_value);
            return NULL;
        }
        if (json_object_add(output_object, new_key, new_value) != JSONSuccess) {
            json_value_free(output_value);
            return NULL;
        }
        SKIP_WHITESPACES(string);
        if (**string != ',') {
            break;
        }
        SKIP_CHAR(string);
        SKIP_WHITESPACES(string);
        if (**string == '}') {
            break;
        }
    }
    SKIP_WHITESPACES(string);
    if (**string != '}') {
        json_value_free(output_value);
        return NULL;
    }
    SKIP_CHAR(string);
    return output_value;
}

static JSON_Value *_Nullable parse_array_value(const char **string, size_t nesting) {
    JSON_Value *_Nullable _Owned output_value = NULL;
    JSON_Array *_Nullable output_array = NULL;
    output_value = json_value_init_array();
    output_array = json_value_get_array(output_value);
    if (output_array == NULL) {
        json_value_free(output_value);
        return NULL;
    }
    if (**string != '[') {
        json_value_free(output_value);
        return NULL;
    }
    SKIP_CHAR(string);
    SKIP_WHITESPACES(string);
    if (**string == ']') { /* empty array */
        SKIP_CHAR(string);
        return output_value;
    }
    while (**string != '\0') {
        JSON_Value *_Nullable _Owned new_array_value = parse_value(string, nesting);
        if (new_array_value == NULL) {
            json_value_free(output_value);
            return NULL;
        }
        if (json_array_add(output_array, new_array_value) != JSONSuccess) {
            json_value_free(output_value);
            return NULL;
        }
        SKIP_WHITESPACES(string);
        if (**string != ',') {
            break;
        }
        SKIP_CHAR(string);
        SKIP_WHITESPACES(string);
        if (**string == ']') {
            break;
        }
    }
    SKIP_WHITESPACES(string);
    if (**string != ']' || /* Trim array after parsing is over */
        json_array_resize(output_array, json_array_get_count(output_array)) != JSONSuccess) {
            json_value_free(output_value);
            return NULL;
    }
    SKIP_CHAR(string);
    return output_value;
}

static JSON_Value *_Nullable parse_string_value(const char **string) {
    size_t new_string_len = 0;
    char *_Nullable _Owned new_string = get_quoted_string(string, &new_string_len);
    if (new_string == NULL) {
        return NULL;
    }
    return json_value_init_string_no_copy(new_string, new_string_len + 1);
}

static JSON_Value *_Nullable parse_boolean_value(const char **string) {
    size_t true_token_size = SIZEOF_TOKEN("true");
    size_t false_token_size = SIZEOF_TOKEN("false");
    if (strncmp("true", *string, true_token_size) == 0) {
        skip_chars(string, true_token_size);
        return json_value_init_boolean(1);
    } else if (strncmp("false", *string, false_token_size) == 0) {
        skip_chars(string, false_token_size);
        return json_value_init_boolean(0);
    }
    return NULL;
}

static JSON_Value *_Nullable parse_number_value(const char **string) {
    char *__null_terminated end;
    double number = 0;
    errno = 0;
    number = strtod(*string, &end);
    if (errno == ERANGE && (number <= -HUGE_VAL || number >= HUGE_VAL)) {
        return NULL;
    }
    if ((errno && errno != ERANGE) || !is_decimal(*string, end - *string)) {
        return NULL;
    }
    *string = end;
    return json_value_init_number(number);
}

static JSON_Value *_Nullable parse_null_value(const char **string) {
    size_t token_size = SIZEOF_TOKEN("null");
    if (strncmp("null", *string, token_size) == 0) {
        skip_chars(string, token_size);
        return json_value_init_null();
    }
    return NULL;
}

/* Serialization */

static void json_writer_append(JSON_Writer *writer, const char *__counted_by(len) chars _Noescape, size_t len) {
    if (writer->buf != NULL) {
        memcpy(writer->buf + writer->pos, chars, len);
    }
    writer->pos += len;
}

/*  APPEND_STRING() is only called on string literals. */
#define APPEND_STRING(str) json_writer_append(writer, (str), SIZEOF_TOKEN((str)))

#define APPEND_INDENT(level) do {\
                                int level_i = 0;\
                                for (level_i = 0; level_i < (level); level_i++) {\
                                    APPEND_STRING(PARSON_INDENT_STR);\
                                }\
                            } while (0)

static JSON_Status json_serialize_number(double num, JSON_Writer *writer) {
    char num_buf[PARSON_NUM_BUF_SIZE];
    int written = -1;
    if (parson_number_serialization_function) {
        written = parson_number_serialization_function(num, num_buf);
    } else {
        const char *_Nullable __null_terminated float_format = parson_float_format;
        /* unsafe: the format is the user's, for one double, which the compiler
         * cannot check against the argument */
        __unsafe { written = snprintf(num_buf, sizeof(num_buf), float_format ? float_format : PARSON_DEFAULT_FLOAT_FORMAT, num); }
    }
    if (written < 0 || (size_t)written >= sizeof(num_buf)) {
        return JSONFailure;
    }
    json_writer_append(writer, num_buf, written);
    return JSONSuccess;
}

static JSON_Status json_serialize_r(const JSON_Value *_Nullable value, JSON_Writer *writer, int level, parson_bool_t is_pretty)
{
    const char *_Nullable __null_terminated key = NULL;
    JSON_Chars string;
    JSON_Array *_Nullable array = NULL;
    JSON_Object *_Nullable object = NULL;
    size_t i = 0, count = 0;

    switch (json_value_get_type(value)) {
        case JSONArray:
            array = json_value_get_array(value);
            count = json_array_get_count(array);
            APPEND_STRING("[");
            if (count > 0 && is_pretty) {
                APPEND_STRING("\n");
            }
            for (i = 0; i < count; i++) {
                if (is_pretty) {
                    APPEND_INDENT(level+1);
                }
                if (json_serialize_r(json_array_get_value(array, i), writer, level+1, is_pretty) != JSONSuccess) {
                    return JSONFailure;
                }
                if (i < (count - 1)) {
                    APPEND_STRING(",");
                }
                if (is_pretty) {
                    APPEND_STRING("\n");
                }
            }
            if (count > 0 && is_pretty) {
                APPEND_INDENT(level);
            }
            APPEND_STRING("]");
            return JSONSuccess;
        case JSONObject:
            object = json_value_get_object(value);
            count  = json_object_get_count(object);
            APPEND_STRING("{");
            if (count > 0 && is_pretty) {
                APPEND_STRING("\n");
            }
            for (i = 0; i < count; i++) {
                key = json_object_get_name(object, i);
                if (key == NULL) {
                    return JSONFailure;
                }
                if (is_pretty) {
                    APPEND_INDENT(level+1);
                }
                /* We do not support key names with embedded \0 chars */
                json_serialize_string(__null_terminated_to_indexable(key), strlen(key), writer);
                APPEND_STRING(":");
                if (is_pretty) {
                    APPEND_STRING(" ");
                }
                if (json_serialize_r(json_object_get_value_at(object, i), writer, level+1, is_pretty) != JSONSuccess) {
                    return JSONFailure;
                }
                if (i < (count - 1)) {
                    APPEND_STRING(",");
                }
                if (is_pretty) {
                    APPEND_STRING("\n");
                }
            }
            if (count > 0 && is_pretty) {
                APPEND_INDENT(level);
            }
            APPEND_STRING("}");
            return JSONSuccess;
        case JSONString:
            string = json_value_get_chars(value);
            if (string.p == NULL) {
                return JSONFailure;
            }
            json_serialize_string(string.p, string.len, writer);
            return JSONSuccess;
        case JSONBoolean:
            if (json_value_get_boolean(value)) {
                APPEND_STRING("true");
            } else {
                APPEND_STRING("false");
            }
            return JSONSuccess;
        case JSONNumber:
            return json_serialize_number(json_value_get_number(value), writer);
        case JSONNull:
            APPEND_STRING("null");
            return JSONSuccess;
        case JSONError:
            return JSONFailure;
        default:
            return JSONFailure;
    }
}

static void json_serialize_string(const char *__counted_by(len) string _Noescape, size_t len, JSON_Writer *writer) {
    size_t i = 0;
    char c = '\0';
    APPEND_STRING("\"");
    for (i = 0; i < len; i++) {
        c = string[i];
        switch (c) {
            case '\"': APPEND_STRING("\\\""); break;
            case '\\': APPEND_STRING("\\\\"); break;
            case '\b': APPEND_STRING("\\b"); break;
            case '\f': APPEND_STRING("\\f"); break;
            case '\n': APPEND_STRING("\\n"); break;
            case '\r': APPEND_STRING("\\r"); break;
            case '\t': APPEND_STRING("\\t"); break;
            case '\x00': APPEND_STRING("\\u0000"); break;
            case '\x01': APPEND_STRING("\\u0001"); break;
            case '\x02': APPEND_STRING("\\u0002"); break;
            case '\x03': APPEND_STRING("\\u0003"); break;
            case '\x04': APPEND_STRING("\\u0004"); break;
            case '\x05': APPEND_STRING("\\u0005"); break;
            case '\x06': APPEND_STRING("\\u0006"); break;
            case '\x07': APPEND_STRING("\\u0007"); break;
            /* '\x08' duplicate: '\b' */
            /* '\x09' duplicate: '\t' */
            /* '\x0a' duplicate: '\n' */
            case '\x0b': APPEND_STRING("\\u000b"); break;
            /* '\x0c' duplicate: '\f' */
            /* '\x0d' duplicate: '\r' */
            case '\x0e': APPEND_STRING("\\u000e"); break;
            case '\x0f': APPEND_STRING("\\u000f"); break;
            case '\x10': APPEND_STRING("\\u0010"); break;
            case '\x11': APPEND_STRING("\\u0011"); break;
            case '\x12': APPEND_STRING("\\u0012"); break;
            case '\x13': APPEND_STRING("\\u0013"); break;
            case '\x14': APPEND_STRING("\\u0014"); break;
            case '\x15': APPEND_STRING("\\u0015"); break;
            case '\x16': APPEND_STRING("\\u0016"); break;
            case '\x17': APPEND_STRING("\\u0017"); break;
            case '\x18': APPEND_STRING("\\u0018"); break;
            case '\x19': APPEND_STRING("\\u0019"); break;
            case '\x1a': APPEND_STRING("\\u001a"); break;
            case '\x1b': APPEND_STRING("\\u001b"); break;
            case '\x1c': APPEND_STRING("\\u001c"); break;
            case '\x1d': APPEND_STRING("\\u001d"); break;
            case '\x1e': APPEND_STRING("\\u001e"); break;
            case '\x1f': APPEND_STRING("\\u001f"); break;
            case '/':
                if (parson_escape_slashes) {
                    APPEND_STRING("\\/");  /* to make json embeddable in xml\/html */
                } else {
                    APPEND_STRING("/");
                }
                break;
            default:
                if (writer->buf != NULL) {
                    writer->buf[writer->pos] = c;
                }
                writer->pos++;
                break;
        }
    }
    APPEND_STRING("\"");
}

#undef APPEND_STRING
#undef APPEND_INDENT

/* Parser API */
 JSON_Value *_Nullable json_parse_file(const char *filename _Noescape) {
    char *_Nullable _Owned __null_terminated file_contents = read_file(filename);
    JSON_Value *_Nullable _Owned output_value = NULL;
    if (file_contents == NULL) {
        return NULL;
    }
    output_value = json_parse_string(file_contents);
    parson_free(file_contents);
    return output_value;
}

 JSON_Value *_Nullable json_parse_file_with_comments(const char *filename _Noescape) {
    char *_Nullable _Owned __null_terminated file_contents = read_file(filename);
    JSON_Value *_Nullable _Owned output_value = NULL;
    if (file_contents == NULL) {
        return NULL;
    }
    output_value = json_parse_string_with_comments(file_contents);
    parson_free(file_contents);
    return output_value;
}

 JSON_Value *_Nullable json_parse_string(const char *_Nullable string _Noescape) {
    const char *_Nullable after_bom = string;
    size_t i = 0;
    if (string == NULL || after_bom == NULL) {
        return NULL;
    }
    for (i = 0; i < 3 && *after_bom == "\xEF\xBB\xBF"[i]; i++) {
        after_bom++;
    }
    if (i == 3) {
        string = after_bom; /* Support for UTF-8 BOM */
    }
    return parse_value(&string, 0);
}

 JSON_Value *_Nullable json_parse_string_with_comments(const char *_Nullable string _Noescape) {
    JSON_Value *_Nullable _Owned result = NULL;
    size_t length = 0;
    char *_Nullable _Owned string_mutable_copy = NULL;
    if (string == NULL) {
        return NULL;
    }
    length = strlen(string);
    string_mutable_copy = (char*)parson_malloc(length + 1);
    if (string_mutable_copy == NULL) {
        return NULL;
    }
    memcpy(string_mutable_copy, __null_terminated_to_indexable(string), length);
    string_mutable_copy[length] = '\0';
    remove_comments(string_mutable_copy, length, "/*", "*/");
    remove_comments(string_mutable_copy, length, "//", "\n");
    /* unsafe: remove_comments does not write the terminator at [length] */
    __unsafe {
        const char *__null_terminated string_ptr = __unsafe_null_terminated_from_indexable(string_mutable_copy, string_mutable_copy + length);
        result = parse_value(&string_ptr, 0);
    }
    parson_free(string_mutable_copy);
    return result;
}

/* JSON Object API */

JSON_Value *_Nullable json_object_get_value(const JSON_Object *_Nullable object, const char *_Nullable name _Noescape) {
    if (object == NULL || name == NULL) {
        return NULL;
    }
    return json_object_getn_value(object, name, strlen(name));
}

const char *_Nullable json_object_get_string(const JSON_Object *_Nullable object, const char *_Nullable name _Noescape) {
    return json_value_get_string(json_object_get_value(object, name));
}

size_t json_object_get_string_len(const JSON_Object *_Nullable object, const char *_Nullable name _Noescape) {
    return json_value_get_string_len(json_object_get_value(object, name));
}

double json_object_get_number(const JSON_Object *_Nullable object, const char *_Nullable name _Noescape) {
    return json_value_get_number(json_object_get_value(object, name));
}

JSON_Object *_Nullable json_object_get_object(const JSON_Object *_Nullable object, const char *_Nullable name _Noescape) {
    return json_value_get_object(json_object_get_value(object, name));
}

JSON_Array *_Nullable json_object_get_array(const JSON_Object *_Nullable object, const char *_Nullable name _Noescape) {
    return json_value_get_array(json_object_get_value(object, name));
}

int json_object_get_boolean(const JSON_Object *_Nullable object, const char *_Nullable name _Noescape) {
    return json_value_get_boolean(json_object_get_value(object, name));
}

JSON_Value *_Nullable json_object_dotget_value(const JSON_Object *_Nullable object, const char *name _Noescape) {
    const char *_Nullable __null_terminated dot_position = strchr(name, '.');
    if (!dot_position) {
        return json_object_get_value(object, name);
    }
    object = json_value_get_object(json_object_getn_value(object, name, dot_position - name));
    return json_object_dotget_value(object, dot_position + 1);
}

const char *_Nullable json_object_dotget_string(const JSON_Object *_Nullable object, const char *name _Noescape) {
    return json_value_get_string(json_object_dotget_value(object, name));
}

size_t json_object_dotget_string_len(const JSON_Object *_Nullable object, const char *name _Noescape) {
    return json_value_get_string_len(json_object_dotget_value(object, name));
}

double json_object_dotget_number(const JSON_Object *_Nullable object, const char *name _Noescape) {
    return json_value_get_number(json_object_dotget_value(object, name));
}

JSON_Object *_Nullable json_object_dotget_object(const JSON_Object *_Nullable object, const char *name _Noescape) {
    return json_value_get_object(json_object_dotget_value(object, name));
}

JSON_Array *_Nullable json_object_dotget_array(const JSON_Object *_Nullable object, const char *name _Noescape) {
    return json_value_get_array(json_object_dotget_value(object, name));
}

int json_object_dotget_boolean(const JSON_Object *_Nullable object, const char *name _Noescape) {
    return json_value_get_boolean(json_object_dotget_value(object, name));
}

/* These two take a non-null object: clang crashes on o->names[i] when o is _Nullable (PORT.md) */
static const char *_Nullable json_object_name_at(const JSON_Object *object, size_t index) {
    if (index >= object->count || object->names == NULL) {
        return NULL;
    }
    return object->names[index];
}

static JSON_Value *_Nullable json_object_value_at(const JSON_Object *object, size_t index) {
    if (index >= object->count || object->values == NULL) {
        return NULL;
    }
    return object->values[index];
}

static JSON_Value *_Nullable json_array_value_at(const JSON_Array *array, size_t index) {
    if (index >= array->count || array->items == NULL) {
        return NULL;
    }
    return array->items[index];
}

size_t json_object_get_count(const JSON_Object *_Nullable object) {
    return object ? object->count : 0;
}

const char  *_Nullable json_object_get_name(const JSON_Object *_Nullable object, size_t index) {
    if (object == NULL) {
        return NULL;
    }
    return json_object_name_at(object, index);
}

JSON_Value  *_Nullable json_object_get_value_at(const JSON_Object *_Nullable object, size_t index) {
    if (object == NULL) {
        return NULL;
    }
    return json_object_value_at(object, index);
}

JSON_Value *_Nullable json_object_get_wrapping_value(const JSON_Object *_Nullable object) {
    if (!object) {
        return NULL;
    }
    /* unsafe: the wrapping value owns object, so it is alive as long as object is */
    __unsafe { return object->wrapping_value; }
}

int json_object_has_value (const JSON_Object *_Nullable object, const char *_Nullable name _Noescape) {
    return json_object_get_value(object, name) != NULL;
}

int json_object_has_value_of_type(const JSON_Object *_Nullable object, const char *_Nullable name _Noescape, JSON_Value_Type type) {
    JSON_Value *val = json_object_get_value(object, name);
    return val != NULL && json_value_get_type(val) == type;
}

int json_object_dothas_value (const JSON_Object *_Nullable object, const char *name _Noescape) {
    return json_object_dotget_value(object, name) != NULL;
}

int json_object_dothas_value_of_type(const JSON_Object *_Nullable object, const char *name _Noescape, JSON_Value_Type type) {
    JSON_Value *val = json_object_dotget_value(object, name);
    return val != NULL && json_value_get_type(val) == type;
}

/* JSON Array API */
JSON_Value  *_Nullable json_array_get_value(const JSON_Array *_Nullable array, size_t index) {
    if (array == NULL) {
        return NULL;
    }
    return json_array_value_at(array, index);
}

const char *_Nullable json_array_get_string(const JSON_Array *_Nullable array, size_t index) {
    return json_value_get_string(json_array_get_value(array, index));
}

size_t json_array_get_string_len(const JSON_Array *_Nullable array, size_t index) {
    return json_value_get_string_len(json_array_get_value(array, index));
}

double json_array_get_number(const JSON_Array *_Nullable array, size_t index) {
    return json_value_get_number(json_array_get_value(array, index));
}

JSON_Object *_Nullable json_array_get_object(const JSON_Array *_Nullable array, size_t index) {
    return json_value_get_object(json_array_get_value(array, index));
}

JSON_Array *_Nullable json_array_get_array(const JSON_Array *_Nullable array, size_t index) {
    return json_value_get_array(json_array_get_value(array, index));
}

int json_array_get_boolean(const JSON_Array *_Nullable array, size_t index) {
    return json_value_get_boolean(json_array_get_value(array, index));
}

size_t json_array_get_count(const JSON_Array *_Nullable array) {
    return array ? array->count : 0;
}

JSON_Value *_Nullable json_array_get_wrapping_value(const JSON_Array *_Nullable array) {
    if (!array) {
        return NULL;
    }
    /* unsafe: the wrapping value owns array, so it is alive as long as array is */
    __unsafe { return array->wrapping_value; }
}

/* JSON Value API */
JSON_Value_Type json_value_get_type(const JSON_Value *_Nullable value) {
    return value ? value->type : JSONError;
}

JSON_Object *_Nullable json_value_get_object(const JSON_Value *_Nullable value) {
    return value && value->type == JSONObject ? value->value.object : NULL;
}

JSON_Array  *_Nullable json_value_get_array(const JSON_Value *_Nullable value) {
    return value && value->type == JSONArray ? value->value.array : NULL;
}

static JSON_Chars json_value_get_chars(const JSON_Value *_Nullable value) {
    JSON_Chars chars = { NULL, 0 };
    if (value && value->type == JSONString && value->value.string.chars) {
        chars.p = value->value.string.chars;
        chars.len = value->value.string.size - 1;
    }
    return chars;
}

const char  *_Nullable json_value_get_string(const JSON_Value *_Nullable value) {
    JSON_Chars chars = json_value_get_chars(value);
    if (chars.p == NULL) {
        return NULL;
    }
    /* unsafe: a JSON_String has its terminator right after its characters, and nothing writes them once it is made */
    __unsafe { return __unsafe_forge_null_terminated(const char *, chars.p); }
}

size_t json_value_get_string_len(const JSON_Value *_Nullable value) {
    return json_value_get_chars(value).len;
}

double json_value_get_number(const JSON_Value *_Nullable value) {
    return value && value->type == JSONNumber ? value->value.number : 0;
}

int json_value_get_boolean(const JSON_Value *_Nullable value) {
    return value && value->type == JSONBoolean ? value->value.boolean : -1;
}

JSON_Value *_Nullable json_value_get_parent (const JSON_Value *_Nullable value) {
    /* unsafe: the parent owns value, so it is alive as long as value is in it */
    __unsafe { return value ? value->parent : NULL; }
}

void json_value_free(JSON_Value *_Nullable _Owned value) {
    if (value == NULL) {
        return;
    }
    switch (value->type) {
        case JSONObject:
            json_object_free(_Take(value->value.object));
            break;
        case JSONString:
            parson_free(_Take(value->value.string.chars));
            break;
        case JSONArray:
            json_array_free(_Take(value->value.array));
            break;
        default:
            break;
    }
    parson_free(value);
}

 JSON_Value *_Nullable json_value_init_object(void) {
    JSON_Value *_Nullable _Owned new_value = (JSON_Value*)parson_malloc(sizeof(JSON_Value));
    JSON_Object *_Nullable _Owned object = NULL;
    if (!new_value) {
        return NULL;
    }
    object = json_object_make(new_value);
    if (!object) {
        parson_free(new_value);
        return NULL;
    }
    new_value->parent = NULL;
    new_value->type = JSONObject;
    new_value->value.object = object;
    return new_value;
}

 JSON_Value *_Nullable json_value_init_array(void) {
    JSON_Value *_Nullable _Owned new_value = (JSON_Value*)parson_malloc(sizeof(JSON_Value));
    JSON_Array *_Nullable _Owned array = NULL;
    if (!new_value) {
        return NULL;
    }
    array = json_array_make(new_value);
    if (!array) {
        parson_free(new_value);
        return NULL;
    }
    new_value->parent = NULL;
    new_value->type = JSONArray;
    new_value->value.array = array;
    return new_value;
}

 JSON_Value *_Nullable json_value_init_string(const char *_Nullable string _Noescape) {
    size_t length = 0;
    if (string == NULL) {
        return NULL;
    }
    length = strlen(string);
    return json_value_init_string_with_len(__null_terminated_to_indexable(string), length);
}

 JSON_Value *_Nullable json_value_init_string_with_len(const char *_Nullable __counted_by_or_null(length) string, size_t length) {
    char *_Nullable _Owned copy = NULL;
    if (string == NULL) {
        return NULL;
    }
    if (!is_valid_utf8(string, length)) {
        return NULL;
    }
    copy = (char*)parson_malloc(length + 1);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, string, length);
    copy[length] = '\0';
    return json_value_init_string_no_copy(copy, length + 1);
}

 JSON_Value *_Nullable json_value_init_number(double number) {
    JSON_Value *new_value = NULL;
    if (IS_NUMBER_INVALID(number)) {
        return NULL;
    }
    new_value = (JSON_Value*)parson_malloc(sizeof(JSON_Value));
    if (new_value == NULL) {
        return NULL;
    }
    new_value->parent = NULL;
    new_value->type = JSONNumber;
    new_value->value.number = number;
    return new_value;
}

 JSON_Value *_Nullable json_value_init_boolean(int boolean) {
    JSON_Value *new_value = (JSON_Value*)parson_malloc(sizeof(JSON_Value));
    if (!new_value) {
        return NULL;
    }
    new_value->parent = NULL;
    new_value->type = JSONBoolean;
    new_value->value.boolean = boolean ? 1 : 0;
    return new_value;
}

 JSON_Value *_Nullable json_value_init_null(void) {
    JSON_Value *new_value = (JSON_Value*)parson_malloc(sizeof(JSON_Value));
    if (!new_value) {
        return NULL;
    }
    new_value->parent = NULL;
    new_value->type = JSONNull;
    new_value->value.null = 0;
    return new_value;
}

 JSON_Value *_Nullable json_value_deep_copy(const JSON_Value *_Nullable value) {
    size_t i = 0;
    JSON_Value *_Nullable _Owned return_value = NULL;
    JSON_Chars temp_string;
    const char *_Nullable __null_terminated temp_key = NULL;
    char *_Nullable _Owned temp_string_copy = NULL;
    JSON_Array *_Nullable temp_array = NULL, *_Nullable temp_array_copy = NULL;
    JSON_Object *_Nullable temp_object = NULL, *_Nullable temp_object_copy = NULL;

    switch (json_value_get_type(value)) {
        case JSONArray:
            temp_array = json_value_get_array(value);
            return_value = json_value_init_array();
            temp_array_copy = json_value_get_array(return_value);
            if (temp_array_copy == NULL) {
                json_value_free(return_value);
                return NULL;
            }
            for (i = 0; i < json_array_get_count(temp_array); i++) {
                JSON_Value *_Nullable _Owned item_copy = json_value_deep_copy(json_array_get_value(temp_array, i));
                if (item_copy == NULL) {
                    json_value_free(return_value);
                    return NULL;
                }
                if (json_array_add(temp_array_copy, item_copy) != JSONSuccess) {
                    json_value_free(return_value);
                    return NULL;
                }
            }
            return return_value;
        case JSONObject:
            temp_object = json_value_get_object(value);
            return_value = json_value_init_object();
            temp_object_copy = json_value_get_object(return_value);
            if (temp_object_copy == NULL) {
                json_value_free(return_value);
                return NULL;
            }
            for (i = 0; i < json_object_get_count(temp_object); i++) {
                JSON_Value *_Nullable _Owned member_copy = NULL;
                char *_Nullable _Owned __null_terminated key_copy = NULL;
                temp_key = json_object_get_name(temp_object, i);
                if (temp_key == NULL) {
                    json_value_free(return_value);
                    return NULL;
                }
                member_copy = json_value_deep_copy(json_object_get_value(temp_object, temp_key));
                if (!member_copy) {
                    json_value_free(return_value);
                    return NULL;
                }
                key_copy = parson_strdup(temp_key);
                if (!key_copy) {
                    json_value_free(member_copy);
                    json_value_free(return_value);
                    return NULL;
                }
                if (json_object_add(temp_object_copy, key_copy, member_copy) != JSONSuccess) {
                    json_value_free(return_value);
                    return NULL;
                }
            }
            return return_value;
        case JSONBoolean:
            return json_value_init_boolean(json_value_get_boolean(value));
        case JSONNumber:
            return json_value_init_number(json_value_get_number(value));
        case JSONString:
            temp_string = json_value_get_chars(value);
            if (temp_string.p == NULL) {
                return NULL;
            }
            temp_string_copy = (char*)parson_malloc(temp_string.len + 1);
            if (temp_string_copy == NULL) {
                return NULL;
            }
            memcpy(temp_string_copy, temp_string.p, temp_string.len);
            temp_string_copy[temp_string.len] = '\0';
            return json_value_init_string_no_copy(temp_string_copy, temp_string.len + 1);
        case JSONNull:
            return json_value_init_null();
        case JSONError:
            return NULL;
        default:
            return NULL;
    }
}

/* buf is null to compute the size only; the size counts the terminator */
static JSON_Status json_serialize(const JSON_Value *_Nullable value, char *_Nullable __counted_by_or_null(buf_size) buf, size_t buf_size, parson_bool_t is_pretty, size_t *out_size) {
    JSON_Writer writer = { buf, buf_size, 0 };
    if (json_serialize_r(value, &writer, 0, is_pretty) != JSONSuccess) {
        return JSONFailure;
    }
    if (writer.buf != NULL) {
        writer.buf[writer.pos] = '\0';
    }
    *out_size = writer.pos + 1;
    return JSONSuccess;
}

size_t json_serialization_size(const JSON_Value *_Nullable value) {
    size_t size = 0;
    return json_serialize(value, NULL, 0, PARSON_FALSE, &size) == JSONSuccess ? size : 0;
}

JSON_Status json_serialize_to_buffer(const JSON_Value *_Nullable value, char *__counted_by(buf_size_in_bytes) buf, size_t buf_size_in_bytes) {
    size_t needed_size_in_bytes = json_serialization_size(value);
    if (needed_size_in_bytes == 0 || buf_size_in_bytes < needed_size_in_bytes) {
        return JSONFailure;
    }
    return json_serialize(value, buf, buf_size_in_bytes, PARSON_FALSE, &needed_size_in_bytes);
}

JSON_Status json_serialize_to_file(const JSON_Value *_Nullable value, const char *filename _Noescape) {
    JSON_Status return_code = JSONSuccess;
    FILE *_Nullable fp = NULL;
    char *_Nullable _Owned __null_terminated serialized_string = json_serialize_to_string(value);
    if (serialized_string == NULL) {
        return JSONFailure;
    }
    fp = fopen(filename, "w");
    if (fp == NULL) {
        json_free_serialized_string(serialized_string);
        return JSONFailure;
    }
    if (fputs(serialized_string, fp) == EOF) {
        return_code = JSONFailure;
    }
    if (fclose(fp) == EOF) {
        return_code = JSONFailure;
    }
    json_free_serialized_string(serialized_string);
    return return_code;
}

 char *_Nullable __null_terminated json_serialize_to_string(const JSON_Value *_Nullable value) {
    size_t buf_size_bytes = json_serialization_size(value);
    char *_Nullable _Owned buf = NULL;
    if (buf_size_bytes == 0) {
        return NULL;
    }
    buf = (char*)parson_malloc(buf_size_bytes);
    if (buf == NULL) {
        return NULL;
    }
    if (json_serialize_to_buffer(value, buf, buf_size_bytes) != JSONSuccess) {
        parson_free(buf);
        return NULL;
    }
    /* unsafe: json_serialize_to_buffer terminated buf at its last byte */
    __unsafe { return __unsafe_null_terminated_from_indexable(buf, buf + buf_size_bytes - 1); }
}

size_t json_serialization_size_pretty(const JSON_Value *_Nullable value) {
    size_t size = 0;
    return json_serialize(value, NULL, 0, PARSON_TRUE, &size) == JSONSuccess ? size : 0;
}

JSON_Status json_serialize_to_buffer_pretty(const JSON_Value *_Nullable value, char *__counted_by(buf_size_in_bytes) buf, size_t buf_size_in_bytes) {
    size_t needed_size_in_bytes = json_serialization_size_pretty(value);
    if (needed_size_in_bytes == 0 || buf_size_in_bytes < needed_size_in_bytes) {
        return JSONFailure;
    }
    return json_serialize(value, buf, buf_size_in_bytes, PARSON_TRUE, &needed_size_in_bytes);
}

JSON_Status json_serialize_to_file_pretty(const JSON_Value *_Nullable value, const char *filename _Noescape) {
    JSON_Status return_code = JSONSuccess;
    FILE *_Nullable fp = NULL;
    char *_Nullable _Owned __null_terminated serialized_string = json_serialize_to_string_pretty(value);
    if (serialized_string == NULL) {
        return JSONFailure;
    }
    fp = fopen(filename, "w");
    if (fp == NULL) {
        json_free_serialized_string(serialized_string);
        return JSONFailure;
    }
    if (fputs(serialized_string, fp) == EOF) {
        return_code = JSONFailure;
    }
    if (fclose(fp) == EOF) {
        return_code = JSONFailure;
    }
    json_free_serialized_string(serialized_string);
    return return_code;
}

 char *_Nullable __null_terminated json_serialize_to_string_pretty(const JSON_Value *_Nullable value) {
    size_t buf_size_bytes = json_serialization_size_pretty(value);
    char *_Nullable _Owned buf = NULL;
    if (buf_size_bytes == 0) {
        return NULL;
    }
    buf = (char*)parson_malloc(buf_size_bytes);
    if (buf == NULL) {
        return NULL;
    }
    if (json_serialize_to_buffer_pretty(value, buf, buf_size_bytes) != JSONSuccess) {
        parson_free(buf);
        return NULL;
    }
    /* unsafe: json_serialize_to_buffer_pretty terminated buf at its last byte */
    __unsafe { return __unsafe_null_terminated_from_indexable(buf, buf + buf_size_bytes - 1); }
}

void json_free_serialized_string(char *_Nullable _Owned __null_terminated string) {
    parson_free(string);
}

static void json_array_remove_at(JSON_Array * array, size_t ix) {
    size_t i = 0;
    if (array->items == NULL) {
        return;
    }
    json_value_free(_Take(array->items[ix]));
    for (i = ix; i + 1 < array->count; i++) {
        JSON_Value *_Nullable _Owned next = _Take(array->items[i + 1]);
        json_array_store(array, i, next);
    }
    array->count -= 1;
}

JSON_Status json_array_remove(JSON_Array * _Nullable array, size_t ix) {
    if (array == NULL || ix >= array->count) {
        return JSONFailure;
    }
    json_array_remove_at(array, ix);
    return JSONSuccess;
}

/* unsafe: the public functions below take ownership of value only when they
 * succeed, which no annotation can say, so their parameter is a plain borrow.
 * Each calls this after its last failure point; the caller gave value up. */
__unsafe static JSON_Value *adopt_value(const JSON_Value *value) {
    return (JSON_Value*)value;
}

static JSON_Value *_Nullable json_array_take(JSON_Array * array, size_t ix) {
    return array->items ? _Take(array->items[ix]) : NULL;
}

static parson_bool_t has_parent(const JSON_Value *value) {
    return value->parent != NULL;
}

JSON_Status json_array_replace_value(JSON_Array * _Nullable array, size_t ix, JSON_Value *_Nullable value) {
    if (array == NULL || value == NULL || has_parent(value) || ix >= array->count) {
        return JSONFailure;
    }
    json_value_free(json_array_take(array, ix));
    value->parent = array->wrapping_value;
    json_array_store(array, ix, adopt_value(value));
    return JSONSuccess;
}

JSON_Status json_array_replace_string(JSON_Array * _Nullable array, size_t i, const char *_Nullable string _Noescape) {
    JSON_Value *value = json_value_init_string(string);
    if (value == NULL) {
        return JSONFailure;
    }
    if (json_array_replace_value(array, i, value) != JSONSuccess) {
        json_value_free(value);
        return JSONFailure;
    }
    return JSONSuccess;
}

JSON_Status json_array_replace_string_with_len(JSON_Array * _Nullable array, size_t i, const char *_Nullable __counted_by_or_null(len) string, size_t len) {
    JSON_Value *value = json_value_init_string_with_len(string, len);
    if (value == NULL) {
        return JSONFailure;
    }
    if (json_array_replace_value(array, i, value) != JSONSuccess) {
        json_value_free(value);
        return JSONFailure;
    }
    return JSONSuccess;
}

JSON_Status json_array_replace_number(JSON_Array * _Nullable array, size_t i, double number) {
    JSON_Value *value = json_value_init_number(number);
    if (value == NULL) {
        return JSONFailure;
    }
    if (json_array_replace_value(array, i, value) != JSONSuccess) {
        json_value_free(value);
        return JSONFailure;
    }
    return JSONSuccess;
}

JSON_Status json_array_replace_boolean(JSON_Array * _Nullable array, size_t i, int boolean) {
    JSON_Value *value = json_value_init_boolean(boolean);
    if (value == NULL) {
        return JSONFailure;
    }
    if (json_array_replace_value(array, i, value) != JSONSuccess) {
        json_value_free(value);
        return JSONFailure;
    }
    return JSONSuccess;
}

JSON_Status json_array_replace_null(JSON_Array * _Nullable array, size_t i) {
    JSON_Value *value = json_value_init_null();
    if (value == NULL) {
        return JSONFailure;
    }
    if (json_array_replace_value(array, i, value) != JSONSuccess) {
        json_value_free(value);
        return JSONFailure;
    }
    return JSONSuccess;
}

static void json_array_clear_items(JSON_Array * array) {
    size_t i = 0;
    if (array->items != NULL) {
        for (i = 0; i < array->count; i++) {
            json_value_free(_Take(array->items[i]));
        }
    }
    array->count = 0;
}

JSON_Status json_array_clear(JSON_Array * _Nullable array) {
    if (array == NULL) {
        return JSONFailure;
    }
    json_array_clear_items(array);
    return JSONSuccess;
}

JSON_Status json_array_append_value(JSON_Array * _Nullable array, JSON_Value *_Nullable value) {
    if (array == NULL || value == NULL || has_parent(value)) {
        return JSONFailure;
    }
    if (json_array_reserve(array) != JSONSuccess) {
        return JSONFailure;
    }
    return json_array_add(array, adopt_value(value)); /* cannot fail: there is room */
}

JSON_Status json_array_append_string(JSON_Array * _Nullable array, const char *_Nullable string _Noescape) {
    JSON_Value *value = json_value_init_string(string);
    if (value == NULL) {
        return JSONFailure;
    }
    if (json_array_append_value(array, value) != JSONSuccess) {
        json_value_free(value);
        return JSONFailure;
    }
    return JSONSuccess;
}

JSON_Status json_array_append_string_with_len(JSON_Array * _Nullable array, const char *_Nullable __counted_by_or_null(len) string, size_t len) {
    JSON_Value *value = json_value_init_string_with_len(string, len);
    if (value == NULL) {
        return JSONFailure;
    }
    if (json_array_append_value(array, value) != JSONSuccess) {
        json_value_free(value);
        return JSONFailure;
    }
    return JSONSuccess;
}

JSON_Status json_array_append_number(JSON_Array * _Nullable array, double number) {
    JSON_Value *value = json_value_init_number(number);
    if (value == NULL) {
        return JSONFailure;
    }
    if (json_array_append_value(array, value) != JSONSuccess) {
        json_value_free(value);
        return JSONFailure;
    }
    return JSONSuccess;
}

JSON_Status json_array_append_boolean(JSON_Array * _Nullable array, int boolean) {
    JSON_Value *value = json_value_init_boolean(boolean);
    if (value == NULL) {
        return JSONFailure;
    }
    if (json_array_append_value(array, value) != JSONSuccess) {
        json_value_free(value);
        return JSONFailure;
    }
    return JSONSuccess;
}

JSON_Status json_array_append_null(JSON_Array * _Nullable array) {
    JSON_Value *value = json_value_init_null();
    if (value == NULL) {
        return JSONFailure;
    }
    if (json_array_append_value(array, value) != JSONSuccess) {
        json_value_free(value);
        return JSONFailure;
    }
    return JSONSuccess;
}

static JSON_Status json_object_put(JSON_Object * object, const char *name _Noescape, JSON_Value *value) {
    unsigned long hash = 0;
    parson_bool_t found = PARSON_FALSE;
    size_t cell_ix = 0;
    size_t item_ix = 0;
    char *_Nullable _Owned __null_terminated key_copy = NULL;

    hash = hash_string(name, strlen(name));
    found = PARSON_FALSE;
    cell_ix = json_object_get_cell_ix(object, name, strlen(name), hash, &found);
    if (found) {
        if (object->cells == NULL || object->values == NULL) {
            return JSONFailure; /* won't happen */
        }
        item_ix = object->cells[cell_ix];
        json_value_free(_Take(object->values[item_ix]));
        value->parent = object->wrapping_value;
        json_object_store_value(object, item_ix, adopt_value(value));
        return JSONSuccess;
    }
    if (object->count >= object->item_capacity) {
        if (json_object_grow_and_rehash(object) != JSONSuccess) {
            return JSONFailure;
        }
    }
    key_copy = parson_strdup(name);
    if (!key_copy) {
        return JSONFailure;
    }
    return json_object_add(object, key_copy, adopt_value(value)); /* cannot fail: the key is new and there is room */
}

JSON_Status json_object_set_value(JSON_Object * _Nullable object, const char *_Nullable name _Noescape, JSON_Value *_Nullable value) {
    if (!object || !name || !value || has_parent(value)) {
        return JSONFailure;
    }
    return json_object_put(object, name, value);
}

JSON_Status json_object_set_string(JSON_Object * _Nullable object, const char *_Nullable name _Noescape, const char *_Nullable string _Noescape) {
    JSON_Value *value = json_value_init_string(string);
    JSON_Status status = json_object_set_value(object, name, value);
    if (status != JSONSuccess) {
        json_value_free(value);
    }
    return status;
}

JSON_Status json_object_set_string_with_len(JSON_Object * _Nullable object, const char *_Nullable name _Noescape, const char *_Nullable __counted_by_or_null(len) string, size_t len) {
    JSON_Value *value = json_value_init_string_with_len(string, len);
    JSON_Status status = json_object_set_value(object, name, value);
    if (status != JSONSuccess) {
        json_value_free(value);
    }
    return status;
}

JSON_Status json_object_set_number(JSON_Object * _Nullable object, const char *_Nullable name _Noescape, double number) {
    JSON_Value *value = json_value_init_number(number);
    JSON_Status status = json_object_set_value(object, name, value);
    if (status != JSONSuccess) {
        json_value_free(value);
    }
    return status;
}

JSON_Status json_object_set_boolean(JSON_Object * _Nullable object, const char *_Nullable name _Noescape, int boolean) {
    JSON_Value *value = json_value_init_boolean(boolean);
    JSON_Status status = json_object_set_value(object, name, value);
    if (status != JSONSuccess) {
        json_value_free(value);
    }
    return status;
}

JSON_Status json_object_set_null(JSON_Object * _Nullable object, const char *_Nullable name _Noescape) {
    JSON_Value *value = json_value_init_null();
    JSON_Status status = json_object_set_value(object, name, value);
    if (status != JSONSuccess) {
        json_value_free(value);
    }
    return status;
}

JSON_Status json_object_dotset_value(JSON_Object * _Nullable object, const char *_Nullable name _Noescape, JSON_Value *_Nullable value) {
    const char *_Nullable __null_terminated dot_pos = NULL;
    JSON_Value *_Nullable temp_value = NULL;
    JSON_Object *_Nullable new_object = NULL;
    JSON_Value *_Nullable _Owned new_value = NULL;
    size_t name_len = 0;
    char *_Nullable _Owned __null_terminated name_copy = NULL;

    if (object == NULL || name == NULL || value == NULL) {
        return JSONFailure;
    }
    dot_pos = strchr(name, '.');
    if (dot_pos == NULL) {
        return json_object_set_value(object, name, value);
    }
    name_len = dot_pos - name;
    temp_value = json_object_getn_value(object, name, name_len);
    if (temp_value) {
        /* Don't overwrite existing non-object (unlike json_object_set_value, but it shouldn't be changed at this point) */
        JSON_Object *_Nullable temp_object = json_value_get_object(temp_value);
        return json_object_dotset_value(temp_object, dot_pos + 1, value);
    }
    /* Make everything that can fail first: once value is in new_value's tree it cannot be given back. */
    if (object->count >= object->item_capacity && json_object_grow_and_rehash(object) != JSONSuccess) {
        return JSONFailure;
    }
    name_copy = parson_strndup(__null_terminated_to_indexable(name), name_len);
    if (!name_copy) {
        return JSONFailure;
    }
    new_value = json_value_init_object();
    new_object = json_value_get_object(new_value);
    if (new_value == NULL || new_object == NULL || json_object_dotset_value(new_object, dot_pos + 1, value) != JSONSuccess) {
        parson_free(name_copy);
        json_value_free(new_value);
        return JSONFailure;
    }
    return json_object_add(object, name_copy, new_value); /* cannot fail: the key is new and there is room */
}

JSON_Status json_object_dotset_string(JSON_Object * _Nullable object, const char *_Nullable name _Noescape, const char *_Nullable string _Noescape) {
    JSON_Value *value = json_value_init_string(string);
    if (value == NULL) {
        return JSONFailure;
    }
    if (json_object_dotset_value(object, name, value) != JSONSuccess) {
        json_value_free(value);
        return JSONFailure;
    }
    return JSONSuccess;
}

JSON_Status json_object_dotset_string_with_len(JSON_Object * _Nullable object, const char *_Nullable name _Noescape, const char *_Nullable __counted_by_or_null(len) string, size_t len) {
    JSON_Value *value = json_value_init_string_with_len(string, len);
    if (value == NULL) {
        return JSONFailure;
    }
    if (json_object_dotset_value(object, name, value) != JSONSuccess) {
        json_value_free(value);
        return JSONFailure;
    }
    return JSONSuccess;
}

JSON_Status json_object_dotset_number(JSON_Object * _Nullable object, const char *_Nullable name _Noescape, double number) {
    JSON_Value *value = json_value_init_number(number);
    if (value == NULL) {
        return JSONFailure;
    }
    if (json_object_dotset_value(object, name, value) != JSONSuccess) {
        json_value_free(value);
        return JSONFailure;
    }
    return JSONSuccess;
}

JSON_Status json_object_dotset_boolean(JSON_Object * _Nullable object, const char *_Nullable name _Noescape, int boolean) {
    JSON_Value *value = json_value_init_boolean(boolean);
    if (value == NULL) {
        return JSONFailure;
    }
    if (json_object_dotset_value(object, name, value) != JSONSuccess) {
        json_value_free(value);
        return JSONFailure;
    }
    return JSONSuccess;
}

JSON_Status json_object_dotset_null(JSON_Object * _Nullable object, const char *_Nullable name _Noescape) {
    JSON_Value *value = json_value_init_null();
    if (value == NULL) {
        return JSONFailure;
    }
    if (json_object_dotset_value(object, name, value) != JSONSuccess) {
        json_value_free(value);
        return JSONFailure;
    }
    return JSONSuccess;
}

JSON_Status json_object_remove(JSON_Object * _Nullable object, const char *name _Noescape) {
    return object ? json_object_remove_internal(object, name) : JSONFailure;
}

JSON_Status json_object_dotremove(JSON_Object * _Nullable object, const char *name _Noescape) {
    return json_object_dotremove_internal(object, name);
}

static void json_object_clear_items(JSON_Object * object) {
    size_t i = 0;
    if (object->names != NULL && object->values != NULL) {
        for (i = 0; i < object->count; i++) {
            parson_free(_Take(object->names[i]));
            json_value_free(_Take(object->values[i]));
        }
    }
    object->count = 0;
    if (object->cells != NULL) {
        for (i = 0; i < object->cell_capacity; i++) {
            object->cells[i] = OBJECT_INVALID_IX;
        }
    }
}

JSON_Status json_object_clear(JSON_Object * _Nullable object) {
    if (object == NULL) {
        return JSONFailure;
    }
    json_object_clear_items(object);
    return JSONSuccess;
}

JSON_Status json_validate(const JSON_Value *_Nullable schema, const JSON_Value *_Nullable value) {
    JSON_Value *temp_schema_value = NULL, *temp_value = NULL;
    JSON_Array *schema_array = NULL, *value_array = NULL;
    JSON_Object *schema_object = NULL, *value_object = NULL;
    JSON_Value_Type schema_type = JSONError, value_type = JSONError;
    const char *__null_terminated key = NULL;
    size_t i = 0, count = 0;
    if (schema == NULL || value == NULL) {
        return JSONFailure;
    }
    schema_type = json_value_get_type(schema);
    value_type = json_value_get_type(value);
    if (schema_type != value_type && schema_type != JSONNull) { /* null represents all values */
        return JSONFailure;
    }
    switch (schema_type) {
        case JSONArray:
            schema_array = json_value_get_array(schema);
            value_array = json_value_get_array(value);
            count = json_array_get_count(schema_array);
            if (count == 0) {
                return JSONSuccess; /* Empty array allows all types */
            }
            /* Get first value from array, rest is ignored */
            temp_schema_value = json_array_get_value(schema_array, 0);
            for (i = 0; i < json_array_get_count(value_array); i++) {
                temp_value = json_array_get_value(value_array, i);
                if (json_validate(temp_schema_value, temp_value) != JSONSuccess) {
                    return JSONFailure;
                }
            }
            return JSONSuccess;
        case JSONObject:
            schema_object = json_value_get_object(schema);
            value_object = json_value_get_object(value);
            count = json_object_get_count(schema_object);
            if (count == 0) {
                return JSONSuccess; /* Empty object allows all objects */
            } else if (json_object_get_count(value_object) < count) {
                return JSONFailure; /* Tested object mustn't have less name-value pairs than schema */
            }
            for (i = 0; i < count; i++) {
                key = json_object_get_name(schema_object, i);
                temp_schema_value = json_object_get_value(schema_object, key);
                temp_value = json_object_get_value(value_object, key);
                if (temp_value == NULL) {
                    return JSONFailure;
                }
                if (json_validate(temp_schema_value, temp_value) != JSONSuccess) {
                    return JSONFailure;
                }
            }
            return JSONSuccess;
        case JSONString: case JSONNumber: case JSONBoolean: case JSONNull:
            return JSONSuccess; /* equality already tested before switch */
        case JSONError: default:
            return JSONFailure;
    }
}

int json_value_equals(const JSON_Value *_Nullable a, const JSON_Value *_Nullable b) {
    JSON_Object *a_object = NULL, *b_object = NULL;
    JSON_Array *a_array = NULL, *b_array = NULL;
    JSON_Chars a_string, b_string;
    const char *__null_terminated key = NULL;
    size_t a_count = 0, b_count = 0, i = 0;
    JSON_Value_Type a_type, b_type;
    a_type = json_value_get_type(a);
    b_type = json_value_get_type(b);
    if (a_type != b_type) {
        return PARSON_FALSE;
    }
    switch (a_type) {
        case JSONArray:
            a_array = json_value_get_array(a);
            b_array = json_value_get_array(b);
            a_count = json_array_get_count(a_array);
            b_count = json_array_get_count(b_array);
            if (a_count != b_count) {
                return PARSON_FALSE;
            }
            for (i = 0; i < a_count; i++) {
                if (!json_value_equals(json_array_get_value(a_array, i),
                                       json_array_get_value(b_array, i))) {
                    return PARSON_FALSE;
                }
            }
            return PARSON_TRUE;
        case JSONObject:
            a_object = json_value_get_object(a);
            b_object = json_value_get_object(b);
            a_count = json_object_get_count(a_object);
            b_count = json_object_get_count(b_object);
            if (a_count != b_count) {
                return PARSON_FALSE;
            }
            for (i = 0; i < a_count; i++) {
                key = json_object_get_name(a_object, i);
                if (!json_value_equals(json_object_get_value(a_object, key),
                                       json_object_get_value(b_object, key))) {
                    return PARSON_FALSE;
                }
            }
            return PARSON_TRUE;
        case JSONString:
            a_string = json_value_get_chars(a);
            b_string = json_value_get_chars(b);
            if (a_string.p == NULL || b_string.p == NULL) {
                return PARSON_FALSE; /* shouldn't happen */
            }
            return a_string.len == b_string.len &&
                   memcmp(a_string.p, b_string.p, a_string.len) == 0;
        case JSONBoolean:
            return json_value_get_boolean(a) == json_value_get_boolean(b);
        case JSONNumber:
            return fabs(json_value_get_number(a) - json_value_get_number(b)) < 0.000001; /* EPSILON */
        case JSONError:
            return PARSON_TRUE;
        case JSONNull:
            return PARSON_TRUE;
        default:
            return PARSON_TRUE;
    }
}

JSON_Value_Type json_type(const JSON_Value *_Nullable value) {
    return json_value_get_type(value);
}

JSON_Object *_Nullable json_object (const JSON_Value *_Nullable value) {
    return json_value_get_object(value);
}

JSON_Array *_Nullable json_array(const JSON_Value *_Nullable value) {
    return json_value_get_array(value);
}

const char *_Nullable json_string(const JSON_Value *_Nullable value) {
    return json_value_get_string(value);
}

size_t json_string_len(const JSON_Value *_Nullable value) {
    return json_value_get_string_len(value);
}

double json_number(const JSON_Value *_Nullable value) {
    return json_value_get_number(value);
}

int json_boolean(const JSON_Value *_Nullable value) {
    return json_value_get_boolean(value);
}

void json_set_allocation_functions(JSON_Malloc_Function malloc_fun, JSON_Free_Function free_fun ) {
    /* unsafe: settings, set before other calls (parson.h says so) */
    __unsafe {
        parson_malloc_hook = malloc_fun;
        parson_free = free_fun;
    }
}

void json_set_escape_slashes(int escape_slashes) {
    __unsafe { parson_escape_slashes = escape_slashes; } /* unsafe: as above */
}

void json_set_float_serialization_format(const char *_Nullable format _Noescape) {
    /* unsafe: as above */
    __unsafe {
        parson_free(_Take(parson_float_format));
        if (format) {
            parson_float_format = parson_strdup(format);
        }
    }
}

void json_set_number_serialization_function(JSON_Number_Serialization_Function _Nullable func) {
    __unsafe { parson_number_serialization_function = func; } /* unsafe: as above */
}
