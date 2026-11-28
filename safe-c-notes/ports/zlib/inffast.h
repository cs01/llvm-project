/* inffast.h -- header to use inffast.c
 * Copyright (C) 1995-2003, 2010 Mark Adler
 * For conditions of distribution and use, see copyright notice in zlib.h
 */

/* WARNING: this file should *not* be used by applications. It is
   part of the implementation of the compression library and is
   subject to change. Applications should only use zlib.h.
 */

/* Safe C: beg is where inflate() began writing in this call, start bytes
   before the end of the output buffer (strm->next_out + strm->avail_out) */
void ZLIB_INTERNAL inflate_fast(z_streamp strm, unsigned start,
                                unsigned char FAR *__counted_by(start) beg);
