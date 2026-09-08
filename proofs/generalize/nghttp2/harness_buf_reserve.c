/* Does nghttp2_buf_reserve keep its own documented invariants? The interesting
   part is what it does after the reallocation, not before. */
#include "lib/nghttp2_mem.c"
#include "lib/nghttp2_buf.c"

unsigned long nondet_ulong(void);
void *malloc(unsigned long);
void free(void *);
void *calloc(unsigned long, unsigned long);
void *realloc(void *, unsigned long);
static void *h_malloc(size_t n, void *u)            { (void)u; return malloc(n); }
static void  h_free(void *p, void *u)               { (void)u; free(p); }
static void *h_calloc(size_t n, size_t s, void *u)  { (void)u; return calloc(n, s); }
static void *h_realloc(void *p, size_t n, void *u)  { (void)u; return realloc(p, n); }

#define CAP 32u

void harness(void) {
  /* nghttp2_mem_default() lives in another TU; supply the allocator directly
     so CBMC models the real malloc/realloc rather than an uninterpreted call. */
  nghttp2_mem m;
  m.mem_user_data = NULL;
  m.malloc  = h_malloc;
  m.free    = h_free;
  m.calloc  = h_calloc;
  m.realloc = h_realloc;
  nghttp2_buf buf;

  size_t cap = nondet_ulong();
  __CPROVER_assume(cap >= 1 && cap <= CAP);
  buf.begin = m.malloc(cap, NULL);
  __CPROVER_assume(buf.begin != NULL);
  buf.end = buf.begin + cap;

  size_t o1 = nondet_ulong(), o2 = nondet_ulong(), o3 = nondet_ulong();
  __CPROVER_assume(o1 <= o2 && o2 <= cap && o3 <= cap);
  buf.pos = buf.begin + o1;
  buf.last = buf.begin + o2;
  buf.mark = buf.begin + o3;

  size_t new_cap = nondet_ulong();
  __CPROVER_assume(new_cap <= 2u * CAP);

  (void)nghttp2_buf_reserve(&buf, new_cap, &m);
}
