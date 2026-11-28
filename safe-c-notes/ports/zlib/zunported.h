/* zunported.h: the zlib functions the Safe C inflate calls that are not
   ported (adler32.c, crc32.c and zutil.c are compiled as plain C).

   Safe C checks at link time that a function a Safe C file calls was itself
   compiled as Safe C, with the same type. These were not, so they are
   declared here first, in a header marked legacy: Safe C trusts them as
   declared. Callers are still held to the declarations: a buffer passed to
   adler32() or crc32() must hold len bytes. zlib.h and zutil.h declare them
   again, with the same types. */
#ifndef ZUNPORTED_H
#define ZUNPORTED_H
#pragma clang safe_c legacy

ZEXTERN uLong ZEXPORT adler32(uLong adler, const Bytef *_Nullable __counted_by(len) buf,
                                uInt len);
ZEXTERN uLong ZEXPORT crc32(uLong crc, const Bytef *_Nullable __counted_by(len) buf,
                                uInt len);

/* zutil.h's default allocator, for when the application gives none */
#if defined(ZLIB_INTERNAL) && !defined(Z_SOLO)
voidpf _Nullable ZLIB_INTERNAL zcalloc(voidpf _Nullable opaque, unsigned items, unsigned size);
void ZLIB_INTERNAL zcfree(voidpf _Nullable opaque, voidpf ptr);
#endif

#endif
