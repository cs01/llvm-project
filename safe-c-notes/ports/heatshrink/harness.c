/* heatshrink c|d SEED < in > out: compress or decompress stdin, feeding and
   draining it in chunks whose sizes come from SEED. Plain C, built against
   either the original or the port, to compare the two. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "heatshrink_decoder.h"
#include "heatshrink_encoder.h"

static unsigned seed;
static size_t chunk(size_t max) { seed = seed * 1103515245u + 12345u; return 1 + (seed >> 8) % max; }

static unsigned char in[1 << 20], out[1 << 22];

int main(int argc, char **argv) {
  if (argc != 3) return 2;
  seed = (unsigned)atoi(argv[2]);
  size_t n = fread(in, 1, sizeof in, stdin), at = 0, o = 0, got = 0;
  if (argv[1][0] == 'c') {
    static heatshrink_encoder e;
    heatshrink_encoder_reset(&e);
    while (at < n) {
      size_t k = chunk(64); if (k > n - at) k = n - at;
      if (heatshrink_encoder_sink(&e, in + at, k, &got) < 0) return 3;
      at += got;
      HSE_poll_res p;
      do { size_t m = chunk(16); if (o + m > sizeof out) return 4; p = heatshrink_encoder_poll(&e, out + o, m, &got); o += got; } while (p == HSER_POLL_MORE);
      if (p < 0) return 5;
    }
    while (heatshrink_encoder_finish(&e) == HSER_FINISH_MORE) {
      HSE_poll_res p;
      do { size_t m = chunk(16); if (o + m > sizeof out) return 4; p = heatshrink_encoder_poll(&e, out + o, m, &got); o += got; } while (p == HSER_POLL_MORE);
      if (p < 0) return 5;
    }
  } else {
    static heatshrink_decoder d;
    heatshrink_decoder_reset(&d);
    while (at < n) {
      size_t k = chunk(64); if (k > n - at) k = n - at;
      HSD_sink_res s = heatshrink_decoder_sink(&d, in + at, k, &got);
      if (s < 0) return 3;
      at += got;
      HSD_poll_res p;
      do { size_t m = chunk(16); if (o + m > sizeof out) return 4; p = heatshrink_decoder_poll(&d, out + o, m, &got); o += got; } while (p == HSDR_POLL_MORE);
      if (p < 0) return 5;
    }
    while (heatshrink_decoder_finish(&d) == HSDR_FINISH_MORE) {
      HSD_poll_res p;
      do { size_t m = chunk(16); if (o + m > sizeof out) return 4; p = heatshrink_decoder_poll(&d, out + o, m, &got); o += got; } while (p == HSDR_POLL_MORE);
      if (p < 0) return 5;
    }
  }
  fwrite(out, 1, o, stdout);
  return 0;
}
