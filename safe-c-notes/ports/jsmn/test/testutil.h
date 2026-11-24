#ifndef __TEST_UTIL_H__
#define __TEST_UTIL_H__

#include "../safec.h" /* vendored; -fsafe-c also force-includes it */
#include "../jsmn.h"

static int vtokeq(const char *_Nullable s _Noescape, jsmntok_t *__counted_by(numtok) t,
                  unsigned long numtok,
                  va_list ap) {
  if (numtok > 0) {
    unsigned long i;
    int start, end, size;
    jsmntype_t type;
    char *_Nullable __null_terminated value;

    size = -1;
    value = NULL;
    for (i = 0; i < numtok; i++) {
      __unsafe { type = va_arg(ap, jsmntype_t); } /* va_arg: unchecked count and type */
      if (type == JSMN_STRING) {
        /* va_arg cannot know what the caller passed. */
        __unsafe { value = __unsafe_forge_null_terminated(char *, va_arg(ap, char *)); }
        __unsafe { size = va_arg(ap, int); } /* va_arg: unchecked count and type */
        start = end = -1;
      } else if (type == JSMN_PRIMITIVE) {
        __unsafe { value = __unsafe_forge_null_terminated(char *, va_arg(ap, char *)); }
        start = end = size = -1;
      } else {
        __unsafe { start = va_arg(ap, int); } /* va_arg: unchecked count and type */
        __unsafe { end = va_arg(ap, int); } /* va_arg: unchecked count and type */
        __unsafe { size = va_arg(ap, int); } /* va_arg: unchecked count and type */
        value = NULL;
      }
      if (t[i].type != type) {
        printf("token %lu type is %d, not %d\n", i, t[i].type, type);
        return 0;
      }
      if (start != -1 && end != -1) {
        if (t[i].start != start) {
          printf("token %lu start is %d, not %d\n", i, t[i].start, start);
          return 0;
        }
        if (t[i].end != end) {
          printf("token %lu end is %d, not %d\n", i, t[i].end, end);
          return 0;
        }
      }
      if (size != -1 && t[i].size != size) {
        printf("token %lu size is %d, not %d\n", i, t[i].size, size);
        return 0;
      }

      if (s != NULL && value != NULL) {
        const char *sb = __null_terminated_to_indexable(s);
        const char *p = sb + t[i].start;
        if (strlen(value) != (unsigned long)(t[i].end - t[i].start) ||
            /* strncmp would want a terminator inside p's bounds; the slice
               has none, and the length is known, so compare bytes. */
            memcmp(p, __null_terminated_to_indexable(value),
                   t[i].end - t[i].start) != 0) {
          printf("token %lu value is ", i);
          fwrite(sb + t[i].start, 1, (size_t)(t[i].end - t[i].start), stdout);
          printf(", not %s\n", value);
          return 0;
        }
      }
    }
  }
  return 1;
}

static int tokeq(const char *_Nullable s _Noescape, jsmntok_t *__counted_by(numtok) tokens,
                 unsigned long numtok, ...) {
  int ok;
  va_list args;
  va_start(args, numtok);
  ok = vtokeq(s, tokens, numtok, args);
  va_end(args);
  return ok;
}

static int parse(const char *s _Noescape, int status, unsigned long numtok, ...) {
  int r;
  int ok = 1;
  va_list args;
  jsmn_parser p;
  jsmntok_t *t = malloc(numtok * sizeof(jsmntok_t));
  if (t == NULL)
    return 0;

  jsmn_init(&p);
  r = jsmn_parse(&p, __null_terminated_to_indexable(s), strlen(s), t, numtok);
  if (r != status) {
    printf("status is %d, not %d\n", r, status);
    return 0;
  }

  if (status >= 0) {
    va_start(args, numtok);
    ok = vtokeq(s, t, numtok, args);
    va_end(args);
  }
  free(t);
  return ok;
}

#endif /* __TEST_UTIL_H__ */
