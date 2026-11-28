/* The rest of inflate's API, Safe C port against stock zlib (built as in
   diff.c): gzip headers read into small buffers, preset dictionaries (set
   and read back), inflateCopy() mid-stream, and inflateSync() past damage.
   Every return code, output byte and header field is folded into one hash
   per test; the two builds must agree on each. Plain C. */
#define Z_PREFIX
#define ZLIB_CONST
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "zlib.h"
#undef inflateInit2_
#undef inflate
#undef inflateEnd
#undef inflateSetDictionary
#undef inflateGetDictionary
#undef inflateGetHeader
#undef inflateCopy
#undef inflateSync
int inflateInit2_(z_streamp, int, const char *, int);
int inflate(z_streamp, int);
int inflateEnd(z_streamp);
int inflateSetDictionary(z_streamp, const Bytef *, uInt);
int inflateGetDictionary(z_streamp, Bytef *, uInt *);
int inflateGetHeader(z_streamp, gz_headerp);
int inflateCopy(z_streamp, z_streamp);
int inflateSync(z_streamp);

typedef struct {
  int (*init2)(z_streamp, int, const char *, int);
  int (*inf)(z_streamp, int);
  int (*end)(z_streamp);
  int (*setdict)(z_streamp, const Bytef *, uInt);
  int (*getdict)(z_streamp, Bytef *, uInt *);
  int (*gethdr)(z_streamp, gz_headerp);
  int (*copy)(z_streamp, z_streamp);
  int (*sync)(z_streamp);
} api;
static const api safe = {inflateInit2_, inflate, inflateEnd, inflateSetDictionary,
                         inflateGetDictionary, inflateGetHeader, inflateCopy, inflateSync};
static const api stock = {z_inflateInit2_, z_inflate, z_inflateEnd, z_inflateSetDictionary,
                          z_inflateGetDictionary, z_inflateGetHeader, z_inflateCopy, z_inflateSync};

static unsigned long long h;
static void mix(const void *p, size_t n) {
  const unsigned char *b = p;
  for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 1099511628211ULL;
}
static void mixi(long v) { mix(&v, sizeof v); }

#define N 20000
static unsigned char data[N], comp[2 * N + 1000], out[N + 100];
static const unsigned char dict[] = "the quick brown fox jumps over the lazy dog, and a dictionary";
static size_t clen;

static void make(void) {
  unsigned s = 1;
  for (int i = 0; i < N; i++) {
    s = s * 1103515245 + 12345;
    data[i] = i > 100 && (s >> 16) % 4 ? data[i - 1 - (s >> 8) % 90] : "the quick brown fox "[(s >> 16) % 20];
  }
}

/* deflate data with wbits; dict if given; Z_FULL_FLUSH every flush bytes */
static void deflate_it(int wbits, const unsigned char *d, gz_headerp hd, int flush) {
  z_stream z; memset(&z, 0, sizeof z);
  z_deflateInit2_(&z, 6, Z_DEFLATED, wbits, 8, Z_DEFAULT_STRATEGY, ZLIB_VERSION, (int)sizeof z);
  if (d) z_deflateSetDictionary(&z, d, (uInt)sizeof dict);
  if (hd) z_deflateSetHeader(&z, hd);
  z.next_out = comp; z.avail_out = sizeof comp;
  for (size_t at = 0; at < N; at += flush) {
    z.next_in = data + at; z.avail_in = (uInt)(N - at < (size_t)flush ? N - at : (size_t)flush);
    z_deflate(&z, at + flush >= N ? Z_FINISH : Z_FULL_FLUSH);
  }
  clen = z.total_out;
  z_deflateEnd(&z);
}

/* inflate from z's current place, chunk bytes of input and room at a time */
static int drive(const api *a, z_streamp z, size_t chunk, size_t *at) {
  int r = Z_OK;
  while (r == Z_OK && *at <= clen) {
    z->next_in = comp + *at;
    z->avail_in = (uInt)(clen - *at < chunk ? clen - *at : chunk);
    z->next_out = out + z->total_out;
    z->avail_out = (uInt)(sizeof out - z->total_out < chunk ? sizeof out - z->total_out : chunk);
    uInt before = z->avail_in;
    r = a->inf(z, Z_NO_FLUSH);
    *at += before - z->avail_in;
    mixi(r);
    if (r == Z_BUF_ERROR && before == 0) break;
    if (r == Z_BUF_ERROR) r = Z_OK;
  }
  return r;
}

static void t_header(const api *a) {
  unsigned char extra[100], ex[10], name[5], comm[7];
  for (int i = 0; i < 100; i++) extra[i] = (unsigned char)(i * 7);
  gz_header hd; memset(&hd, 0, sizeof hd);
  hd.text = 1; hd.time = 1234; hd.os = 3; hd.extra = extra; hd.extra_len = 100;
  hd.name = (Bytef *)"a-long-file-name.txt"; hd.comment = (Bytef *)"a comment, longer than 7"; hd.hcrc = 1;
  deflate_it(31, NULL, &hd, N);
  for (size_t chunk = 1; chunk <= 9; chunk += 4) {
    z_stream z; memset(&z, 0, sizeof z);
    gz_header g; memset(&g, 0, sizeof g);
    memset(ex, 0, sizeof ex); memset(name, 0, sizeof name); memset(comm, 0, sizeof comm);
    g.extra = ex; g.extra_max = sizeof ex; g.name = name; g.name_max = sizeof name;
    g.comment = comm; g.comm_max = sizeof comm;
    mixi(a->init2(&z, 31, ZLIB_VERSION, (int)sizeof z));
    mixi(a->gethdr(&z, &g));
    size_t at = 0;
    drive(a, &z, chunk, &at);
    mix(out, z.total_out); mixi((long)z.total_out);
    mix(ex, sizeof ex); mix(name, sizeof name); mix(comm, sizeof comm);
    mixi(g.extra_len); mixi(g.done); mixi(g.hcrc); mixi((long)g.time); mixi(g.os); mixi(g.text);
    a->end(&z);
  }
}

static void t_dict(const api *a) {
  for (int raw = 0; raw < 2; raw++) {
    deflate_it(raw ? -15 : 15, dict, NULL, N);
    z_stream z; memset(&z, 0, sizeof z);
    mixi(a->init2(&z, raw ? -15 : 15, ZLIB_VERSION, (int)sizeof z));
    if (raw) mixi(a->setdict(&z, dict, (uInt)sizeof dict));
    size_t at = 0;
    int r = drive(a, &z, 700, &at);
    if (r == Z_NEED_DICT) {
      mixi(a->setdict(&z, data, 10));                 /* wrong dictionary */
      mixi(a->setdict(&z, dict, (uInt)sizeof dict));
      drive(a, &z, 700, &at);
    }
    mix(out, z.total_out); mixi((long)z.total_out);
    uInt len = 0;
    mixi(a->getdict(&z, NULL, &len)); mixi(len);
    unsigned char *back = calloc(1, 32768);
    mixi(a->getdict(&z, back, &len)); mix(back, len);
    free(back);
    a->end(&z);
  }
}

/* new memory is not zeroed, nor left holding what a freed block held, so a
   window that inflateCopy() does not fully copy shows in the output */
static void *dirty_alloc(void *opaque, uInt items, uInt size) {
  (void)opaque;
  void *p = malloc((size_t)items * size);
  if (p) memset(p, 0xa5, (size_t)items * size);
  return p;
}
static void plain_free(void *opaque, void *p) { (void)opaque; free(p); }

static void t_copy(const api *a) {
  deflate_it(15, NULL, NULL, N);
  z_stream z, c; memset(&z, 0, sizeof z);
  z.zalloc = dirty_alloc; z.zfree = plain_free;
  mixi(a->init2(&z, 15, ZLIB_VERSION, (int)sizeof z));
  size_t at = 0;
  z.next_in = comp; z.avail_in = (uInt)(clen / 2);
  z.next_out = out; z.avail_out = 3000;
  mixi(a->inf(&z, Z_NO_FLUSH));
  at = clen / 2 - z.avail_in;
  mixi(a->copy(&c, &z));
  size_t at2 = at;
  unsigned long t = z.total_out;
  drive(a, &z, 333, &at);
  mix(out, z.total_out);
  c.total_out = t;                                     /* finish the copy over the same bytes */
  drive(a, &c, 4096, &at2);
  mix(out, c.total_out); mixi((long)c.total_out);
  a->end(&z); a->end(&c);
}

static void t_sync(const api *a) {
  deflate_it(-15, NULL, NULL, 1500);
  for (int i = 3; i < 60; i++) comp[i] ^= 0x5a;        /* damage the first block */
  z_stream z; memset(&z, 0, sizeof z);
  mixi(a->init2(&z, -15, ZLIB_VERSION, (int)sizeof z));
  size_t at = 0;
  for (int tries = 0; tries < 20; tries++) {
    int r = drive(a, &z, 512, &at);
    if (r == Z_STREAM_END || at >= clen) break;
    z.next_in = comp + at; z.avail_in = (uInt)(clen - at);
    mixi(a->sync(&z));
    at = clen - z.avail_in;
    mixi((long)at);
  }
  mixi((long)z.total_out); mixi((long)z.total_in); mix(out, z.total_out);
  a->end(&z);
}

int main(void) {
  static void (*const tests[])(const api *) = {t_header, t_dict, t_copy, t_sync};
  static const char *const names[] = {"gzip header", "dictionary", "inflateCopy", "inflateSync"};
  int bad = 0;
  make();
  for (int t = 0; t < 4; t++) {
    unsigned long long hs, hp;
    h = 14695981039346656037ULL; tests[t](&stock); hs = h;
    h = 14695981039346656037ULL; tests[t](&safe); hp = h;
    if (hs != hp) { printf("api: %s differs from stock zlib\n", names[t]); bad = 1; }
  }
  if (!bad) printf("api: gzip header, dictionary, inflateCopy, inflateSync the same as stock zlib\n");
  return bad;
}
