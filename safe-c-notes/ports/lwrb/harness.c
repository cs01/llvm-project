/* lwrb SEED: random operations on two ring buffers, printing every result and
   every byte read. Plain C, built against the original or the port. (No
   lwrb_free here: after it the original crashes in write_byte and read_byte.) */
#include <stdio.h>
#include <stdlib.h>
#include "lwrb.h"

static unsigned seed;
static unsigned rnd(unsigned n) { seed = seed * 1103515245u + 12345u; return n ? (seed >> 8) % n : 0; }
static uint8_t mem_a[300], mem_b[300], tmp[400];
static int events;
static void on_event(lwrb_t *b, lwrb_evt_type_t e, lwrb_sz_t n) { (void)b; events += (int)e * 7 + (int)n; }

static void show(const char *what, unsigned long r, size_t n) {
  printf("%s %lu", what, r);
  for (size_t i = 0; i < n && i < sizeof tmp; i++) printf(" %u", tmp[i]);
  printf("\n");
}

int main(int argc, char **argv) {
  if (argc != 2) return 2;
  seed = (unsigned)atoi(argv[1]);
  lwrb_t a, b;
  lwrb_sz_t sa = 1 + rnd(299), sb = 1 + rnd(299);
  printf("init %u %u\n", lwrb_init(&a, mem_a, sa), lwrb_init(&b, mem_b, sb));
  if (rnd(2)) lwrb_set_evt_fn(&a, on_event);
  for (int step = 0; step < 3000; step++) {
    lwrb_sz_t n = rnd(4) == 0 ? rnd(400) : rnd(40), k = rnd(50), got = 0;
    for (size_t i = 0; i < sizeof tmp; i++) tmp[i] = (uint8_t)rnd(256);
    lwrb_t *x = rnd(4) ? &a : &b;
    switch (rnd(16)) {
    case 0: show("write", lwrb_write(x, tmp, n), 0); break;
    case 1: show("read", lwrb_read(x, tmp, n), n); break;
    case 2: show("peek", lwrb_peek(x, k, tmp, n), n); break;
    case 3: show("skip", lwrb_skip(x, n), 0); break;
    case 4: show("advance", lwrb_advance(x, n), 0); break;
    case 5: { uint8_t v = 0; unsigned r = lwrb_write_byte(x, (uint8_t)k); show("wbyte", r, 0); r = lwrb_read_byte(x, &v); printf("rbyte %u %u\n", r, r ? v : 0); } break;
    case 6: { unsigned r = lwrb_write_ex(x, tmp, n, &got, LWRB_FLAG_WRITE_ALL * rnd(2)); printf("write_ex %u %lu\n", r, (unsigned long)got); } break;
    case 7: { unsigned r = lwrb_read_ex(x, tmp, n, &got, LWRB_FLAG_READ_ALL * rnd(2)); show("read_ex", r, r ? got : 0); printf("got %lu\n", (unsigned long)got); } break;
    case 10: { lwrb_sz_t at = 0; uint8_t needle[3] = {tmp[0], tmp[1], tmp[2]}; unsigned r = lwrb_find(x, needle, 1 + rnd(3), rnd(20), &at); printf("find %u %lu\n", r, r ? (unsigned long)at : 0); } break;
    case 11: lwrb_reset(x); printf("reset\n"); break;
    default: printf("state %lu %lu %lu %lu\n", (unsigned long)lwrb_get_free(x), (unsigned long)lwrb_get_full(x),
                    (unsigned long)lwrb_get_linear_block_read_length(x), (unsigned long)lwrb_get_linear_block_write_length(x));
    }
  }
  printf("events %d\n", events);
  return 0;
}
