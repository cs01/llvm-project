/* inih -- simple .INI file parser

SPDX-License-Identifier: BSD-3-Clause

Copyright (C) 2009-2025, Ben Hoyt

inih is released under the New BSD license (see LICENSE.txt). Go to the project
home page for more info:

https://github.com/benhoyt/inih

*/

#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdio.h>
#include <ctype.h>
#include <string.h>

#include "ini.h"

#if !INI_USE_STACK
#if INI_CUSTOM_ALLOCATOR
#include <stddef.h>
/* The same contracts as malloc, free and realloc. alloc_size lets the result
   be cast to char* (Safe C checks the definition returns fresh memory). */
 __attribute__((alloc_size(1)))
void* _Owned _Nullable __sized_by_or_null(size) ini_malloc(size_t size);
void ini_free(void* _Nullable _Owned ptr);
 __attribute__((alloc_size(2)))
void* _Owned _Nullable __sized_by_or_null(size) ini_realloc(void* _Nullable _Owned ptr,
                                                            size_t size);
#else
#include <stdlib.h>
#define ini_malloc malloc
#define ini_free free
#define ini_realloc realloc
#endif
#endif

/* Where ini_parse_stream() reads its lines from. Upstream passes a void*
   stream to every reader, including its own fgets and string readers; Safe C
   rejects casting that void* back to FILE* or to a struct holding a pointer,
   so the built-in sources are told apart by kind instead, and only a user's
   reader sees a void*. */
enum { INI_SOURCE_READER, INI_SOURCE_FILE, INI_SOURCE_STRING };
typedef struct _View {
    int kind;
    ini_reader _Nullable reader;          /* INI_SOURCE_READER */
    void* _Nullable stream;
    FILE* _Nullable file;                 /* INI_SOURCE_FILE */
    const char* _Nullable __counted_by_or_null(length) string; /* INI_SOURCE_STRING */
    size_t length;
    size_t offset;                        /* bytes of string already read */
} ini_source;

/* Strip whitespace chars off end of given string, in place. end must be a
   pointer to the NUL terminator at the end of the string. Return s. */
static char* __bidi_indexable ini_rstrip(char* __bidi_indexable s,
                                         char* __bidi_indexable end)
{
    while (end > s && isspace((unsigned char)(*--end)))
        *end = '\0';
    return s;
}

/* Return pointer to first non-whitespace char in given string. */
static char* __bidi_indexable ini_lskip(char* __bidi_indexable s)
{
    while (*s && isspace((unsigned char)(*s)))
        s++;
    return s;
}

/* Return pointer to first char (of chars) or inline comment in given string,
   or pointer to NUL at end of string if neither found. Inline comment must
   be prefixed by a whitespace character to register as a comment. */
static char* __bidi_indexable ini_find_chars_or_comment(char* __bidi_indexable s,
                                                        const char* _Nullable chars)
{
#if INI_ALLOW_INLINE_COMMENTS
    int was_space = 0;
    while (*s && (!chars || !strchr(chars, *s)) &&
           !(was_space && strchr(INI_INLINE_COMMENT_PREFIXES, *s))) {
        was_space = isspace((unsigned char)(*s));
        s++;
    }
#else
    while (*s && (!chars || !strchr(chars, *s))) {
        s++;
    }
#endif
    return s;
}

/* Similar to strncpy, but ensures dest (size bytes) is
   NUL-terminated, and doesn't pad with NULs. */
static char* __bidi_indexable ini_strncpy0(char* __counted_by(size) dest,
                                           const char* __bidi_indexable src,
                                           size_t size)
{
    /* Could use strncpy internally, but it causes gcc warnings (see issue #91) */
    size_t i;
    for (i = 0; i < size - 1 && src[i]; i++)
        dest[i] = src[i];
    dest[i] = '\0';
    return dest;
}

/* Read the next line from src into str (num bytes), fgets-style: returns
   nonzero with a NUL-terminated line, or 0 at end of input. The string
   source is the fgets() equivalent upstream calls ini_reader_string(). */
static int ini_read_line(ini_source* src, char* __counted_by(num) str _Noescape,
                         int num)
{
    size_t i = 0;
    char c;

    switch (src->kind) {
    case INI_SOURCE_READER:
        return src->reader && src->stream &&
               src->reader(str, num, src->stream) != NULL;
    case INI_SOURCE_FILE:
        return src->file && fgets(str, num, src->file) != NULL;
    case INI_SOURCE_STRING:
        if (!src->string || src->offset == src->length || num < 2)
            return 0;
        while ((int)i < num - 1 && src->offset != src->length) {
            c = src->string[src->offset++];
            str[i++] = c;
            if (c == '\n')
                break;
        }
        str[i] = '\0';
        return 1;
    }
    return 0;
}

/* The parser behind every ini_parse*() entry point. */
static int ini_parse_source(ini_source* src, ini_handler handler,
                            void* _Nullable user)
{
    /* Uses a fair bit of stack (use heap instead if you need to) */
#if INI_USE_STACK
    char line[INI_MAX_LINE];
    size_t max_line = INI_MAX_LINE;
#else
    char* _Owned _Nullable line;
    size_t max_line = INI_INITIAL_ALLOC;
#endif
#if INI_ALLOW_REALLOC && !INI_USE_STACK
    char* _Owned _Nullable new_line;
#endif
    char section[INI_MAX_SECTION] = "";
#if INI_ALLOW_MULTILINE
    char prev_name[INI_MAX_NAME] = "";
#endif

    size_t offset;
    char* start;
    char* end;
    char* name;
    char* value;
    int lineno = 0;
    int error = 0;
    char abyss[16];  /* Used to consume input when a line is too long. */
    size_t abyss_len;

#if !INI_USE_STACK
    line = (char*)ini_malloc(INI_INITIAL_ALLOC);
    if (!line) {
        return -2;
    }
#endif

#if INI_HANDLER_LINENO
#define HANDLER(u, s, n, v) handler(u, s, n, v, lineno)
#else
#define HANDLER(u, s, n, v) handler(u, s, n, v)
#endif

    /* Scan through stream line by line */
    while (ini_read_line(src, line, (int)max_line)) {
        offset = strlen(line);

#if INI_ALLOW_REALLOC && !INI_USE_STACK
        while (max_line < INI_MAX_LINE &&
               offset == max_line - 1 && line[offset - 1] != '\n') {
            max_line *= 2;
            if (max_line > INI_MAX_LINE)
                max_line = INI_MAX_LINE;
            /* Safe C: realloc takes line even when it fails, so out of memory
               leaks it (upstream frees it). */
            new_line = (char*)ini_realloc(line, max_line);
            if (!new_line)
                return -2;
            line = new_line;
            if (!ini_read_line(src, line + offset, (int)(max_line - offset)))
                break;
            offset += strlen(line + offset);
        }
#endif

        lineno++;

        /* If line exceeded INI_MAX_LINE bytes, discard till end of line. */
        if (offset == max_line - 1 && line[offset - 1] != '\n') {
            while (ini_read_line(src, abyss, sizeof(abyss))) {
                if (!error)
                    error = lineno;
                abyss_len = strlen(abyss);
                if (abyss_len > 0 && abyss[abyss_len - 1] == '\n')
                    break;
            }
        }

        start = line;
#if INI_ALLOW_BOM
        if (lineno == 1 && (unsigned char)start[0] == 0xEF &&
                           (unsigned char)start[1] == 0xBB &&
                           (unsigned char)start[2] == 0xBF) {
            start += 3;
        }
#endif
        start = ini_rstrip(ini_lskip(start), line + offset);

        if (strchr(INI_START_COMMENT_PREFIXES, *start)) {
            /* Start-of-line comment */
        }
#if INI_ALLOW_MULTILINE
        else if (*prev_name && *start && start > line) {
#if INI_ALLOW_INLINE_COMMENTS
            end = ini_find_chars_or_comment(start, NULL);
            *end = '\0';
            ini_rstrip(start, end);
#endif
            /* Non-blank line with leading whitespace, treat as continuation
               of previous name's value (as per Python configparser). */
            if (!HANDLER(user, section, prev_name, start) && !error)
                error = lineno;
        }
#endif
        else if (*start == '[') {
            /* A "[section]" line */
            end = ini_find_chars_or_comment(start + 1, "]");
            if (*end == ']') {
                *end = '\0';
                ini_strncpy0(section, start + 1, sizeof(section));
#if INI_ALLOW_MULTILINE
                *prev_name = '\0';
#endif
#if INI_CALL_HANDLER_ON_NEW_SECTION
                if (!HANDLER(user, section, NULL, NULL) && !error)
                    error = lineno;
#endif
            }
            else if (!error) {
                /* No ']' found on section line */
                error = lineno;
            }
        }
        else if (*start) {
            /* Not a comment, must be a name[=:]value pair */
            end = ini_find_chars_or_comment(start, "=:");
            if (*end == '=' || *end == ':') {
                *end = '\0';
                name = ini_rstrip(start, end);
                value = end + 1;
#if INI_ALLOW_INLINE_COMMENTS
                end = ini_find_chars_or_comment(value, NULL);
                *end = '\0';
#endif
                value = ini_lskip(value);
                ini_rstrip(value, end);

#if INI_ALLOW_MULTILINE
                name = ini_strncpy0(prev_name, name, sizeof(prev_name));
#endif
                /* Valid name[=:]value pair found, call handler */
                if (!HANDLER(user, section, name, value) && !error)
                    error = lineno;
            }
            else {
                /* No '=' or ':' found on name[=:]value line */
#if INI_ALLOW_NO_VALUE
                *end = '\0';
                name = ini_rstrip(start, end);
#if INI_ALLOW_MULTILINE
                name = ini_strncpy0(prev_name, name, sizeof(prev_name));
#endif
                if (!HANDLER(user, section, name, NULL) && !error)
                    error = lineno;
#else
                if (!error)
                    error = lineno;
#endif
            }
        }

#if INI_STOP_ON_FIRST_ERROR
        if (error)
            break;
#endif
    }

#if !INI_USE_STACK
    ini_free(line);
#endif

    return error;
}

/* See documentation in header file. The asserts that reader and stream are
   not NULL are gone: the parameters are non-null, which Safe C checks at
   every call. */
int ini_parse_stream(ini_reader reader, void* stream, ini_handler handler,
                     void* _Nullable user)
{
    ini_source src = {INI_SOURCE_READER, reader, stream, NULL, NULL, 0, 0};
    return ini_parse_source(&src, handler, user);
}

/* See documentation in header file. */
int ini_parse_file(FILE* file, ini_handler handler, void* _Nullable user)
{
    ini_source src = {INI_SOURCE_FILE, NULL, NULL, file, NULL, 0, 0};
    int error = ini_parse_source(&src, handler, user);
    return ferror(file) ? -3 : error;
}

/* See documentation in header file. */
int ini_parse(const char* filename, ini_handler handler, void* _Nullable user)
{
    FILE* _Owned _Nullable file;
    int error;

    file = fopen(filename, "r");
    if (!file)
        return -1;
    error = ini_parse_file(file, handler, user);
    fclose(file);
    return error;
}

/* See documentation in header file. */
int ini_parse_string(const char* string, ini_handler handler, void* _Nullable user) {
    size_t length = strlen(string);
    return ini_parse_string_length(__null_terminated_to_indexable(string),
                                   length, handler, user);
}

/* See documentation in header file. */
int ini_parse_string_length(const char* __counted_by(length) string, size_t length,
                            ini_handler handler, void* _Nullable user) {
    ini_source src = {INI_SOURCE_STRING, NULL, NULL, NULL, string, length, 0};
    return ini_parse_source(&src, handler, user);
}
