// jpeg2png. The struct cannot hold the caller's FILE; every logger function takes it.
#include <stdio.h>
struct logger { int channel, iteration; char filename[64]; };
void logger_start(struct logger *log, FILE *_Nullable f) {
  log->filename[0] = 0; log->channel = 0; log->iteration = 0;
  if (f) { if (fprintf(f, "filename,channel,iteration,objective,prob_dist,tv,tv2\n") < 0) exit(1); }
}
