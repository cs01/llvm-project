/* puff.h
  Copyright (C) 2002-2013 Mark Adler, all rights reserved
  version 2.3, 21 Jan 2013

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the author be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.

  Mark Adler    madler@alumni.caltech.edu
 */


/*
 * See puff.c for purpose and usage.
 */
#include "safec.h"              /* Safe C marks; nothing for other compilers */

#ifndef NIL
#  define NIL ((unsigned char *)0)      /* for no output option */
#endif

int puff(unsigned char *_Nullable __unsafe_indexable dest, /* pointer to destination pointer */
         unsigned long *destlen,        /* amount of output space */
         const unsigned char *__unsafe_indexable source, /* pointer to source data pointer */
         unsigned long *sourcelen);     /* amount of input available */

/* Safe C: the same, with the counts by value. err is puff()'s return value;
   outlen and inlen are what puff() sets *destlen and *sourcelen to, and are
   the sizes given when err > 0. */
struct puff_result {
    int err;
    unsigned long outlen, inlen;
};
struct puff_result puff_n(unsigned char *_Nullable __counted_by_or_null(destlen) dest,
                          unsigned long destlen,
                          const unsigned char *__counted_by(sourcelen) source,
                          unsigned long sourcelen);
