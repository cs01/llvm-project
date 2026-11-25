/* Whole files. */
#ifndef SAFEC_FILE_H
#define SAFEC_FILE_H
#include "text.h"
#include <errno.h>
#if defined(__clang__)
#pragma clang assume_nonnull begin
#endif

/* What went wrong is errno's value. */
typedef RESULT(file_result, size_t, int) file_result;

/* Adds the file to *out; the value is how many bytes. */
static inline file_result file_read(const char *__null_terminated path, struct text *out) {
  FILE *_Owned _Nullable f = fopen(path, "rb");
  if (!f)
    return ERR(file_result, errno);
  char part[4096];
  int ok = 1, why = 0;
  size_t n = 0, before = out->len;
  do {
    n = fread(part, 1, sizeof part, f);
    if (n > sizeof part || !text_add(out, part, n))
      ok = 0, why = ENOMEM;
  } while (ok && n == sizeof part);
  if (ferror(f))
    ok = 0, why = errno ? errno : EIO;
  fclose(f);
  if (!ok)
    return ERR(file_result, why);
  return OK(file_result, out->len - before);
}

/* Replaces the file: 1, or 0 and the old one is as it was. It is written to
   'path.tmp' and renamed, so a reader sees all of the old or all of the new.
   The value is how many bytes. */
static inline file_result file_write(const char *__null_terminated path, struct str bytes) {
  char tmp[1024];
  int n = snprintf(tmp, sizeof tmp, "%s.tmp", path);
  if (n < 0 || (size_t)n >= sizeof tmp)
    return ERR(file_result, ENAMETOOLONG);
  FILE *_Owned _Nullable f = fopen(tmp, "wb");
  if (!f)
    return ERR(file_result, errno);
  int ok = fwrite(bytes.p, 1, bytes.len, f) == bytes.len;
  if (fclose(f) != 0)
    ok = 0;
  int why = errno ? errno : EIO;
  if (ok && rename(tmp, path) == 0)
    return OK(file_result, bytes.len);
  why = errno ? errno : why;
  remove(tmp);
  return ERR(file_result, why);
}

#if defined(__clang__)
#pragma clang assume_nonnull end
#endif
#endif
