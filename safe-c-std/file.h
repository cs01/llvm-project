/* Whole files. */
#ifndef SAFEC_FILE_H
#define SAFEC_FILE_H
#include "text.h"
#if defined(__clang__)
#pragma clang assume_nonnull begin
#endif

/* Adds the file to *out: 1, or 0 if it cannot be read. */
static inline int file_read(const char *__null_terminated path, struct text *out) {
  FILE *_Owned _Nullable f = fopen(path, "rb");
  if (!f)
    return 0;
  char part[4096];
  int ok = 1;
  size_t n = 0;
  do {
    n = fread(part, 1, sizeof part, f);
    if (n > sizeof part || !text_add(out, part, n))
      ok = 0;
  } while (ok && n == sizeof part);
  if (ferror(f))
    ok = 0;
  fclose(f);
  return ok;
}

/* Replaces the file: 1, or 0 and the old one is as it was. It is written to
   'path.tmp' and renamed, so a reader sees all of the old or all of the new. */
static inline int file_write(const char *__null_terminated path, struct str bytes) {
  char tmp[1024];
  int n = snprintf(tmp, sizeof tmp, "%s.tmp", path);
  if (n < 0 || (size_t)n >= sizeof tmp)
    return 0;
  FILE *_Owned _Nullable f = fopen(tmp, "wb");
  if (!f)
    return 0;
  int ok = fwrite(bytes.p, 1, bytes.len, f) == bytes.len;
  if (fclose(f) != 0)
    ok = 0;
  if (ok && rename(tmp, path) == 0)
    return 1;
  remove(tmp);
  return 0;
}

#if defined(__clang__)
#pragma clang assume_nonnull end
#endif
#endif
