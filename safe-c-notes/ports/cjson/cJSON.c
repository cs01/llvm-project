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

/* cJSON */
/* JSON parser in C. */

/* disable warnings about old C89 functions in MSVC */
#if !defined(_CRT_SECURE_NO_DEPRECATE) && defined(_MSC_VER)
#define _CRT_SECURE_NO_DEPRECATE
#endif

#ifdef __GNUC__
#pragma GCC visibility push(default)
#endif
#if defined(_MSC_VER)
#pragma warning (push)
/* disable warning about single line comments in system headers */
#pragma warning (disable : 4001)
#endif

#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <limits.h>
#include <ctype.h>
#include <float.h>

#ifdef ENABLE_LOCALES
#include <locale.h>
#endif

#if defined(_MSC_VER)
#pragma warning (pop)
#endif
#ifdef __GNUC__
#pragma GCC visibility pop
#endif

#include "cJSON.h"

/* define our own boolean type */
#ifdef true
#undef true
#endif
#define true ((cJSON_bool)1)

#ifdef false
#undef false
#endif
#define false ((cJSON_bool)0)

/* define isnan and isinf for ANSI C, if in C99 or above, isnan and isinf has been defined in math.h */
#ifndef isinf
#define isinf(d) (isnan((d - d)) && !isnan(d))
#endif
#ifndef isnan
#define isnan(d) (d != d)
#endif

#ifndef NAN
#ifdef _WIN32
#define NAN sqrt(-1.0)
#else
#define NAN 0.0/0.0
#endif
#endif

typedef struct {
    /* keeps the input's bounds, so the error pointer handed out is bounded too.
       Unchecked: it points into the caller's input after the parse returns,
       and cJSON never knows whether that input is still alive. */
    const unsigned char *_Nullable _Unchecked __bidi_indexable json;
    size_t position;
} error;
/* Safe C: one per thread (cJSON_GetErrorPtr reports this thread's last error). */
static _Thread_local error global_error = { NULL, 0 };

CJSON_PUBLIC(const char *_Nullable __unsafe_indexable) cJSON_GetErrorPtr(void)
{
    /* unsafe: only computes an address; the result is __unsafe_indexable, so callers need __unsafe to read it */
    __unsafe { return (const char*) (global_error.json + global_error.position); }
}

/* Field getters: each returns a borrow of a field of item. */
static char *_Nullable __null_terminated valuestring_of(const cJSON *item _Lifetimebound)
{
    return item->valuestring;
}
/* unsafe: as above */
static cJSON *_Nullable first_child(const cJSON *item _Lifetimebound)
{
    return item->child;
}
/* unsafe: as above */
static cJSON *_Nullable next_sibling(const cJSON *item _Lifetimebound)
{
    return item->next;
}

CJSON_PUBLIC(char *_Nullable __null_terminated) cJSON_GetStringValue(const cJSON * const item _Lifetimebound)
{
    /* cJSON_IsString checks for NULL too, but Safe C cannot see that through the call */
    if ((item == NULL) || !cJSON_IsString(item))
    {
        return NULL;
    }

    return valuestring_of(item);
}

CJSON_PUBLIC(double) cJSON_GetNumberValue(const cJSON * const item)
{
    if ((item == NULL) || !cJSON_IsNumber(item))
    {
        return (double) NAN;
    }

    return item->valuedouble;
}

/* This is a safeguard to prevent copy-pasters from using incompatible C and header files */
#if (CJSON_VERSION_MAJOR != 1) || (CJSON_VERSION_MINOR != 7) || (CJSON_VERSION_PATCH != 19)
    #error cJSON.h and cJSON.c have different versions. Make sure that both have the same.
#endif

#define CJSON_STRINGIFY_(x) #x
#define CJSON_STRINGIFY(x) CJSON_STRINGIFY_(x)

CJSON_PUBLIC(const char*) cJSON_Version(void)
{
    /* a literal is already terminated, so no buffer or conversion is needed */
    return CJSON_STRINGIFY(CJSON_VERSION_MAJOR) "." CJSON_STRINGIFY(CJSON_VERSION_MINOR) "." CJSON_STRINGIFY(CJSON_VERSION_PATCH);
}

/* Case insensitive string comparison, doesn't consider two NULL pointers equal though */
static int case_insensitive_strcmp(const char *_Nullable __null_terminated string1, const char *_Nullable __null_terminated string2)
{
    if ((string1 == NULL) || (string2 == NULL))
    {
        return 1;
    }

    if (string1 == string2)
    {
        return 0;
    }

    for(; tolower((unsigned char)*string1) == tolower((unsigned char)*string2); (void)string1++, string2++)
    {
        if (*string1 == '\0')
        {
            return 0;
        }
    }

    return tolower((unsigned char)*string1) - tolower((unsigned char)*string2);
}

typedef struct internal_hooks
{
    void *_Nullable __sized_by_or_null(size) (CJSON_CDECL *allocate)(size_t size);
    void (CJSON_CDECL *deallocate)(void *_Nullable __unsafe_indexable pointer) _Takes(1);
    void *_Nullable __sized_by_or_null(size) (CJSON_CDECL *_Nullable reallocate)(void *_Nullable __unsafe_indexable pointer, size_t size) _Takes(1);
} internal_hooks;

/* Wrappers rather than the libc functions themselves: MSVC cannot take the
   address of a dllimport function in a static initializer (C2322), and Safe C
   needs the hook types to match the user-facing cJSON_Hooks exactly. */
static _Returns_owned void *_Nullable __sized_by_or_null(size) CJSON_CDECL internal_malloc(size_t size)
{
    return malloc(size);
}
static void CJSON_CDECL internal_free(void *_Nullable _Owned __unsafe_indexable pointer)
{
    free(pointer);
}
static _Returns_owned void *_Nullable __sized_by_or_null(size) CJSON_CDECL internal_realloc(void *_Nullable _Owned __unsafe_indexable pointer, size_t size)
{
    return realloc(pointer, size);
}

/* The allocator hooks are plain function pointers, so Safe C cannot know that
   their result is fresh, untyped memory. hook_allocate is the one place that
   vouches for it; marked alloc_size, its result can then be given a type like
   malloc's. */
__unsafe __attribute__((alloc_size(2)))
static void *_Nullable hook_allocate(const internal_hooks * const hooks, size_t size)
{
    /* unsafe: a user-supplied hook is trusted to return fresh memory */
    return hooks->allocate(size);
}

static _Returns_owned unsigned char *_Nullable __counted_by_or_null(size) allocate_bytes(const internal_hooks * const hooks, size_t size)
{
    return (unsigned char *)hook_allocate(hooks, size);
}

_Returns_owned static cJSON *_Nullable allocate_item(const internal_hooks * const hooks)
{
    return (cJSON *)hook_allocate(hooks, sizeof(cJSON));
}

/* strlen of character literals resolved at compile time */
#define static_strlen(string_literal) (sizeof(string_literal) - sizeof(""))

/* Safe C: set by cJSON_InitHooks, which must run before any thread uses cJSON. */
static internal_hooks global_hooks _Unsynchronized = { internal_malloc, internal_free, internal_realloc };

_Returns_owned static char *_Nullable __null_terminated cJSON_strdup(const char *_Nullable __null_terminated string, const internal_hooks * const hooks)
{
    size_t length = 0;
    char *_Nullable _Owned copy = NULL;

    if (string == NULL)
    {
        return NULL;
    }

    length = strlen(string) + sizeof("");
    copy = (char *)allocate_bytes(hooks, length);
    if (copy == NULL)
    {
        return NULL;
    }
    /* the indexable view of string excludes its terminator, so copy the
       characters and write the terminator separately */
    memcpy(copy, __null_terminated_to_indexable(string), length - 1);
    copy[length - 1] = '\0';

    /* unsafe: the terminator was just written at copy[length - 1] */
    __unsafe { return __unsafe_null_terminated_from_indexable(copy, copy + length - 1); }
}

CJSON_PUBLIC(void) cJSON_InitHooks(cJSON_Hooks* hooks)
{
    internal_hooks h = { internal_malloc, internal_free, internal_realloc };
    if (hooks != NULL)
    {
        if (hooks->malloc_fn != NULL)
        {
            h.allocate = hooks->malloc_fn;
        }
        if (hooks->free_fn != NULL)
        {
            h.deallocate = hooks->free_fn;
        }
        /* use realloc only if both free and malloc are used */
        if ((h.allocate != internal_malloc) || (h.deallocate != internal_free))
        {
            h.reallocate = NULL;
        }
    }
    /* Safe C: global_hooks is _Unsynchronized; this is the only write. */
    __unsafe { global_hooks = h; }
}

/* Internal constructor. */
_Returns_owned static cJSON *_Nullable cJSON_New_Item(const internal_hooks * const hooks)
{
    cJSON *_Nullable _Owned node = allocate_item(hooks);
    if (node)
    {
        memset(node, '\0', sizeof(cJSON));
    }

    return node;
}

/* Delete a cJSON structure. */
CJSON_PUBLIC(void) cJSON_Delete(cJSON *_Nullable _Owned item)
{
    cJSON *_Nullable _Owned next = NULL;
    while (item != NULL)
    {
        next = _Take(item->next);
        if (!(item->type & cJSON_IsReference))
        {
            cJSON_Delete(_Take(item->child));
            global_hooks.deallocate(_Take(item->valuestring));
        }
        if (!(item->type & cJSON_StringIsConst))
        {
            global_hooks.deallocate(_Take(item->string));
        }
        /* a reference's child and valuestring, and a constant key, belong to
           someone else: forget them without freeing (see adopt_borrowed) */
        global_hooks.deallocate(item);
        item = next;
    }
}

/* Child lists. next and child own the items; prev is a back pointer (the
   first child's prev is the last child) that only the helpers below and the
   detach/insert/replace functions edit, each in a small __unsafe block, so
   that appending stays O(1) as in upstream cJSON. */

/* Appends item to parent's children and returns it, now borrowed from parent. */
static cJSON *_Nullable append_child(cJSON *_Exclusive parent _Lifetimebound, cJSON *_Owned item)
{
    cJSON *_Nullable head = parent->child;
    if (head == NULL)
    {
        item->prev = item;
        parent->child = item;
        return first_child(parent);
    }
    /* unsafe: head->prev is the tail, a live item of this list that parent (lent exclusively) owns through the next chain */
    __unsafe
    {
        cJSON *tail = head->prev;
        item->prev = tail;
        head->prev = item;
        tail->next = item;
        return head->prev;
    }
}

/* Moves a parsed string into the key, for parse_object. */
static void key_from_valuestring(cJSON *_Exclusive item)
{
    item->string = _Take(item->valuestring);
}

/* get the decimal point character of the current locale */
static unsigned char get_decimal_point(void)
{
#ifdef ENABLE_LOCALES
    struct lconv *lconv = localeconv();
    return (unsigned char) lconv->decimal_point[0];
#else
    return '.';
#endif
}

/* a view: content borrows the caller's input for the length of one parse */
typedef struct _View
{
    const unsigned char *__counted_by(length) content;
    size_t length;
    size_t offset;
    size_t depth; /* How deeply nested (in arrays/objects) is the input at the current offset. */
    internal_hooks hooks;
} parse_buffer;

/* check if the given size is left to read in a given parse buffer (starting with 1) */
#define can_read(buffer, size) ((buffer != NULL) && (((buffer)->offset + size) <= (buffer)->length))
/* check if the buffer can be accessed at the given index (starting with 0) */
#define can_access_at_index(buffer, index) ((buffer != NULL) && (((buffer)->offset + index) < (buffer)->length))
#define cannot_access_at_index(buffer, index) (!can_access_at_index(buffer, index))
/* get a pointer to the buffer at the position */
#define buffer_at_offset(buffer) ((buffer)->content + (buffer)->offset)

/* Parse the input text to generate a number, and populate the result into item. */
static cJSON_bool parse_number(cJSON *_Exclusive const item _Noescape, parse_buffer * const input_buffer)
{
    double number = 0;
    /* strtod stores a pointer into its NUL-terminated input; it is only compared */
    char *__null_terminated after_end = NULL;
    unsigned char *_Nullable number_c_string;
    unsigned char decimal_point = get_decimal_point();
    size_t i = 0;
    size_t number_string_length = 0;
    cJSON_bool has_decimal_point = false;

    if ((input_buffer == NULL) || (input_buffer->content == NULL))
    {
        return false;
    }

    /* copy the number into a temporary buffer and replace '.' with the decimal point
     * of the current locale (for strtod)
     * This also takes care of '\0' not necessarily being available for marking the end of the input */
    for (i = 0; can_access_at_index(input_buffer, i); i++)
    {
        switch (buffer_at_offset(input_buffer)[i])
        {
            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9':
            case '+':
            case '-':
            case 'e':
            case 'E':
                number_string_length++;
                break;

            case '.':
                number_string_length++;
                has_decimal_point = true;
                break;

            default:
                goto loop_end;
        }
    }
loop_end:
    /* malloc for temporary buffer, add 1 for '\0' */
    number_c_string = allocate_bytes(&input_buffer->hooks, number_string_length + 1);
    if (number_c_string == NULL)
    {
        return false; /* allocation failure */
    }

    memcpy(number_c_string, buffer_at_offset(input_buffer), number_string_length);
    number_c_string[number_string_length] = '\0';

    if (has_decimal_point)
    {
        for (i = 0; i < number_string_length; i++)
        {
            if (number_c_string[i] == '.')
            {
                /* replace '.' with the decimal point of the current locale (for strtod) */
                number_c_string[i] = decimal_point;
            }
        }
    }

    number = strtod((const char*)number_c_string, &after_end);
    if ((char*)number_c_string == after_end)
    {
        /* free the temporary buffer */
        input_buffer->hooks.deallocate(number_c_string);
        return false; /* parse_error */
    }

    item->valuedouble = number;

    /* use saturation in case of overflow */
    if (number >= INT_MAX)
    {
        item->valueint = INT_MAX;
    }
    else if (number <= (double)INT_MIN)
    {
        item->valueint = INT_MIN;
    }
    else
    {
        item->valueint = (int)number;
    }

    item->type = cJSON_Number;

    input_buffer->offset += (size_t)(after_end - (char*)number_c_string);
    /* free the temporary buffer */
    input_buffer->hooks.deallocate(number_c_string);
    return true;
}

/* don't ask me, but the original cJSON_SetNumberValue returns an integer or double */
CJSON_PUBLIC(double) cJSON_SetNumberHelper(cJSON *object, double number)
{
    if (object == NULL)
    {
        return (double)NAN;
    }

    if (number >= INT_MAX)
    {
        object->valueint = INT_MAX;
    }
    else if (number <= (double)INT_MIN)
    {
        object->valueint = INT_MIN;
    }
    else
    {
        object->valueint = (int)number;
    }

    return object->valuedouble = number;
}

/* Note: when passing a NULL valuestring, cJSON_SetValuestring treats this as an error and return NULL */
CJSON_PUBLIC(char *_Nullable __null_terminated) cJSON_SetValuestring(cJSON *_Exclusive object _Lifetimebound, const char *valuestring _Noescape)
{
    char *_Nullable _Owned __null_terminated copy = NULL;
    const char *source = NULL;
    char *destination = NULL;
    size_t v1_len;
    size_t v2_len;
    /* if object's type is not cJSON_String or is cJSON_IsReference, it should not set valuestring */
    if ((object == NULL) || !(object->type & cJSON_String) || (object->type & cJSON_IsReference))
    {
        return NULL;
    }
    /* return NULL if the object is corrupted or valuestring is NULL */
    if (object->valuestring == NULL || valuestring == NULL)
    {
        return NULL;
    }

    source = __null_terminated_to_indexable(valuestring);
    destination = __null_terminated_to_indexable(object->valuestring);
    v1_len = strlen(valuestring);
    v2_len = strlen(object->valuestring);

    if (v1_len <= v2_len)
    {
        /* strcpy does not handle overlapping string: [X1, X2] [Y1, Y2] => X2 < Y1 or Y2 < X1 */
        if (!( source + v1_len < destination || destination + v2_len < source ))
        {
            return NULL;
        }
        /* destination's bounds exclude its terminator: copy the characters,
           and terminate only if the new string is shorter */
        memcpy(destination, source, v1_len);
        if (v1_len < v2_len)
        {
            destination[v1_len] = '\0';
        }
        return valuestring_of(object);
    }
    copy = cJSON_strdup(valuestring, &global_hooks);
    if (copy == NULL)
    {
        return NULL;
    }
    global_hooks.deallocate(_Take(object->valuestring));
    object->valuestring = copy;

    return valuestring_of(object);
}

typedef struct
{
    /* owned, except under noalloc (cJSON_PrintPreallocated), where it holds the
       caller's buffer and is taken back out unfreed */
    unsigned char *_Nullable _Owned __counted_by_or_null(length) buffer;
    size_t length;
    size_t offset;
    size_t depth; /* current nesting depth (for formatted printing) */
    cJSON_bool noalloc;
    cJSON_bool format; /* is this print a formatted print */
    internal_hooks hooks;
} printbuffer;

/* Moves the buffer out of p and reallocates it; like reallocf, the old block
   is freed if that fails. It takes p rather than the owned pointer because
   Safe C rejects an owned local passed to a function that both takes and
   returns ownership. */
static _Returns_owned unsigned char *_Nullable __counted_by_or_null(size) reallocate_buffer(printbuffer *_Exclusive p, size_t size)
{
    unsigned char *_Nullable _Owned pointer = _Take(p->buffer);
    p->buffer = NULL;
    p->length = 0;
    if (p->hooks.reallocate == NULL)
    {
        p->hooks.deallocate(pointer);
        return NULL;
    }
    /* unsafe: a user-supplied hook is trusted to keep the bytes it moves (the old block held bytes, so the type is unchanged),
       and realloc keeps the old block when it fails, a conditional move no kind can say. */
    __unsafe
    {
        unsigned char *_Nullable memory = (unsigned char *)p->hooks.reallocate(pointer, size);
        if (memory == NULL)
        {
            p->hooks.deallocate(pointer);
        }
        return memory;
    }
}

/* realloc printbuffer if necessary to have at least "needed" bytes more */
static unsigned char *_Nullable __bidi_indexable ensure(printbuffer *_Exclusive const p _Lifetimebound, size_t needed)
{
    unsigned char *_Nullable _Owned newbuffer = NULL;
    unsigned char *_Nullable oldbuffer = NULL;
    size_t newsize = 0;

    if (p == NULL)
    {
        return NULL;
    }
    /* a local copy: Safe C does not carry a null check on a counted field
       into later uses of its value */
    oldbuffer = p->buffer;
    if (oldbuffer == NULL)
    {
        return NULL;
    }

    if ((p->length > 0) && (p->offset >= p->length))
    {
        /* make sure that offset is valid */
        return NULL;
    }

    if (needed > INT_MAX)
    {
        /* sizes bigger than INT_MAX are currently not supported */
        return NULL;
    }

    needed += p->offset + 1;
    if (needed <= p->length)
    {
        return p->buffer + p->offset;
    }

    if (p->noalloc) {
        return NULL;
    }

    /* calculate new buffer size */
    if (needed > (INT_MAX / 2))
    {
        /* overflow of int, use INT_MAX if possible */
        if (needed <= INT_MAX)
        {
            newsize = INT_MAX;
        }
        else
        {
            return NULL;
        }
    }
    else
    {
        newsize = needed * 2;
    }

    if (p->hooks.reallocate != NULL)
    {
        /* reallocate with realloc if available */
        /* reallocate_bytes frees the old buffer if it fails */
        newbuffer = reallocate_buffer(p, newsize);
        if (newbuffer == NULL)
        {
            return NULL;
        }
    }
    else
    {
        /* otherwise reallocate manually */
        newbuffer = allocate_bytes(&p->hooks, newsize);
        if (!newbuffer)
        {
            p->hooks.deallocate(_Take(p->buffer));
            p->buffer = NULL;
            p->length = 0;

            return NULL;
        }

        memcpy(newbuffer, oldbuffer, p->offset + 1);
        p->hooks.deallocate(_Take(p->buffer));
    }
    p->length = newsize;
    p->buffer = newbuffer;

    return p->buffer + p->offset;
}

/* calculate the new length of the string in a printbuffer and update the offset */
static void update_offset(printbuffer * const buffer)
{
    const unsigned char *_Nullable buffer_pointer = NULL;
    if (buffer == NULL)
    {
        return;
    }
    buffer_pointer = buffer->buffer;
    if (buffer_pointer == NULL)
    {
        return;
    }

    buffer->offset += strlen((const char*)(buffer_pointer + buffer->offset));
}

/* securely comparison of floating-point variables */
static cJSON_bool compare_double(double a, double b)
{
    double maxVal = fabs(a) > fabs(b) ? fabs(a) : fabs(b);
    return (fabs(a - b) <= maxVal * DBL_EPSILON);
}

/* Render the number nicely from the given item into a string. */
static cJSON_bool print_number(const cJSON * const item, printbuffer *_Exclusive const output_buffer)
{
    unsigned char *output_pointer = NULL;
    double d = item->valuedouble;
    int length = 0;
    size_t i = 0;
    unsigned char number_buffer[26] = {0}; /* temporary buffer to print the number into */
    unsigned char decimal_point = get_decimal_point();
    double test = 0.0;

    if (output_buffer == NULL)
    {
        return false;
    }

    /* This checks for NaN and Infinity */
    if (isnan(d) || isinf(d))
    {
        length = snprintf((char*)number_buffer, sizeof(number_buffer), "null");
    }
    else if(d == (double)item->valueint)
    {
        length = snprintf((char*)number_buffer, sizeof(number_buffer), "%d", item->valueint);
    }
    else
    {
        /* Try 15 decimal places of precision to avoid nonsignificant nonzero digits */
        length = snprintf((char*)number_buffer, sizeof(number_buffer), "%1.15g", d);

        /* Check whether the original double can be recovered */
        if ((sscanf((char*)number_buffer, "%lg", &test) != 1) || !compare_double((double)test, d))
        {
            /* If not, print with 17 decimal places of precision */
            length = snprintf((char*)number_buffer, sizeof(number_buffer), "%1.17g", d);
        }
    }

    /* sprintf failed or buffer overrun occurred */
    if ((length < 0) || (length > (int)(sizeof(number_buffer) - 1)))
    {
        return false;
    }

    /* reserve appropriate space in the output */
    output_pointer = ensure(output_buffer, (size_t)length + sizeof(""));
    if (output_pointer == NULL)
    {
        return false;
    }

    /* copy the printed number to the output and replace locale
     * dependent decimal point with '.' */
    for (i = 0; i < ((size_t)length); i++)
    {
        if (number_buffer[i] == decimal_point)
        {
            output_pointer[i] = '.';
            continue;
        }

        output_pointer[i] = number_buffer[i];
    }
    output_pointer[i] = '\0';

    output_buffer->offset += (size_t)length;

    return true;
}

/* parse 4 digit hexadecimal number */
static unsigned parse_hex4(const unsigned char *__counted_by(4) const input)
{
    unsigned int h = 0;
    size_t i = 0;

    for (i = 0; i < 4; i++)
    {
        /* parse digit */
        if ((input[i] >= '0') && (input[i] <= '9'))
        {
            h += (unsigned int) input[i] - '0';
        }
        else if ((input[i] >= 'A') && (input[i] <= 'F'))
        {
            h += (unsigned int) 10 + input[i] - 'A';
        }
        else if ((input[i] >= 'a') && (input[i] <= 'f'))
        {
            h += (unsigned int) 10 + input[i] - 'a';
        }
        else /* invalid */
        {
            return 0;
        }

        if (i < 3)
        {
            /* shift left to make place for the next nibble */
            h = h << 4;
        }
    }

    return h;
}

/* converts a UTF-16 literal to UTF-8
 * A literal can be one or two sequences of the form \uXXXX */
static unsigned char utf16_literal_to_utf8(const unsigned char *__bidi_indexable const input_pointer, const unsigned char *__bidi_indexable const input_end, unsigned char *__bidi_indexable *output_pointer)
{
    long unsigned int codepoint = 0;
    unsigned int first_code = 0;
    const unsigned char *first_sequence = input_pointer;
    unsigned char utf8_length = 0;
    unsigned char utf8_position = 0;
    unsigned char sequence_length = 0;
    unsigned char first_byte_mark = 0;

    if ((input_end - first_sequence) < 6)
    {
        /* input ends unexpectedly */
        goto fail;
    }

    /* get the first utf16 sequence */
    first_code = parse_hex4(first_sequence + 2);

    /* check that the code is valid */
    if (((first_code >= 0xDC00) && (first_code <= 0xDFFF)))
    {
        goto fail;
    }

    /* UTF16 surrogate pair */
    if ((first_code >= 0xD800) && (first_code <= 0xDBFF))
    {
        const unsigned char *second_sequence = first_sequence + 6;
        unsigned int second_code = 0;
        sequence_length = 12; /* \uXXXX\uXXXX */

        if ((input_end - second_sequence) < 6)
        {
            /* input ends unexpectedly */
            goto fail;
        }

        if ((second_sequence[0] != '\\') || (second_sequence[1] != 'u'))
        {
            /* missing second half of the surrogate pair */
            goto fail;
        }

        /* get the second utf16 sequence */
        second_code = parse_hex4(second_sequence + 2);
        /* check that the code is valid */
        if ((second_code < 0xDC00) || (second_code > 0xDFFF))
        {
            /* invalid second half of the surrogate pair */
            goto fail;
        }


        /* calculate the unicode codepoint from the surrogate pair */
        codepoint = 0x10000 + (((first_code & 0x3FF) << 10) | (second_code & 0x3FF));
    }
    else
    {
        sequence_length = 6; /* \uXXXX */
        codepoint = first_code;
    }

    /* encode as UTF-8
     * takes at maximum 4 bytes to encode:
     * 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx */
    if (codepoint < 0x80)
    {
        /* normal ascii, encoding 0xxxxxxx */
        utf8_length = 1;
    }
    else if (codepoint < 0x800)
    {
        /* two bytes, encoding 110xxxxx 10xxxxxx */
        utf8_length = 2;
        first_byte_mark = 0xC0; /* 11000000 */
    }
    else if (codepoint < 0x10000)
    {
        /* three bytes, encoding 1110xxxx 10xxxxxx 10xxxxxx */
        utf8_length = 3;
        first_byte_mark = 0xE0; /* 11100000 */
    }
    else if (codepoint <= 0x10FFFF)
    {
        /* four bytes, encoding 1110xxxx 10xxxxxx 10xxxxxx 10xxxxxx */
        utf8_length = 4;
        first_byte_mark = 0xF0; /* 11110000 */
    }
    else
    {
        /* invalid unicode codepoint */
        goto fail;
    }

    /* encode as utf8 */
    for (utf8_position = (unsigned char)(utf8_length - 1); utf8_position > 0; utf8_position--)
    {
        /* 10xxxxxx */
        (*output_pointer)[utf8_position] = (unsigned char)((codepoint | 0x80) & 0xBF);
        codepoint >>= 6;
    }
    /* encode first byte */
    if (utf8_length > 1)
    {
        (*output_pointer)[0] = (unsigned char)((codepoint | first_byte_mark) & 0xFF);
    }
    else
    {
        (*output_pointer)[0] = (unsigned char)(codepoint & 0x7F);
    }

    *output_pointer += utf8_length;

    return sequence_length;

fail:
    return 0;
}

/* Parse the input text into an unescaped cinput, and populate item. */
static cJSON_bool parse_string(cJSON *_Exclusive const item _Noescape, parse_buffer * const input_buffer)
{
    const unsigned char *input_pointer = buffer_at_offset(input_buffer) + 1;
    const unsigned char *input_end = buffer_at_offset(input_buffer) + 1;
    unsigned char *output_pointer = NULL;
    unsigned char *output = NULL;

    /* not a string */
    if (buffer_at_offset(input_buffer)[0] != '\"')
    {
        goto fail;
    }

    {
        /* calculate approximate size of the output (overestimate) */
        size_t allocation_length = 0;
        size_t skipped_bytes = 0;
        while (((size_t)(input_end - input_buffer->content) < input_buffer->length) && (*input_end != '\"'))
        {
            /* is escape sequence */
            if (input_end[0] == '\\')
            {
                if ((size_t)(input_end + 1 - input_buffer->content) >= input_buffer->length)
                {
                    /* prevent buffer overflow when last input character is a backslash */
                    goto fail;
                }
                skipped_bytes++;
                input_end++;
            }
            input_end++;
        }
        if (((size_t)(input_end - input_buffer->content) >= input_buffer->length) || (*input_end != '\"'))
        {
            goto fail; /* string ended unexpectedly */
        }

        /* This is at most how much we need for the output */
        allocation_length = (size_t) (input_end - buffer_at_offset(input_buffer)) - skipped_bytes;
        output = allocate_bytes(&input_buffer->hooks, allocation_length + sizeof(""));
        if (output == NULL)
        {
            goto fail; /* allocation failure */
        }
    }

    output_pointer = output;
    /* loop through the string literal */
    while (input_pointer < input_end)
    {
        if (*input_pointer != '\\')
        {
            *output_pointer++ = *input_pointer++;
        }
        /* escape sequence */
        else
        {
            unsigned char sequence_length = 2;
            if ((input_end - input_pointer) < 1)
            {
                goto fail;
            }

            switch (input_pointer[1])
            {
                case 'b':
                    *output_pointer++ = '\b';
                    break;
                case 'f':
                    *output_pointer++ = '\f';
                    break;
                case 'n':
                    *output_pointer++ = '\n';
                    break;
                case 'r':
                    *output_pointer++ = '\r';
                    break;
                case 't':
                    *output_pointer++ = '\t';
                    break;
                case '\"':
                case '\\':
                case '/':
                    *output_pointer++ = input_pointer[1];
                    break;

                /* UTF-16 literal */
                case 'u':
                    sequence_length = utf16_literal_to_utf8(input_pointer, input_end, &output_pointer);
                    if (sequence_length == 0)
                    {
                        /* failed to convert UTF16-literal to UTF-8 */
                        goto fail;
                    }
                    break;

                default:
                    goto fail;
            }
            input_pointer += sequence_length;
        }
    }

    /* zero terminate the output */
    *output_pointer = '\0';

    item->type = cJSON_String;
    /* unsafe: the terminator was just written at output_pointer */
    __unsafe { item->valuestring = __unsafe_null_terminated_from_indexable((char*)output, (char*)output_pointer); }

    input_buffer->offset = (size_t) (input_end - input_buffer->content);
    input_buffer->offset++;

    return true;

fail:
    if (output != NULL)
    {
        input_buffer->hooks.deallocate(output);
        output = NULL;
    }

    if (input_pointer != NULL)
    {
        input_buffer->offset = (size_t)(input_pointer - input_buffer->content);
    }

    return false;
}

/* Render the cstring provided to an escaped version that can be printed. */
static cJSON_bool print_string_ptr(const char *_Nullable __null_terminated const string, printbuffer *_Exclusive const output_buffer)
{
    const unsigned char *input = NULL;
    const unsigned char *input_pointer = NULL;
    unsigned char *output = NULL;
    unsigned char *output_pointer = NULL;
    size_t output_length = 0;
    /* numbers of additional characters needed for escaping */
    size_t escape_characters = 0;

    if (output_buffer == NULL)
    {
        return false;
    }

    /* empty string */
    if (string == NULL)
    {
        output = ensure(output_buffer, sizeof("\"\""));
        if (output == NULL)
        {
            return false;
        }
        memcpy(output, "\"\"", sizeof("\"\""));

        return true;
    }

    /* the scans below read the terminator, so it must be in bounds; string is
       const, so nothing can erase it through this view */
    input = (const unsigned char *)__unsafe_null_terminated_to_indexable(string);

    /* set "flag" to 1 if something needs to be escaped */
    for (input_pointer = input; *input_pointer; input_pointer++)
    {
        switch (*input_pointer)
        {
            case '\"':
            case '\\':
            case '\b':
            case '\f':
            case '\n':
            case '\r':
            case '\t':
                /* one character escape sequence */
                escape_characters++;
                break;
            default:
                if (*input_pointer < 32)
                {
                    /* UTF-16 escape sequence uXXXX */
                    escape_characters += 5;
                }
                break;
        }
    }
    output_length = (size_t)(input_pointer - input) + escape_characters;

    output = ensure(output_buffer, output_length + sizeof("\"\""));
    if (output == NULL)
    {
        return false;
    }

    /* no characters have to be escaped */
    if (escape_characters == 0)
    {
        output[0] = '\"';
        memcpy(output + 1, input, output_length);
        output[output_length + 1] = '\"';
        output[output_length + 2] = '\0';

        return true;
    }

    output[0] = '\"';
    output_pointer = output + 1;
    /* copy the string */
    for (input_pointer = input; *input_pointer != '\0'; (void)input_pointer++, output_pointer++)
    {
        if ((*input_pointer > 31) && (*input_pointer != '\"') && (*input_pointer != '\\'))
        {
            /* normal character, copy */
            *output_pointer = *input_pointer;
        }
        else
        {
            /* character needs to be escaped */
            *output_pointer++ = '\\';
            switch (*input_pointer)
            {
                case '\\':
                    *output_pointer = '\\';
                    break;
                case '\"':
                    *output_pointer = '\"';
                    break;
                case '\b':
                    *output_pointer = 'b';
                    break;
                case '\f':
                    *output_pointer = 'f';
                    break;
                case '\n':
                    *output_pointer = 'n';
                    break;
                case '\r':
                    *output_pointer = 'r';
                    break;
                case '\t':
                    *output_pointer = 't';
                    break;
                default:
                    /* escape and print as unicode codepoint */
                    /* 6 bytes: uXXXX plus the terminator snprintf adds, which the next
                       character or the closing quote overwrites */
                    snprintf((char*)output_pointer, 6, "u%04x", *input_pointer);
                    output_pointer += 4;
                    break;
            }
        }
    }
    output[output_length + 1] = '\"';
    output[output_length + 2] = '\0';

    return true;
}

/* Invoke print_string_ptr (which is useful) on an item. */
static cJSON_bool print_string(const cJSON * const item, printbuffer *_Exclusive const p)
{
    return print_string_ptr(item->valuestring, p);
}

/* Predeclare these prototypes. */
static cJSON_bool parse_value(cJSON *_Exclusive const item _Noescape, parse_buffer * const input_buffer);
static cJSON_bool print_value(const cJSON * const item, printbuffer *_Exclusive const output_buffer);
static cJSON_bool parse_array(cJSON *_Exclusive const item _Noescape, parse_buffer * const input_buffer);
static cJSON_bool print_array(const cJSON * const item, printbuffer *_Exclusive const output_buffer);
static cJSON_bool parse_object(cJSON *_Exclusive const item _Noescape, parse_buffer * const input_buffer);
static cJSON_bool print_object(const cJSON * const item, printbuffer *_Exclusive const output_buffer);

/* Utility to jump whitespace and cr/lf */
static parse_buffer *_Nullable buffer_skip_whitespace(parse_buffer * const buffer _Lifetimebound)
{
    if ((buffer == NULL) || (buffer->content == NULL))
    {
        return NULL;
    }

    if (cannot_access_at_index(buffer, 0))
    {
        return buffer;
    }

    while (can_access_at_index(buffer, 0) && (buffer_at_offset(buffer)[0] <= 32))
    {
       buffer->offset++;
    }

    if (buffer->offset == buffer->length)
    {
        buffer->offset--;
    }

    return buffer;
}

/* skip the UTF-8 BOM (byte order mark) if it is at the beginning of a buffer */
static parse_buffer *_Nullable skip_utf8_bom(parse_buffer * const buffer _Lifetimebound)
{
    if ((buffer == NULL) || (buffer->content == NULL) || (buffer->offset != 0))
    {
        return NULL;
    }

    if (can_access_at_index(buffer, 4) && (memcmp(buffer_at_offset(buffer), "\xEF\xBB\xBF", 3) == 0))
    {
        buffer->offset += 3;
    }

    return buffer;
}

CJSON_PUBLIC(cJSON *_Nullable) cJSON_ParseWithOpts(const char *value, const char *_Nullable __unsafe_indexable *_Nullable return_parse_end, cJSON_bool require_null_terminated)
{
    size_t buffer_length;

    if (NULL == value)
    {
        return NULL;
    }

    /* Adding null character size due to require_null_terminated. */
    buffer_length = strlen(value) + sizeof("");

    /* the length counts the terminator, so the bounds must include it; value
       is const, so nothing can erase the terminator through these bounds */
    return cJSON_ParseWithLengthOpts(__unsafe_null_terminated_to_indexable(value), buffer_length, return_parse_end, require_null_terminated);
}

/* Parse an object - create a new root, and populate. */
CJSON_PUBLIC(cJSON *_Nullable) cJSON_ParseWithLengthOpts(const char *_Nullable __counted_by_or_null(buffer_length) value, size_t buffer_length, const char *_Nullable __unsafe_indexable *_Nullable return_parse_end, cJSON_bool require_null_terminated)
{
    parse_buffer buffer = { 0, 0, 0, 0, { 0, 0, 0 } };
    cJSON *_Nullable _Owned item = NULL;

    /* reset error position */
    global_error.json = NULL;
    global_error.position = 0;

    if (value == NULL || 0 == buffer_length)
    {
        goto fail;
    }

    buffer.content = (const unsigned char*)value;
    buffer.length = buffer_length;
    buffer.offset = 0;
    buffer.hooks = global_hooks;

    item = cJSON_New_Item(&global_hooks);
    if (item == NULL) /* memory fail */
    {
        goto fail;
    }

    /* two calls: what a call returns cannot be told apart from other buffers */
    skip_utf8_bom(&buffer);
    buffer_skip_whitespace(&buffer);
    if (!parse_value(item, &buffer))
    {
        /* parse failure. ep is set. */
        goto fail;
    }

    /* if we require null-terminated JSON without appended garbage, skip and then check for a null terminator */
    if (require_null_terminated)
    {
        buffer_skip_whitespace(&buffer);
        if ((buffer.offset >= buffer.length) || buffer_at_offset(&buffer)[0] != '\0')
        {
            goto fail;
        }
    }
    if (return_parse_end)
    {
        *return_parse_end = (const char*)buffer_at_offset(&buffer);
    }

    return item;

fail:
    if (item != NULL)
    {
        cJSON_Delete(item);
    }

    if (value != NULL)
    {
        error local_error;
        local_error.json = (const unsigned char*)value;
        local_error.position = 0;

        if (buffer.offset < buffer.length)
        {
            local_error.position = buffer.offset;
        }
        else if (buffer.length > 0)
        {
            local_error.position = buffer.length - 1;
        }

        if (return_parse_end != NULL)
        {
            *return_parse_end = value + local_error.position;
        }

        global_error = local_error;
    }

    return NULL;
}

/* Default options for cJSON_Parse */
CJSON_PUBLIC(cJSON *_Nullable) cJSON_Parse(const char *value)
{
    return cJSON_ParseWithOpts(value, 0, 0);
}

CJSON_PUBLIC(cJSON *_Nullable) cJSON_ParseWithLength(const char *_Nullable __counted_by_or_null(buffer_length) value, size_t buffer_length)
{
    return cJSON_ParseWithLengthOpts(value, buffer_length, 0, 0);
}

#define cjson_min(a, b) (((a) < (b)) ? (a) : (b))

_Returns_owned static char *_Nullable __null_terminated print(const cJSON *_Nullable const item, cJSON_bool format, const internal_hooks * const hooks)
{
    static const size_t default_buffer_size = 256;
    printbuffer buffer[1];
    unsigned char *_Nullable _Owned printed = NULL;
    size_t printed_length = 0;

    memset(buffer, 0, sizeof(buffer));

    /* create buffer */
    buffer->buffer = allocate_bytes(hooks, default_buffer_size);
    buffer->length = default_buffer_size;
    buffer->format = format;
    buffer->hooks = *hooks;
    if (buffer->buffer == NULL)
    {
        goto fail;
    }

    /* print the value */
    if (!print_value(item, buffer))
    {
        goto fail;
    }
    update_offset(buffer);
    printed_length = buffer->offset;

    /* check if reallocate is available */
    if (hooks->reallocate != NULL)
    {
        /* reallocate_buffer frees the buffer if it fails */
        printed = reallocate_buffer(buffer, printed_length + 1);
        if (printed == NULL) {
            goto fail;
        }
    }
    else /* otherwise copy the JSON over to a new buffer */
    {
        unsigned char *_Nullable source = buffer->buffer;
        printed = allocate_bytes(hooks, buffer->offset + 1);
        if ((printed == NULL) || (source == NULL))
        {
            goto fail;
        }
        memcpy(printed, source, cjson_min(buffer->length, buffer->offset + 1));
        printed[buffer->offset] = '\0'; /* just to be sure */

        /* free the buffer */
        hooks->deallocate(_Take(buffer[0].buffer));
        buffer->buffer = NULL;
        buffer->length = 0;
    }

    /* without Safe C the conversion macro drops its second argument */
    (void)printed_length;
    /* unsafe: printed[buffer->offset] was set to 0 above, and printed_length is that offset */
    __unsafe { return __unsafe_null_terminated_from_indexable((char*)printed, (char*)printed + printed_length); }

fail:
    hooks->deallocate(_Take(buffer[0].buffer));
    buffer->buffer = NULL;
    buffer->length = 0;

    hooks->deallocate(printed);

    return NULL;
}

/* Render a cJSON item/entity/structure to text. */
CJSON_PUBLIC(char *_Nullable __null_terminated) cJSON_Print(const cJSON *item)
{
    return print(item, true, &global_hooks);
}

CJSON_PUBLIC(char *_Nullable __null_terminated) cJSON_PrintUnformatted(const cJSON *item)
{
    return print(item, false, &global_hooks);
}

CJSON_PUBLIC(char *_Nullable __null_terminated) cJSON_PrintBuffered(const cJSON *item, int prebuffer, cJSON_bool fmt)
{
    printbuffer p = { 0, 0, 0, 0, 0, 0, { 0, 0, 0 } };

    if (prebuffer < 0)
    {
        return NULL;
    }

    p.buffer = allocate_bytes(&global_hooks, (size_t)prebuffer);
    p.length = (size_t)prebuffer;
    if (!p.buffer)
    {
        return NULL;
    }

    p.offset = 0;
    p.noalloc = false;
    p.format = fmt;
    p.hooks = global_hooks;

    if (!print_value(item, &p))
    {
        global_hooks.deallocate(_Take(p.buffer));
        p.buffer = NULL;
        p.length = 0;
        return NULL;
    }

    /* scans for the terminator print_value wrote, and traps if there is none */
    __unsafe { return __unsafe_null_terminated_from_indexable((char*)p.buffer); }
}

CJSON_PUBLIC(cJSON_bool) cJSON_PrintPreallocated(cJSON *item, char *_Nullable __counted_by_or_null(length) buffer, const int length, const cJSON_bool format)
{
    printbuffer p = { 0, 0, 0, 0, 0, 0, { 0, 0, 0 } };

    if ((length < 0) || (buffer == NULL))
    {
        return false;
    }

    /* unsafe, GAP: an owned field that sometimes borrows (noalloc); ensure never frees or grows a noalloc buffer, and it is taken back below */
    __unsafe { p.buffer = (unsigned char*)buffer; }
    p.length = (size_t)length;
    p.offset = 0;
    p.noalloc = true;
    p.format = format;
    p.hooks = global_hooks;

    cJSON_bool ok = print_value(item, &p);
    /* the caller's buffer: forget it, do not free it */
    (void)_Take(p.buffer);
    p.buffer = NULL;
    p.length = 0;
    return ok;
}

/* Parser core - when encountering text, process appropriately. */
static cJSON_bool parse_value(cJSON *_Exclusive const item _Noescape, parse_buffer * const input_buffer)
{
    if ((input_buffer == NULL) || (input_buffer->content == NULL))
    {
        return false; /* no input */
    }

    /* parse the different types of values */
    /* null */
    if (can_read(input_buffer, 4) && (memcmp(buffer_at_offset(input_buffer), "null", 4) == 0))
    {
        item->type = cJSON_NULL;
        input_buffer->offset += 4;
        return true;
    }
    /* false */
    if (can_read(input_buffer, 5) && (memcmp(buffer_at_offset(input_buffer), "false", 5) == 0))
    {
        item->type = cJSON_False;
        input_buffer->offset += 5;
        return true;
    }
    /* true */
    if (can_read(input_buffer, 4) && (memcmp(buffer_at_offset(input_buffer), "true", 4) == 0))
    {
        item->type = cJSON_True;
        item->valueint = 1;
        input_buffer->offset += 4;
        return true;
    }
    /* string */
    if (can_access_at_index(input_buffer, 0) && (buffer_at_offset(input_buffer)[0] == '\"'))
    {
        return parse_string(item, input_buffer);
    }
    /* number */
    if (can_access_at_index(input_buffer, 0) && ((buffer_at_offset(input_buffer)[0] == '-') || ((buffer_at_offset(input_buffer)[0] >= '0') && (buffer_at_offset(input_buffer)[0] <= '9'))))
    {
        return parse_number(item, input_buffer);
    }
    /* array */
    if (can_access_at_index(input_buffer, 0) && (buffer_at_offset(input_buffer)[0] == '['))
    {
        return parse_array(item, input_buffer);
    }
    /* object */
    if (can_access_at_index(input_buffer, 0) && (buffer_at_offset(input_buffer)[0] == '{'))
    {
        return parse_object(item, input_buffer);
    }

    return false;
}

/* Render a value to text. */
static cJSON_bool print_value(const cJSON * const item, printbuffer *_Exclusive const output_buffer)
{
    unsigned char *output = NULL;

    if ((item == NULL) || (output_buffer == NULL))
    {
        return false;
    }

    switch ((item->type) & 0xFF)
    {
        case cJSON_NULL:
            output = ensure(output_buffer, 5);
            if (output == NULL)
            {
                return false;
            }
            memcpy(output, "null", sizeof("null"));
            return true;

        case cJSON_False:
            output = ensure(output_buffer, 6);
            if (output == NULL)
            {
                return false;
            }
            memcpy(output, "false", sizeof("false"));
            return true;

        case cJSON_True:
            output = ensure(output_buffer, 5);
            if (output == NULL)
            {
                return false;
            }
            memcpy(output, "true", sizeof("true"));
            return true;

        case cJSON_Number:
            return print_number(item, output_buffer);

        case cJSON_Raw:
        {
            size_t raw_length = 0;
            if (item->valuestring == NULL)
            {
                return false;
            }

            raw_length = strlen(item->valuestring) + sizeof("");
            output = ensure(output_buffer, raw_length);
            if (output == NULL)
            {
                return false;
            }
            /* the indexable view excludes the terminator: copy it separately */
            memcpy(output, __null_terminated_to_indexable(item->valuestring), raw_length - 1);
            output[raw_length - 1] = '\0';
            return true;
        }

        case cJSON_String:
            return print_string(item, output_buffer);

        case cJSON_Array:
            return print_array(item, output_buffer);

        case cJSON_Object:
            return print_object(item, output_buffer);

        default:
            return false;
    }
}

/* Build an array from input text. */
static cJSON_bool parse_array(cJSON *_Exclusive const item _Noescape, parse_buffer * const input_buffer)
{
    cJSON *_Nullable current_item = NULL;

    if (input_buffer->depth >= CJSON_NESTING_LIMIT)
    {
        return false; /* to deeply nested */
    }
    input_buffer->depth++;

    if (buffer_at_offset(input_buffer)[0] != '[')
    {
        /* not an array */
        goto fail;
    }

    input_buffer->offset++;
    buffer_skip_whitespace(input_buffer);
    if (can_access_at_index(input_buffer, 0) && (buffer_at_offset(input_buffer)[0] == ']'))
    {
        /* empty array */
        goto success;
    }

    /* check if we skipped to the end of the buffer */
    if (cannot_access_at_index(input_buffer, 0))
    {
        input_buffer->offset--;
        goto fail;
    }

    /* step back to character in front of the first element */
    input_buffer->offset--;
    /* loop through the comma separated array elements */
    do
    {
        /* allocate next item */
        cJSON *_Nullable _Owned new_item = cJSON_New_Item(&(input_buffer->hooks));
        if (new_item == NULL)
        {
            goto fail; /* allocation failure */
        }


        /* parse next value */
        input_buffer->offset++;
        buffer_skip_whitespace(input_buffer);
        if (!parse_value(new_item, input_buffer))
        {
            cJSON_Delete(new_item);
            goto fail; /* failed to parse value */
        }
        buffer_skip_whitespace(input_buffer);

        /* parsed while this code still owned it; the list holds it from now on */
        current_item = append_child(item, new_item);
        if (current_item == NULL)
        {
            goto fail;
        }
    }
    while (can_access_at_index(input_buffer, 0) && (buffer_at_offset(input_buffer)[0] == ','));

    if (cannot_access_at_index(input_buffer, 0) || buffer_at_offset(input_buffer)[0] != ']')
    {
        goto fail; /* expected end of array */
    }

success:
    input_buffer->depth--;

    item->type = cJSON_Array;

    input_buffer->offset++;

    return true;

fail:
    cJSON_Delete(_Take(item->child));

    return false;
}

/* Render an array to text */
static cJSON_bool print_array(const cJSON * const item, printbuffer *_Exclusive const output_buffer)
{
    unsigned char *output_pointer = NULL;
    size_t length = 0;
    cJSON *current_element = item->child;

    if (output_buffer == NULL)
    {
        return false;
    }

    if (output_buffer->depth >= CJSON_NESTING_LIMIT)
    {
        return false; /* nesting is too deep */
    }

    /* Compose the output array. */
    /* opening square bracket */
    output_pointer = ensure(output_buffer, 1);
    if (output_pointer == NULL)
    {
        return false;
    }

    *output_pointer = '[';
    output_buffer->offset++;
    output_buffer->depth++;

    while (current_element != NULL)
    {
        if (!print_value(current_element, output_buffer))
        {
            return false;
        }
        update_offset(output_buffer);
        if (current_element->next)
        {
            length = (size_t) (output_buffer->format ? 2 : 1);
            output_pointer = ensure(output_buffer, length + 1);
            if (output_pointer == NULL)
            {
                return false;
            }
            *output_pointer++ = ',';
            if(output_buffer->format)
            {
                *output_pointer++ = ' ';
            }
            *output_pointer = '\0';
            output_buffer->offset += length;
        }
        current_element = current_element->next;
    }

    output_pointer = ensure(output_buffer, 2);
    if (output_pointer == NULL)
    {
        return false;
    }
    *output_pointer++ = ']';
    *output_pointer = '\0';
    output_buffer->depth--;

    return true;
}

/* Build an object from the text. */
static cJSON_bool parse_object(cJSON *_Exclusive const item _Noescape, parse_buffer * const input_buffer)
{
    cJSON *_Nullable current_item = NULL;

    if (input_buffer->depth >= CJSON_NESTING_LIMIT)
    {
        return false; /* to deeply nested */
    }
    input_buffer->depth++;

    if (cannot_access_at_index(input_buffer, 0) || (buffer_at_offset(input_buffer)[0] != '{'))
    {
        goto fail; /* not an object */
    }

    input_buffer->offset++;
    buffer_skip_whitespace(input_buffer);
    if (can_access_at_index(input_buffer, 0) && (buffer_at_offset(input_buffer)[0] == '}'))
    {
        goto success; /* empty object */
    }

    /* check if we skipped to the end of the buffer */
    if (cannot_access_at_index(input_buffer, 0))
    {
        input_buffer->offset--;
        goto fail;
    }

    /* step back to character in front of the first element */
    input_buffer->offset--;
    /* loop through the comma separated array elements */
    do
    {
        /* allocate next item */
        cJSON *_Nullable _Owned new_item = cJSON_New_Item(&(input_buffer->hooks));
        if (new_item == NULL)
        {
            goto fail; /* allocation failure */
        }


        if (cannot_access_at_index(input_buffer, 1))
        {
            cJSON_Delete(new_item);
            goto fail; /* nothing comes after the comma */
        }

        /* parse the name of the child */
        input_buffer->offset++;
        buffer_skip_whitespace(input_buffer);
        if (!parse_string(new_item, input_buffer))
        {
            cJSON_Delete(new_item);
            goto fail; /* failed to parse name */
        }
        buffer_skip_whitespace(input_buffer);

        /* swap valuestring and string, because we parsed the name */
        key_from_valuestring(new_item);

        if (cannot_access_at_index(input_buffer, 0) || (buffer_at_offset(input_buffer)[0] != ':'))
        {
            cJSON_Delete(new_item);
            goto fail; /* invalid object */
        }

        /* parse the value */
        input_buffer->offset++;
        buffer_skip_whitespace(input_buffer);
        if (!parse_value(new_item, input_buffer))
        {
            cJSON_Delete(new_item);
            goto fail; /* failed to parse value */
        }
        buffer_skip_whitespace(input_buffer);

        /* parsed while this code still owned it; the list holds it from now on */
        current_item = append_child(item, new_item);
        if (current_item == NULL)
        {
            goto fail;
        }
    }
    while (can_access_at_index(input_buffer, 0) && (buffer_at_offset(input_buffer)[0] == ','));

    if (cannot_access_at_index(input_buffer, 0) || (buffer_at_offset(input_buffer)[0] != '}'))
    {
        goto fail; /* expected end of object */
    }

success:
    input_buffer->depth--;

    item->type = cJSON_Object;

    input_buffer->offset++;
    return true;

fail:
    cJSON_Delete(_Take(item->child));

    return false;
}

/* Render an object to text. */
static cJSON_bool print_object(const cJSON * const item, printbuffer *_Exclusive const output_buffer)
{
    unsigned char *output_pointer = NULL;
    size_t length = 0;
    cJSON *current_item = item->child;

    if (output_buffer == NULL)
    {
        return false;
    }

    if (output_buffer->depth >= CJSON_NESTING_LIMIT)
    {
        return false; /* nesting is too deep */
    }

    /* Compose the output: */
    length = (size_t) (output_buffer->format ? 2 : 1); /* fmt: {\n */
    output_pointer = ensure(output_buffer, length + 1);
    if (output_pointer == NULL)
    {
        return false;
    }

    *output_pointer++ = '{';
    output_buffer->depth++;
    if (output_buffer->format)
    {
        *output_pointer++ = '\n';
    }
    output_buffer->offset += length;

    while (current_item)
    {
        if (output_buffer->format)
        {
            size_t i;
            output_pointer = ensure(output_buffer, output_buffer->depth);
            if (output_pointer == NULL)
            {
                return false;
            }
            for (i = 0; i < output_buffer->depth; i++)
            {
                *output_pointer++ = '\t';
            }
            output_buffer->offset += output_buffer->depth;
        }

        /* print key */
        if (!print_string_ptr(current_item->string, output_buffer))
        {
            return false;
        }
        update_offset(output_buffer);

        length = (size_t) (output_buffer->format ? 2 : 1);
        output_pointer = ensure(output_buffer, length);
        if (output_pointer == NULL)
        {
            return false;
        }
        *output_pointer++ = ':';
        if (output_buffer->format)
        {
            *output_pointer++ = '\t';
        }
        output_buffer->offset += length;

        /* print value */
        if (!print_value(current_item, output_buffer))
        {
            return false;
        }
        update_offset(output_buffer);

        /* print comma if not last */
        length = ((size_t)(output_buffer->format ? 1 : 0) + (size_t)(current_item->next ? 1 : 0));
        output_pointer = ensure(output_buffer, length + 1);
        if (output_pointer == NULL)
        {
            return false;
        }
        if (current_item->next)
        {
            *output_pointer++ = ',';
        }

        if (output_buffer->format)
        {
            *output_pointer++ = '\n';
        }
        *output_pointer = '\0';
        output_buffer->offset += length;

        current_item = current_item->next;
    }

    output_pointer = ensure(output_buffer, output_buffer->format ? (output_buffer->depth + 1) : 2);
    if (output_pointer == NULL)
    {
        return false;
    }
    if (output_buffer->format)
    {
        size_t i;
        for (i = 0; i < (output_buffer->depth - 1); i++)
        {
            *output_pointer++ = '\t';
        }
    }
    *output_pointer++ = '}';
    *output_pointer = '\0';
    output_buffer->depth--;

    return true;
}

/* Get Array size/item / object item. */
CJSON_PUBLIC(int) cJSON_GetArraySize(const cJSON *array)
{
    cJSON *child = NULL;
    size_t size = 0;

    if (array == NULL)
    {
        return 0;
    }

    child = array->child;

    while(child != NULL)
    {
        size++;
        child = child->next;
    }

    /* FIXME: Can overflow here. Cannot be fixed without breaking the API */

    return (int)size;
}

static cJSON *_Nullable get_array_item(const cJSON *array _Lifetimebound, size_t index)
{
    cJSON *_Nullable current_child = NULL;

    if (array == NULL)
    {
        return NULL;
    }

    current_child = first_child(array);
    while ((current_child != NULL) && (index > 0))
    {
        index--;
        current_child = next_sibling(current_child);
    }

    return current_child;
}

CJSON_PUBLIC(cJSON *_Nullable) cJSON_GetArrayItem(const cJSON *array _Lifetimebound, int index)
{
    if (index < 0)
    {
        return NULL;
    }

    return get_array_item(array, (size_t)index);
}

static cJSON *_Nullable get_object_item(const cJSON * const object _Lifetimebound, const char * const name, const cJSON_bool case_sensitive)
{
    cJSON *_Nullable current_element = NULL;

    if ((object == NULL) || (name == NULL))
    {
        return NULL;
    }

    current_element = first_child(object);
    if (case_sensitive)
    {
        while ((current_element != NULL) && (current_element->string != NULL) && (strcmp(name, current_element->string) != 0))
        {
            current_element = next_sibling(current_element);
        }
    }
    else
    {
        while ((current_element != NULL) && (case_insensitive_strcmp(name, current_element->string) != 0))
        {
            current_element = next_sibling(current_element);
        }
    }

    if ((current_element == NULL) || (current_element->string == NULL)) {
        return NULL;
    }

    return current_element;
}

CJSON_PUBLIC(cJSON *_Nullable) cJSON_GetObjectItem(const cJSON * const object _Lifetimebound, const char * const string)
{
    return get_object_item(object, string, false);
}

CJSON_PUBLIC(cJSON *_Nullable) cJSON_GetObjectItemCaseSensitive(const cJSON * const object _Lifetimebound, const char * const string)
{
    return get_object_item(object, string, true);
}

CJSON_PUBLIC(cJSON_bool) cJSON_HasObjectItem(const cJSON *object, const char *string)
{
    return cJSON_GetObjectItem(object, string) ? 1 : 0;
}

#if defined(__clang__) || (defined(__GNUC__)  && ((__GNUC__ > 4) || ((__GNUC__ == 4) && (__GNUC_MINOR__ > 5))))
    #pragma GCC diagnostic push
#endif
#ifdef __GNUC__
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif
/* The two places where a pointer cJSON does not own enters an owned field.
   GAP: an owned field whose ownership depends on a flag (cJSON_IsReference,
   cJSON_StringIsConst) and an API that moves an item only when the call
   succeeds cannot be stated with kinds. Rust would use Cow or an enum, and
   Result<(), Box<T>> to hand the item back on failure. */

/* A caller's item entering a tree: cJSON_AddItemToArray and friends take
   the item on success, and the caller gives up its pointer by convention.
   Also the borrowed child of a reference item, which cJSON_Delete skips
   because of cJSON_IsReference. */
/* unsafe: see above; the const cast is cJSON's API returning non-const pointers into const input.
   GAP: an __unsafe block does not lift the borrow-return rule, so the whole function is __unsafe. */
__unsafe _Returns_owned static cJSON *adopt_item(const cJSON *item)
{
    return (cJSON*)item;
}

/* A string the item does not own: a constant key (cJSON_StringIsConst) or a
   string reference's valuestring (cJSON_IsReference), never freed by cJSON_Delete. */
/* unsafe: see above */
__unsafe _Returns_owned static char *__null_terminated adopt_string(const char *__null_terminated string)
{
    return (char*)string;
}
#if defined(__clang__) || (defined(__GNUC__)  && ((__GNUC__ > 4) || ((__GNUC__ == 4) && (__GNUC_MINOR__ > 5))))
    #pragma GCC diagnostic pop
#endif

/* Utility for handling references. */
_Returns_owned static cJSON *_Nullable create_reference(const cJSON *_Nullable item, const internal_hooks * const hooks)
{
    cJSON *_Nullable _Owned reference = NULL;
    if (item == NULL)
    {
        return NULL;
    }

    reference = cJSON_New_Item(hooks);
    if (reference == NULL)
    {
        return NULL;
    }

    /* a copy of item without its key and siblings; child and valuestring are borrowed */
    reference->type = item->type | cJSON_IsReference;
    reference->valueint = item->valueint;
    reference->valuedouble = item->valuedouble;
    if (item->child != NULL)
    {
        reference->child = adopt_item(item->child);
    }
    if (item->valuestring != NULL)
    {
        reference->valuestring = adopt_string(item->valuestring);
    }
    return reference;
}

static cJSON_bool add_item_to_array(cJSON *_Nullable _Exclusive array, cJSON *_Nullable item)
{
    if ((item == NULL) || (array == NULL) || (array == item))
    {
        return false;
    }

    (void)append_child(array, adopt_item(item));

    return true;
}

/* Add item to array/object. */
CJSON_PUBLIC(cJSON_bool) cJSON_AddItemToArray(cJSON *_Exclusive array, cJSON *item)
{
    return add_item_to_array(array, item);
}

/* Gives item its key and appends it to object; returns item, now borrowed from object. */
static cJSON *_Nullable attach_to_object(cJSON *_Exclusive const object _Lifetimebound, char *_Owned __null_terminated key, cJSON *_Owned item, const internal_hooks * const hooks, const cJSON_bool constant_key)
{
    if (!(item->type & cJSON_StringIsConst))
    {
        hooks->deallocate(_Take(item->string));
    }
    else
    {
        /* a constant key is not ours to free */
        (void)_Take(item->string);
    }

    item->string = key;
    item->type = constant_key ? (item->type | cJSON_StringIsConst) : (item->type & ~cJSON_StringIsConst);

    return append_child(object, item);
}

_Returns_owned static char *_Nullable __null_terminated make_key(const char *__null_terminated string, const internal_hooks * const hooks, const cJSON_bool constant_key)
{
    if (constant_key)
    {
        return adopt_string(string);
    }
    return cJSON_strdup(string, hooks);
}

static cJSON_bool add_item_to_object(cJSON *_Nullable _Exclusive const object, const char *_Nullable const string, cJSON *_Nullable const item, const internal_hooks * const hooks, const cJSON_bool constant_key)
{
    char *_Nullable _Owned __null_terminated new_key = NULL;

    if ((object == NULL) || (string == NULL) || (item == NULL) || (object == item))
    {
        return false;
    }

    new_key = make_key(string, hooks, constant_key);
    if (new_key == NULL)
    {
        return false;
    }

    /* the last failure is behind us, so the item moves into the tree */
    (void)attach_to_object(object, new_key, adopt_item(item), hooks, constant_key);
    return true;
}

/* For the cJSON_Add*ToObject helpers: adds a new item, or frees it on failure. */
static cJSON *_Nullable add_new_to_object(cJSON *_Nullable _Exclusive const object _Lifetimebound, const char *_Nullable const name, cJSON *_Nullable _Owned item)
{
    char *_Nullable _Owned __null_terminated key = NULL;

    if (item == NULL)
    {
        return NULL;
    }
    if ((object == NULL) || (name == NULL))
    {
        cJSON_Delete(item);
        return NULL;
    }
    key = cJSON_strdup(name, &global_hooks);
    if (key == NULL)
    {
        cJSON_Delete(item);
        return NULL;
    }

    return attach_to_object(object, key, item, &global_hooks, false);
}

CJSON_PUBLIC(cJSON_bool) cJSON_AddItemToObject(cJSON *_Exclusive object, const char *string, cJSON *item)
{
    return add_item_to_object(object, string, item, &global_hooks, false);
}

/* Add an item to an object with constant string as key */
CJSON_PUBLIC(cJSON_bool) cJSON_AddItemToObjectCS(cJSON *_Exclusive object, const char *string, cJSON *item)
{
    return add_item_to_object(object, string, item, &global_hooks, true);
}

CJSON_PUBLIC(cJSON_bool) cJSON_AddItemReferenceToArray(cJSON *_Exclusive array, cJSON *item)
{
    cJSON *_Nullable _Owned reference = NULL;
    if (array == NULL)
    {
        return false;
    }

    reference = create_reference(item, &global_hooks);
    if (reference == NULL)
    {
        return false;
    }
    (void)append_child(array, reference);
    return true;
}

CJSON_PUBLIC(cJSON_bool) cJSON_AddItemReferenceToObject(cJSON *_Exclusive object, const char *string, cJSON *item)
{
    char *_Nullable _Owned __null_terminated key = NULL;
    cJSON *_Nullable _Owned reference = NULL;
    if ((object == NULL) || (string == NULL))
    {
        return false;
    }

    reference = create_reference(item, &global_hooks);
    if (reference == NULL)
    {
        return false;
    }
    key = cJSON_strdup(string, &global_hooks);
    if (key == NULL)
    {
        cJSON_Delete(reference);
        return false;
    }
    (void)attach_to_object(object, key, reference, &global_hooks, false);
    return true;
}

CJSON_PUBLIC(cJSON *_Nullable) cJSON_AddNullToObject(cJSON *_Exclusive const object _Lifetimebound, const char * const name)
{
    return add_new_to_object(object, name, cJSON_CreateNull());
}

CJSON_PUBLIC(cJSON *_Nullable) cJSON_AddTrueToObject(cJSON *_Exclusive const object _Lifetimebound, const char * const name)
{
    return add_new_to_object(object, name, cJSON_CreateTrue());
}

CJSON_PUBLIC(cJSON *_Nullable) cJSON_AddFalseToObject(cJSON *_Exclusive const object _Lifetimebound, const char * const name)
{
    return add_new_to_object(object, name, cJSON_CreateFalse());
}

CJSON_PUBLIC(cJSON *_Nullable) cJSON_AddBoolToObject(cJSON *_Exclusive const object _Lifetimebound, const char * const name, const cJSON_bool boolean)
{
    return add_new_to_object(object, name, cJSON_CreateBool(boolean));
}

CJSON_PUBLIC(cJSON *_Nullable) cJSON_AddNumberToObject(cJSON *_Exclusive const object _Lifetimebound, const char * const name, const double number)
{
    return add_new_to_object(object, name, cJSON_CreateNumber(number));
}

CJSON_PUBLIC(cJSON *_Nullable) cJSON_AddStringToObject(cJSON *_Exclusive const object _Lifetimebound, const char * const name, const char * const string)
{
    return add_new_to_object(object, name, cJSON_CreateString(string));
}

CJSON_PUBLIC(cJSON *_Nullable) cJSON_AddRawToObject(cJSON *_Exclusive const object _Lifetimebound, const char * const name, const char * const raw)
{
    return add_new_to_object(object, name, cJSON_CreateRaw(raw));
}

CJSON_PUBLIC(cJSON *_Nullable) cJSON_AddObjectToObject(cJSON *_Exclusive const object _Lifetimebound, const char * const name)
{
    return add_new_to_object(object, name, cJSON_CreateObject());
}

CJSON_PUBLIC(cJSON *_Nullable) cJSON_AddArrayToObject(cJSON *_Exclusive const object _Lifetimebound, const char * const name)
{
    return add_new_to_object(object, name, cJSON_CreateArray());
}

/* Detach, insert and replace are pointer surgery through prev, as in
   upstream cJSON (and Rust's LinkedList, which does the same in unsafe).
   item names a child of parent only by identity; it is _Unchecked so a
   caller may pass a borrow of parent next to parent itself. */
CJSON_PUBLIC(cJSON *_Nullable) cJSON_DetachItemViaPointer(cJSON *_Exclusive parent, cJSON *_Unchecked const item)
{
    if (parent == NULL)
    {
        return NULL;
    }
    /* unsafe: item is a child of parent (the API's precondition, checked as far as upstream does), parent is lent exclusively,
       and the moves below keep each item owned by exactly one next/child field */
    __unsafe
    {
        if ((item == NULL) || (item != parent->child && item->prev == NULL))
        {
            return NULL;
        }

        if ((item != parent->child) && (item->prev != NULL))
        {
            /* not the first element */
            item->prev->next = item->next;
        }
        if (item->next != NULL)
        {
            /* not the last element */
            item->next->prev = item->prev;
        }

        if (item == parent->child)
        {
            /* first element */
            parent->child = item->next;
        }
        else if ((item->next == NULL) && (parent->child != NULL))
        {
            /* last element */
            parent->child->prev = item->prev;
        }

        /* make sure the detached item doesn't point anywhere anymore */
        item->prev = NULL;
        item->next = NULL;

        return adopt_item(item);
    }
}

CJSON_PUBLIC(cJSON *_Nullable) cJSON_DetachItemFromArray(cJSON *_Exclusive array, int which)
{
    if (which < 0)
    {
        return NULL;
    }

    return cJSON_DetachItemViaPointer(array, get_array_item(array, (size_t)which));
}

CJSON_PUBLIC(void) cJSON_DeleteItemFromArray(cJSON *_Exclusive array, int which)
{
    cJSON_Delete(cJSON_DetachItemFromArray(array, which));
}

CJSON_PUBLIC(cJSON *_Nullable) cJSON_DetachItemFromObject(cJSON *_Exclusive object, const char *string)
{
    return cJSON_DetachItemViaPointer(object, cJSON_GetObjectItem(object, string));
}

CJSON_PUBLIC(cJSON *_Nullable) cJSON_DetachItemFromObjectCaseSensitive(cJSON *_Exclusive object, const char *string)
{
    return cJSON_DetachItemViaPointer(object, cJSON_GetObjectItemCaseSensitive(object, string));
}

CJSON_PUBLIC(void) cJSON_DeleteItemFromObject(cJSON *_Exclusive object, const char *string)
{
    cJSON_Delete(cJSON_DetachItemFromObject(object, string));
}

CJSON_PUBLIC(void) cJSON_DeleteItemFromObjectCaseSensitive(cJSON *_Exclusive object, const char *string)
{
    cJSON_Delete(cJSON_DetachItemFromObjectCaseSensitive(object, string));
}

/* Replace array/object items with new ones. */
CJSON_PUBLIC(cJSON_bool) cJSON_InsertItemInArray(cJSON *_Exclusive array, int which, cJSON *newitem)
{
    cJSON *_Nullable after_inserted = NULL;

    if (which < 0 || newitem == NULL || array == NULL)
    {
        return false;
    }

    after_inserted = get_array_item(array, (size_t)which);
    if (after_inserted == NULL)
    {
        return add_item_to_array(array, newitem);
    }

    /* unsafe: after_inserted is a child of array (lent exclusively); newitem moves in on success, as cJSON's API says */
    __unsafe
    {
        cJSON *_Nullable before_inserted = NULL;
        if (after_inserted != array->child && after_inserted->prev == NULL) {
            /* return false if after_inserted is a corrupted array item */
            return false;
        }

        before_inserted = after_inserted->prev;
        newitem->prev = before_inserted;
        after_inserted->prev = newitem;
        if (after_inserted == array->child)
        {
            newitem->next = array->child;
            array->child = newitem;
        }
        else if (before_inserted != NULL)
        {
            newitem->next = before_inserted->next;
            before_inserted->next = newitem;
        }
    }
    return true;
}

CJSON_PUBLIC(cJSON_bool) cJSON_ReplaceItemViaPointer(cJSON *_Exclusive const parent, cJSON *_Unchecked const item, cJSON * replacement)
{
    if ((parent == NULL) || (parent->child == NULL) || (replacement == NULL))
    {
        return false;
    }

    /* unsafe: as in cJSON_DetachItemViaPointer; replacement moves in on success, and item is freed after it is unlinked */
    __unsafe
    {
        if (item == NULL)
        {
            return false;
        }

        if (replacement == item)
        {
            return true;
        }

        replacement->next = item->next;
        replacement->prev = item->prev;

        if (replacement->next != NULL)
        {
            replacement->next->prev = replacement;
        }
        if (parent->child == item)
        {
            if (parent->child->prev == parent->child)
            {
                replacement->prev = replacement;
            }
            parent->child = replacement;
        }
        else
        {   /*
             * To find the last item in array quickly, we use prev in array.
             * We can't modify the last item's next pointer where this item was the parent's child
             */
            if (replacement->prev != NULL)
            {
                replacement->prev->next = replacement;
            }
            if (replacement->next == NULL)
            {
                parent->child->prev = replacement;
            }
        }

        item->next = NULL;
        item->prev = NULL;
        cJSON_Delete(item);
    }

    return true;
}

CJSON_PUBLIC(cJSON_bool) cJSON_ReplaceItemInArray(cJSON *_Exclusive array, int which, cJSON *newitem)
{
    if (which < 0)
    {
        return false;
    }

    return cJSON_ReplaceItemViaPointer(array, get_array_item(array, (size_t)which), newitem);
}

static cJSON_bool replace_item_in_object(cJSON *_Nullable _Exclusive object, const char *_Nullable string, cJSON *_Nullable _Exclusive replacement, cJSON_bool case_sensitive)
{
    if ((replacement == NULL) || (string == NULL))
    {
        return false;
    }

    /* replace the name in the replacement */
    if (!(replacement->type & cJSON_StringIsConst))
    {
        global_hooks.deallocate(_Take(replacement->string));
    }
    else
    {
        /* a constant key is not ours to free */
        (void)_Take(replacement->string);
    }
    replacement->string = cJSON_strdup(string, &global_hooks);
    if (replacement->string == NULL)
    {
        return false;
    }

    replacement->type &= ~cJSON_StringIsConst;

    return cJSON_ReplaceItemViaPointer(object, get_object_item(object, string, case_sensitive), replacement);
}

CJSON_PUBLIC(cJSON_bool) cJSON_ReplaceItemInObject(cJSON *_Exclusive object, const char *string, cJSON *_Exclusive newitem)
{
    return replace_item_in_object(object, string, newitem, false);
}

CJSON_PUBLIC(cJSON_bool) cJSON_ReplaceItemInObjectCaseSensitive(cJSON *_Exclusive object, const char *string, cJSON *_Exclusive newitem)
{
    return replace_item_in_object(object, string, newitem, true);
}

/* Create basic types: */
CJSON_PUBLIC(cJSON *_Nullable) cJSON_CreateNull(void)
{
    cJSON *_Nullable _Owned item = cJSON_New_Item(&global_hooks);
    if(item)
    {
        item->type = cJSON_NULL;
    }

    return item;
}

CJSON_PUBLIC(cJSON *_Nullable) cJSON_CreateTrue(void)
{
    cJSON *_Nullable _Owned item = cJSON_New_Item(&global_hooks);
    if(item)
    {
        item->type = cJSON_True;
    }

    return item;
}

CJSON_PUBLIC(cJSON *_Nullable) cJSON_CreateFalse(void)
{
    cJSON *_Nullable _Owned item = cJSON_New_Item(&global_hooks);
    if(item)
    {
        item->type = cJSON_False;
    }

    return item;
}

CJSON_PUBLIC(cJSON *_Nullable) cJSON_CreateBool(cJSON_bool boolean)
{
    cJSON *_Nullable _Owned item = cJSON_New_Item(&global_hooks);
    if(item)
    {
        item->type = boolean ? cJSON_True : cJSON_False;
    }

    return item;
}

CJSON_PUBLIC(cJSON *_Nullable) cJSON_CreateNumber(double num)
{
    cJSON *_Nullable _Owned item = cJSON_New_Item(&global_hooks);
    if(item)
    {
        item->type = cJSON_Number;
        item->valuedouble = num;

        /* use saturation in case of overflow */
        if (num >= INT_MAX)
        {
            item->valueint = INT_MAX;
        }
        else if (num <= (double)INT_MIN)
        {
            item->valueint = INT_MIN;
        }
        else
        {
            item->valueint = (int)num;
        }
    }

    return item;
}

CJSON_PUBLIC(cJSON *_Nullable) cJSON_CreateString(const char *string _Noescape)
{
    cJSON *_Nullable _Owned item = cJSON_New_Item(&global_hooks);
    if(item)
    {
        item->type = cJSON_String;
        item->valuestring = cJSON_strdup(string, &global_hooks);
        if(!item->valuestring)
        {
            cJSON_Delete(item);
            return NULL;
        }
    }

    return item;
}

CJSON_PUBLIC(cJSON *_Nullable) cJSON_CreateStringReference(const char *string)
{
    cJSON *_Nullable _Owned item = cJSON_New_Item(&global_hooks);
    if (item != NULL)
    {
        item->type = cJSON_String | cJSON_IsReference;
        if (string != NULL)
        {
            item->valuestring = adopt_string(string);
        }
    }

    return item;
}

CJSON_PUBLIC(cJSON *_Nullable) cJSON_CreateObjectReference(const cJSON *child)
{
    cJSON *_Nullable _Owned item = cJSON_New_Item(&global_hooks);
    if (item != NULL) {
        item->type = cJSON_Object | cJSON_IsReference;
        if (child != NULL)
        {
            item->child = adopt_item(child);
        }
    }

    return item;
}

CJSON_PUBLIC(cJSON *_Nullable) cJSON_CreateArrayReference(const cJSON *child) {
    cJSON *_Nullable _Owned item = cJSON_New_Item(&global_hooks);
    if (item != NULL) {
        item->type = cJSON_Array | cJSON_IsReference;
        if (child != NULL)
        {
            item->child = adopt_item(child);
        }
    }

    return item;
}

CJSON_PUBLIC(cJSON *_Nullable) cJSON_CreateRaw(const char *raw)
{
    cJSON *_Nullable _Owned item = cJSON_New_Item(&global_hooks);
    if(item)
    {
        item->type = cJSON_Raw;
        item->valuestring = cJSON_strdup(raw, &global_hooks);
        if(!item->valuestring)
        {
            cJSON_Delete(item);
            return NULL;
        }
    }

    return item;
}

CJSON_PUBLIC(cJSON *_Nullable) cJSON_CreateArray(void)
{
    cJSON *_Nullable _Owned item = cJSON_New_Item(&global_hooks);
    if(item)
    {
        item->type=cJSON_Array;
    }

    return item;
}

CJSON_PUBLIC(cJSON *_Nullable) cJSON_CreateObject(void)
{
    cJSON *_Nullable _Owned item = cJSON_New_Item(&global_hooks);
    if (item)
    {
        item->type = cJSON_Object;
    }

    return item;
}

/* Create Arrays: */
CJSON_PUBLIC(cJSON *_Nullable) cJSON_CreateIntArray(const int *_Nullable __counted_by_or_null(count) numbers, int count)
{
    size_t i = 0;
    cJSON *_Nullable _Owned n = NULL;
    cJSON *_Nullable _Owned a = NULL;

    if ((count < 0) || (numbers == NULL))
    {
        return NULL;
    }

    a = cJSON_CreateArray();

    for(i = 0; a && (i < (size_t)count); i++)
    {
        n = cJSON_CreateNumber(numbers[i]);
        if (!n)
        {
            cJSON_Delete(a);
            return NULL;
        }
        (void)append_child(a, n);
    }


    return a;
}

CJSON_PUBLIC(cJSON *_Nullable) cJSON_CreateFloatArray(const float *_Nullable __counted_by_or_null(count) numbers, int count)
{
    size_t i = 0;
    cJSON *_Nullable _Owned n = NULL;
    cJSON *_Nullable _Owned a = NULL;

    if ((count < 0) || (numbers == NULL))
    {
        return NULL;
    }

    a = cJSON_CreateArray();

    for(i = 0; a && (i < (size_t)count); i++)
    {
        n = cJSON_CreateNumber((double)numbers[i]);
        if(!n)
        {
            cJSON_Delete(a);
            return NULL;
        }
        (void)append_child(a, n);
    }


    return a;
}

CJSON_PUBLIC(cJSON *_Nullable) cJSON_CreateDoubleArray(const double *_Nullable __counted_by_or_null(count) numbers, int count)
{
    size_t i = 0;
    cJSON *_Nullable _Owned n = NULL;
    cJSON *_Nullable _Owned a = NULL;

    if ((count < 0) || (numbers == NULL))
    {
        return NULL;
    }

    a = cJSON_CreateArray();

    for(i = 0; a && (i < (size_t)count); i++)
    {
        n = cJSON_CreateNumber(numbers[i]);
        if(!n)
        {
            cJSON_Delete(a);
            return NULL;
        }
        (void)append_child(a, n);
    }


    return a;
}

CJSON_PUBLIC(cJSON *_Nullable) cJSON_CreateStringArray(const char *_Nullable const *_Nullable __counted_by_or_null(count) strings, int count)
{
    size_t i = 0;
    cJSON *_Nullable _Owned n = NULL;
    cJSON *_Nullable _Owned a = NULL;

    if ((count < 0) || (strings == NULL))
    {
        return NULL;
    }

    a = cJSON_CreateArray();

    for (i = 0; a && (i < (size_t)count); i++)
    {
        n = cJSON_CreateString(strings[i]);
        if(!n)
        {
            cJSON_Delete(a);
            return NULL;
        }
        (void)append_child(a, n);
    }


    return a;
}

/* Duplication */
_Returns_owned cJSON *_Nullable cJSON_Duplicate_rec(const cJSON *_Nullable item, size_t depth, cJSON_bool recurse);

CJSON_PUBLIC(cJSON *_Nullable) cJSON_Duplicate(const cJSON *item, cJSON_bool recurse)
{
    return cJSON_Duplicate_rec(item, 0, recurse );
}

_Returns_owned cJSON *_Nullable cJSON_Duplicate_rec(const cJSON *_Nullable item, size_t depth, cJSON_bool recurse)
{
    cJSON *_Nullable _Owned newitem = NULL;
    cJSON *_Nullable child = NULL;

    /* Bail on bad ptr */
    if (!item)
    {
        goto fail;
    }
    /* Create new item */
    newitem = cJSON_New_Item(&global_hooks);
    if (!newitem)
    {
        goto fail;
    }
    /* Copy over all vars */
    newitem->type = item->type & (~cJSON_IsReference);
    newitem->valueint = item->valueint;
    newitem->valuedouble = item->valuedouble;
    if (item->valuestring)
    {
        newitem->valuestring = cJSON_strdup(item->valuestring, &global_hooks);
        if (!newitem->valuestring)
        {
            goto fail;
        }
    }
    if (item->string)
    {
        /* GAP: 'c ? owned : owned' does not count as owned, so this is an if */
        if (item->type & cJSON_StringIsConst)
        {
            newitem->string = adopt_string(item->string);
        }
        else
        {
            newitem->string = cJSON_strdup(item->string, &global_hooks);
        }
        if (!newitem->string)
        {
            goto fail;
        }
    }
    /* If non-recursive, then we're done! */
    if (!recurse)
    {
        return newitem;
    }
    /* Walk the ->next chain for the child. */
    child = item->child;
    while (child != NULL)
    {
        if(depth >= CJSON_CIRCULAR_LIMIT) {
            goto fail;
        }
        cJSON *_Nullable _Owned newchild = cJSON_Duplicate_rec(child, depth + 1, true); /* Duplicate (with recurse) each item in the ->next chain */
        if (!newchild)
        {
            goto fail;
        }
        (void)append_child(newitem, newchild);
        child = child->next;
    }

    return newitem;

fail:
    cJSON_Delete(newitem);

    return NULL;
}

static void skip_oneline_comment(char *__bidi_indexable *input)
{
    *input += static_strlen("//");

    for (; (*input)[0] != '\0'; ++(*input))
    {
        if ((*input)[0] == '\n') {
            *input += static_strlen("\n");
            return;
        }
    }
}

static void skip_multiline_comment(char *__bidi_indexable *input)
{
    *input += static_strlen("/*");

    for (; (*input)[0] != '\0'; ++(*input))
    {
        if (((*input)[0] == '*') && ((*input)[1] == '/'))
        {
            *input += static_strlen("*/");
            return;
        }
    }
}

static void minify_string(char *__bidi_indexable *input, char *__bidi_indexable *output) {
    (*output)[0] = (*input)[0];
    *input += static_strlen("\"");
    *output += static_strlen("\"");


    for (; (*input)[0] != '\0'; (void)++(*input), ++(*output)) {
        (*output)[0] = (*input)[0];

        if ((*input)[0] == '\"') {
            (*output)[0] = '\"';
            *input += static_strlen("\"");
            *output += static_strlen("\"");
            return;
        } else if (((*input)[0] == '\\') && ((*input)[1] == '\"')) {
            (*output)[1] = (*input)[1];
            *input += static_strlen("\"");
            *output += static_strlen("\"");
        }
    }
}

CJSON_PUBLIC(void) cJSON_Minify(char *_Nullable __null_terminated string _Noescape)
{
    char *json = NULL;
    char *into = NULL;

    if (string == NULL)
    {
        return;
    }

    /* minifying reads and rewrites the terminator, so it must be in bounds;
       the output never runs ahead of the input, so the final terminator lands
       at or before the original one */
    __unsafe { json = __unsafe_null_terminated_to_indexable(string); }
    into = json;

    while (json[0] != '\0')
    {
        switch (json[0])
        {
            case ' ':
            case '\t':
            case '\r':
            case '\n':
                json++;
                break;

            case '/':
                if (json[1] == '/')
                {
                    skip_oneline_comment(&json);
                }
                else if (json[1] == '*')
                {
                    skip_multiline_comment(&json);
                } else {
                    json++;
                }
                break;

            case '\"':
                minify_string(&json, &into);
                break;

            default:
                into[0] = json[0];
                json++;
                into++;
        }
    }

    /* and null-terminate. */
    *into = '\0';
}

CJSON_PUBLIC(cJSON_bool) cJSON_IsInvalid(const cJSON * const item)
{
    if (item == NULL)
    {
        return false;
    }

    return (item->type & 0xFF) == cJSON_Invalid;
}

CJSON_PUBLIC(cJSON_bool) cJSON_IsFalse(const cJSON * const item)
{
    if (item == NULL)
    {
        return false;
    }

    return (item->type & 0xFF) == cJSON_False;
}

CJSON_PUBLIC(cJSON_bool) cJSON_IsTrue(const cJSON * const item)
{
    if (item == NULL)
    {
        return false;
    }

    return (item->type & 0xff) == cJSON_True;
}


CJSON_PUBLIC(cJSON_bool) cJSON_IsBool(const cJSON * const item)
{
    if (item == NULL)
    {
        return false;
    }

    return (item->type & (cJSON_True | cJSON_False)) != 0;
}
CJSON_PUBLIC(cJSON_bool) cJSON_IsNull(const cJSON * const item)
{
    if (item == NULL)
    {
        return false;
    }

    return (item->type & 0xFF) == cJSON_NULL;
}

CJSON_PUBLIC(cJSON_bool) cJSON_IsNumber(const cJSON * const item)
{
    if (item == NULL)
    {
        return false;
    }

    return (item->type & 0xFF) == cJSON_Number;
}

CJSON_PUBLIC(cJSON_bool) cJSON_IsString(const cJSON * const item)
{
    if (item == NULL)
    {
        return false;
    }

    return (item->type & 0xFF) == cJSON_String;
}

CJSON_PUBLIC(cJSON_bool) cJSON_IsArray(const cJSON * const item)
{
    if (item == NULL)
    {
        return false;
    }

    return (item->type & 0xFF) == cJSON_Array;
}

CJSON_PUBLIC(cJSON_bool) cJSON_IsObject(const cJSON * const item)
{
    if (item == NULL)
    {
        return false;
    }

    return (item->type & 0xFF) == cJSON_Object;
}

CJSON_PUBLIC(cJSON_bool) cJSON_IsRaw(const cJSON * const item)
{
    if (item == NULL)
    {
        return false;
    }

    return (item->type & 0xFF) == cJSON_Raw;
}

CJSON_PUBLIC(cJSON_bool) cJSON_Compare(const cJSON * const a, const cJSON * const b, const cJSON_bool case_sensitive)
{
    if ((a == NULL) || (b == NULL) || ((a->type & 0xFF) != (b->type & 0xFF)))
    {
        return false;
    }

    /* check if type is valid */
    switch (a->type & 0xFF)
    {
        case cJSON_False:
        case cJSON_True:
        case cJSON_NULL:
        case cJSON_Number:
        case cJSON_String:
        case cJSON_Raw:
        case cJSON_Array:
        case cJSON_Object:
            break;

        default:
            return false;
    }

    /* identical objects are equal */
    if (a == b)
    {
        return true;
    }

    switch (a->type & 0xFF)
    {
        /* in these cases and equal type is enough */
        case cJSON_False:
        case cJSON_True:
        case cJSON_NULL:
            return true;

        case cJSON_Number:
            if (compare_double(a->valuedouble, b->valuedouble))
            {
                return true;
            }
            return false;

        case cJSON_String:
        case cJSON_Raw:
            if ((a->valuestring == NULL) || (b->valuestring == NULL))
            {
                return false;
            }
            if (strcmp(a->valuestring, b->valuestring) == 0)
            {
                return true;
            }

            return false;

        case cJSON_Array:
        {
            cJSON *a_element = a->child;
            cJSON *b_element = b->child;

            for (; (a_element != NULL) && (b_element != NULL);)
            {
                if (!cJSON_Compare(a_element, b_element, case_sensitive))
                {
                    return false;
                }

                a_element = a_element->next;
                b_element = b_element->next;
            }

            /* one of the arrays is longer than the other */
            if (a_element != b_element) {
                return false;
            }

            return true;
        }

        case cJSON_Object:
        {
            cJSON *a_element = NULL;
            cJSON *b_element = NULL;
            cJSON_ArrayForEach(a_element, a)
            {
                /* TODO This has O(n^2) runtime, which is horrible! */
                b_element = get_object_item(b, a_element->string, case_sensitive);
                if (b_element == NULL)
                {
                    return false;
                }

                if (!cJSON_Compare(a_element, b_element, case_sensitive))
                {
                    return false;
                }
            }

            /* doing this twice, once on a and b to prevent true comparison if a subset of b
             * TODO: Do this the proper way, this is just a fix for now */
            cJSON_ArrayForEach(b_element, b)
            {
                a_element = get_object_item(a, b_element->string, case_sensitive);
                if (a_element == NULL)
                {
                    return false;
                }

                if (!cJSON_Compare(b_element, a_element, case_sensitive))
                {
                    return false;
                }
            }

            return true;
        }

        default:
            return false;
    }
}

CJSON_PUBLIC(void *_Nullable __sized_by_or_null(size)) cJSON_malloc(size_t size)
{
    return global_hooks.allocate(size);
}

CJSON_PUBLIC(void) cJSON_free(void *_Nullable object)
{
    global_hooks.deallocate(object);
    object = NULL;
}
